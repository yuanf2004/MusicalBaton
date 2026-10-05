#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

#include "bluetooth.h"
#include "button.h"
#include "led.h"
#include "sensor.h"

#define SENSOR_POLL_INTERVAL_MS 50
#define BLE_PUBLISH_INTERVAL_MS 50

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
	next_ble_ms = k_uptime_get();

	while (1) {
		/* Use a fresh deadline each cycle; do not catch up in bursts. */
		int64_t cycle_start_ms = k_uptime_get();
		int64_t next_sample_ms = cycle_start_ms + SENSOR_POLL_INTERVAL_MS;
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

				if (event.click_count == 3) {
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

		/* UART output follows publication so it cannot delay this notification. */
		printk("IMU accel [mg]: X=%d Y=%d Z=%d | "
		       "gyro [0.1 deg/s]: X=%d Y=%d Z=%d\n",
		       sample.accel_x_mg, sample.accel_y_mg,
		       sample.accel_z_mg, sample.gyro_x_dps_tenths,
		       sample.gyro_y_dps_tenths, sample.gyro_z_dps_tenths);

		k_sleep(K_TIMEOUT_ABS_MS(next_sample_ms));
	}
}
