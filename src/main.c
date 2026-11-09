#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

#include "bluetooth.h"
#include "button.h"
#include "conducting.h"
#include "led.h"
#include "sensor.h"

/* 1 / 416 Hz, rounded to the nearest whole microsecond. */
#define SENSOR_POLL_INTERVAL_US 2404
#define BLE_PUBLISH_INTERVAL_MS 50
/* Enable temporarily when inspecting raw sensor measurements. */
#define PRINT_MOTION_SAMPLES 0
/* Compact 40 Hz state stream for tuning with real conducting recordings. */
#define PRINT_CONDUCTING_DIAGNOSTICS 1
#define CONDUCTING_DIAGNOSTIC_INTERVAL_MS 25
#define TRAINING_CAPTURE_ENABLED DT_NODE_EXISTS(DT_NODELABEL(training_capture))

/* Adding boards/training_capture.overlay enables lossless 416 Hz binary dataset capture. The receiver scans for
 * the four-byte MBR1 marker, then sequence, timestamp, accel xyz and gyro xyz,
 * all little-endian. Disable ordinary diagnostics to preserve UART bandwidth. */
#if TRAINING_CAPTURE_ENABLED
static const struct device *const training_uart =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static void stream_training_sample(const struct motion_sample *sample)
{
	static uint16_t sequence;
	uint8_t packet[22] = {'M', 'B', 'R', '1'};

	sys_put_le16(sequence++, &packet[4]);
	sys_put_le32(sample->timestamp_ms, &packet[6]);
	sys_put_le16((uint16_t)sample->accel_x_mg, &packet[10]);
	sys_put_le16((uint16_t)sample->accel_y_mg, &packet[12]);
	sys_put_le16((uint16_t)sample->accel_z_mg, &packet[14]);
	sys_put_le16((uint16_t)sample->gyro_x_dps_tenths, &packet[16]);
	sys_put_le16((uint16_t)sample->gyro_y_dps_tenths, &packet[18]);
	sys_put_le16((uint16_t)sample->gyro_z_dps_tenths, &packet[20]);

	for (size_t i = 0; i < sizeof(packet); i++) {
		uart_poll_out(training_uart, packet[i]);
	}
}
#endif

K_MSGQ_DEFINE(button_event_queue,
	      sizeof(struct button_event),
	      8,
	      4);

static void on_button_event(const struct button_event *event)
{
	/* Do not block inside the button handler. */
	k_msgq_put(&button_event_queue, event, K_NO_WAIT);
}

int main(void)
{
	int ret;
	int64_t next_ble_ms;
	int64_t next_diagnostic_ms;
	struct conducting_detector detector;
	conducting_reset(&detector);

	ret = led_init();
	if (ret < 0) {
		printk("LED initialization failed: %d\n", ret);
		return 0;
	}

	ret = sensor_init();
	if (ret < 0) {
		return 0;
	}

	ret = button_init(on_button_event);
	if (ret < 0) {
		printk("Button initialization failed: %d\n", ret);
		return 0;
	}

	ret = bluetooth_init();
	if (ret < 0) {
		return 0;
	}

	printk("Triple-press to toggle Bluetooth\n");
	printk("Double-press to recalibrate: hold still afterward\n");
	printk("Experimental beat detector: hold still for one second to calibrate\n");
	next_ble_ms = k_uptime_get();
	next_diagnostic_ms = next_ble_ms;

	while (1) {
		/* Use a fresh deadline each cycle; do not catch up in bursts. */
		int64_t cycle_start_us = k_ticks_to_us_floor64(k_uptime_ticks());
		int64_t cycle_start_ms = cycle_start_us / 1000;
		int64_t next_sample_us = cycle_start_us + SENSOR_POLL_INTERVAL_US;
		struct button_event event;
		struct motion_sample sample;


		while (k_msgq_get(&button_event_queue,
				  &event,
				  K_NO_WAIT) == 0) {
			if (event.type == BUTTON_EVENT_PRESSED) {
				printk("Baton button pressed\n");
			}

			if (event.type == BUTTON_EVENT_LONG_HOLD) {
				printk("Ten-second hold: rebooting\n");
				k_sleep(K_MSEC(100));
				sys_reboot(SYS_REBOOT_COLD);
			}

			if (event.type == BUTTON_EVENT_CLICKS) {
				printk("Detected %u clicks\n",
				       event.click_count);

				if (event.click_count == 2) {
					/* Reset on the main thread, after the complete click
					 * sequence; a triple-click must not recalibrate.
					 */
					conducting_reset(&detector);
					conducting_set_sensitivity(&detector,
						bluetooth_conducting_sensitivity());
					printk("Recalibration requested: hold still for one second\n");
				} else if (event.click_count == 3) {
					printk("Triple-click detected\n");

					if (bluetooth_is_active()) {
						ret = bluetooth_deactivate();
					} else {
						ret = bluetooth_activate();
					}

					if (ret < 0) {
						printk("Bluetooth toggle failed: %d\n", ret);
					}
				}
			}
		}

		ret = sensor_read(&sample);
		if (ret < 0) {
			printk("Sensor read failed: %d\n", ret);
			k_sleep(K_SECONDS(1));
			continue;
		}

		/* Submit beats before motion telemetry or UART work from this cycle. */
		bool was_calibrated = detector.calibrated;
		conducting_set_sensitivity(&detector, bluetooth_conducting_sensitivity());
		struct conducting_beat beat;
		bool have_beat = conducting_process(&detector, &sample, &beat);
		if (have_beat) {
			ret = bluetooth_publish_beat(beat.timestamp_ms, beat.bpm_tenths,
						     beat.strength);
			if (ret < 0) {
				printk("Bluetooth beat notification failed: %d\n", ret);
			}
		}

		/* Gate on cycle start so fetch jitter cannot skip alternate samples.
		 * Never retry a failed notification or catch up missed deadlines.
		 */
		if (bluetooth_is_active() && cycle_start_ms >= next_ble_ms) {
			next_ble_ms = cycle_start_ms + BLE_PUBLISH_INTERVAL_MS;
			const struct bluetooth_motion_sample motion_sample = {
				.timestamp_ms = sample.timestamp_ms,
				.flags = BLUETOOTH_MOTION_FLAG_ACCEL_VALID |
					 BLUETOOTH_MOTION_FLAG_GYRO_VALID,
				.accel_x_mg = sample.accel_x_mg,
				.accel_y_mg = sample.accel_y_mg,
				.accel_z_mg = sample.accel_z_mg,
				.gyro_x_dps_tenths = sample.gyro_x_dps_tenths,
				.gyro_y_dps_tenths = sample.gyro_y_dps_tenths,
				.gyro_z_dps_tenths = sample.gyro_z_dps_tenths,
			};

			ret = bluetooth_publish(&motion_sample);

			if (ret < 0) {
				printk("Bluetooth publish failed: %d\n",
				       ret);
			}
		}

		if (have_beat) {
			printk("BEAT: t=%u detected=%u strength=%u BPM=%u.%u\n",
			       beat.timestamp_ms, beat.detected_ms, beat.strength,
			       beat.bpm_tenths / 10, beat.bpm_tenths % 10);
		}
		if (detector.calibrated != was_calibrated) {
			printk(detector.calibrated ? "Beat detector calibrated\n" :
			       "Beat detector reset: hold still for one second\n");
		}

#if TRAINING_CAPTURE_ENABLED
		stream_training_sample(&sample);
#endif
#if PRINT_CONDUCTING_DIAGNOSTICS && !TRAINING_CAPTURE_ENABLED
		if (!bluetooth_is_connected() && cycle_start_ms >= next_diagnostic_ms) {
			next_diagnostic_ms = cycle_start_ms +
				CONDUCTING_DIAGNOSTIC_INTERVAL_MS;
			/* CD: time, gyro xyz (0.1 dps), accel xyz (mg),
			 * smoothed speed (0.1 dps),
			 * linear acceleration (mg), armed, release (0.1 dps),
			 * followed by eight reserved compatibility fields (currently zero).
			 */
			printk("CD,%u,%d,%d,%d,%d,%d,%d,%d,%d,%u,%d,%u,%u,%u,%d,%d,%u,%d\n",
			       sample.timestamp_ms,
			       sample.gyro_x_dps_tenths,
			       sample.gyro_y_dps_tenths,
			       sample.gyro_z_dps_tenths,
			       sample.accel_x_mg,
			       sample.accel_y_mg,
			       sample.accel_z_mg,
			       (int)(detector.speed * 10.0f),
			       0,
			       detector.armed,
			       (int)(detector.release_dps * 10.0f),
			       0, 0, 0, 0, 0, 0, 0);
		}
#endif
#if PRINT_MOTION_SAMPLES
		printk("IMU accel [mg]: X=%d Y=%d Z=%d | "
		       "gyro [0.1 deg/s]: X=%d Y=%d Z=%d\n",
		       sample.accel_x_mg, sample.accel_y_mg,
		       sample.accel_z_mg, sample.gyro_x_dps_tenths,
		       sample.gyro_y_dps_tenths, sample.gyro_z_dps_tenths);
#endif

		k_sleep(K_TIMEOUT_ABS_US(next_sample_us));
	}
}
