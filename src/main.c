#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

#include "bluetooth.h"
#include "button.h"
#include "led.h"
#include "sensor.h"

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

	while (1) {
		struct button_event event;
		struct acceleration sample;

		while (k_msgq_get(&button_event_queue,
				  &event,
				  K_NO_WAIT) == 0) {
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

		if (bluetooth_is_active()) {
			ret = bluetooth_publish(
				sample.x_mg,
				sample.y_mg,
				sample.z_mg
			);

			if (ret < 0) {
				printk("Bluetooth publish failed: %d\n",
				       ret);
			}
		}

		k_sleep(K_MSEC(200));
	}
}
