#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "sensor.h"
#include "bluetooth.h"

int main(void)
{
	int ret = sensor_init();

	if (ret < 0) {
		return 0;
	}

	/* Advertise only after confirming that the sensor works. */
	ret = bluetooth_start();
	if (ret < 0) {
		return 0;
	}

	while (1) {
		struct acceleration sample;

		ret = sensor_read(&sample);
		if (ret < 0) {
			printk("Failed to read acceleration: %d\n", ret);
			k_sleep(K_SECONDS(1));
			continue;
		}

		printk("X: %d mg, Y: %d mg, Z: %d mg\n",
		       sample.x_mg, sample.y_mg, sample.z_mg);

		ret = bluetooth_publish(sample.x_mg, sample.y_mg, sample.z_mg);
		if (ret < 0) {
			printk("Failed to send notification: %d\n", ret);
		}

		k_sleep(K_MSEC(200));
	}

	return 0;
}
