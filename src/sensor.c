#include "sensor.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

#define MMA8451_NODE DT_NODELABEL(mma8451)

/* MMA8451 register addresses */
#define MMA8451_REG_OUT_X_MSB  0x01
#define MMA8451_REG_WHO_AM_I   0x0D
#define MMA8451_REG_CTRL_REG1  0x2A

#define MMA8451_WHO_AM_I_VALUE 0x1A

static const struct i2c_dt_spec mma8451 =
	I2C_DT_SPEC_GET(MMA8451_NODE);

int sensor_init(void)
{
	uint8_t who_am_i;
	uint8_t ctrl_reg1;
	int ret;

	if (!i2c_is_ready_dt(&mma8451)) {
		printk("I2C device is not ready\n");
		return -ENODEV;
	}

	printk("I2C controller is ready\n");

	/* Verify that the sensor responds. */
	ret = i2c_reg_read_byte_dt(
		&mma8451,
		MMA8451_REG_WHO_AM_I,
		&who_am_i
	);

	if (ret < 0) {
		printk("Failed to read WHO_AM_I: %d\n", ret);
		return ret;
	}

	printk("WHO_AM_I: 0x%02X\n", who_am_i);

	if (who_am_i != MMA8451_WHO_AM_I_VALUE) {
		printk(
			"Unexpected sensor ID; expected 0x%02X\n",
			MMA8451_WHO_AM_I_VALUE
		);

		return -ENODEV;
	}

	/*
	 * Read CTRL_REG1 and set bit 0 to enter active mode.
	 */
	ret = i2c_reg_read_byte_dt(
		&mma8451,
		MMA8451_REG_CTRL_REG1,
		&ctrl_reg1
	);

	if (ret < 0) {
		printk("Failed to read CTRL_REG1: %d\n", ret);
		return ret;
	}

	ctrl_reg1 |= 0x01;

	ret = i2c_reg_write_byte_dt(
		&mma8451,
		MMA8451_REG_CTRL_REG1,
		ctrl_reg1
	);

	if (ret < 0) {
		printk("Failed to activate MMA8451: %d\n", ret);
		return ret;
	}

	printk("MMA8451 active\n");

	return 0;
}

int sensor_read(struct acceleration *sample)
{
	uint8_t data[6];
	int ret = i2c_burst_read_dt(&mma8451, MMA8451_REG_OUT_X_MSB,
				  data, sizeof(data));

	if (ret < 0) {
		return ret;
	}

	/*
	 * Signed 14-bit output, left-aligned within 16 bits.
	 */
	int16_t x_raw =
		(int16_t)((data[0] << 8) | data[1]) >> 2;

	int16_t y_raw =
		(int16_t)((data[2] << 8) | data[3]) >> 2;

	int16_t z_raw =
		(int16_t)((data[4] << 8) | data[5]) >> 2;

	/*
	 * At the default ±2 g range:
	 * approximately 4096 counts per g.
	 */
	sample->x_mg =
		(int16_t)(((int32_t)x_raw * 1000) / 4096);

	sample->y_mg =
		(int16_t)(((int32_t)y_raw * 1000) / 4096);

	sample->z_mg =
		(int16_t)(((int32_t)z_raw * 1000) / 4096);

	return 0;
}
