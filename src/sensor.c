#include "sensor.h"

#include <errno.h>
#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define IMU_NODE DT_ALIAS(baton_imu)

/* AN5272: ODR/10 LPF2 can take 20 samples to settle at 52 Hz (~385 ms).
 * Leave margin before exposing measurements. Revisit if ODR/filter changes.
 */
#define IMU_MEASUREMENT_SETTLE_MS 500

static const struct device *const imu = DEVICE_DT_GET(IMU_NODE);
static bool initialized;
static const struct i2c_dt_spec imu_i2c = I2C_DT_SPEC_GET(IMU_NODE);

static void diagnose_i2c(void)
{
	/* These are read-only identity probes, not an automatic address change. */
	for (unsigned int i = 0; i < 2; i++) {
		uint16_t address = imu_i2c.addr ^ i;
		uint8_t identity;
		int ret = i2c_reg_read_byte(imu_i2c.bus, address, 0x0f, &identity);

		if (ret == 0) {
			printk("IMU probe 0x%02x: WHO_AM_I=0x%02x (expected 0x6c)\n",
			       address, identity);
		} else {
			printk("IMU probe 0x%02x: read failed (%d)\n", address, ret);
		}
	}
}

static int16_t accel_to_mg(const struct sensor_value *value)
{
	return (int16_t)CLAMP(sensor_ms2_to_mg(value), INT16_MIN, INT16_MAX);
}

static int16_t gyro_to_dps_tenths(const struct sensor_value *value)
{
	/* Zephyr returns rad/s. Its helper converts to units of 0.00001 deg/s.
	 * Round symmetrically to 0.1 deg/s before packing into the BLE fields.
	 */
	int32_t angular_velocity = sensor_rad_to_10udegrees(value);
	int32_t tenths = (angular_velocity +
			 (angular_velocity >= 0 ? 5000 : -5000)) / 10000;

	return (int16_t)CLAMP(tenths, INT16_MIN, INT16_MAX);
}

int sensor_init(void)
{
	if (initialized) {
		return 0;
	}

	/* Deferred initialization prevents I2C access during sensor power-up. */
	k_sleep(K_MSEC(100));
	if (!i2c_is_ready_dt(&imu_i2c)) {
		printk("IMU I2C controller is not ready\n");
		return -ENODEV;
	}

	int ret = device_init(imu);

	if (ret < 0 && ret != -EALREADY) {
		printk("LSM6DSOX initialization failed: %d\n", ret);
		diagnose_i2c();
		return ret;
	}
	if (!device_is_ready(imu)) {
		printk("LSM6DSOX is not ready; check I2C wiring and address\n");
		diagnose_i2c();
		return -ENODEV;
	}

	/* Allow accelerometer LPF2 and the gyroscope to settle. */
	k_sleep(K_MSEC(IMU_MEASUREMENT_SETTLE_MS));
	initialized = true;
	printk("LSM6DSOX ready (accelerometer and gyroscope)\n");
	return 0;
}

int sensor_read(struct motion_sample *sample)
{
	struct sensor_value accel[3];
	struct sensor_value gyro[3];
	int ret;

	if (sample == NULL) {
		return -EINVAL;
	}
	if (!initialized) {
		return -EACCES;
	}

	ret = sensor_sample_fetch(imu);
	if (ret < 0) {
		return ret;
	}
	uint32_t timestamp_ms = k_uptime_get_32();

	ret = sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, accel);
	if (ret < 0) {
		return ret;
	}
	ret = sensor_channel_get(imu, SENSOR_CHAN_GYRO_XYZ, gyro);
	if (ret < 0) {
		return ret;
	}

	*sample = (struct motion_sample) {
		.timestamp_ms = timestamp_ms,
		.accel_x_mg = accel_to_mg(&accel[0]),
		.accel_y_mg = accel_to_mg(&accel[1]),
		.accel_z_mg = accel_to_mg(&accel[2]),
		.gyro_x_dps_tenths = gyro_to_dps_tenths(&gyro[0]),
		.gyro_y_dps_tenths = gyro_to_dps_tenths(&gyro[1]),
		.gyro_z_dps_tenths = gyro_to_dps_tenths(&gyro[2]),
	};
	return 0;
}
