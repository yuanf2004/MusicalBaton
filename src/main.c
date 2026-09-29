// #include <zephyr/kernel.h>
// #include <zephyr/device.h>
// #include <zephyr/devicetree.h>
// #include <zephyr/drivers/i2c.h>
// #include <zephyr/sys/printk.h>
// #include <stdint.h>

// #define MMA8451_NODE DT_NODELABEL(mma8451)

// /* MMA8451 register addresses */
// #define MMA8451_REG_STATUS       0x00
// #define MMA8451_REG_OUT_X_MSB    0x01
// #define MMA8451_REG_WHO_AM_I     0x0D
// #define MMA8451_REG_CTRL_REG1    0x2A

// /* Expected WHO_AM_I value for MMA8451 */
// #define MMA8451_WHO_AM_I_VALUE   0x1A

// static const struct i2c_dt_spec mma8451 =
// 	I2C_DT_SPEC_GET(MMA8451_NODE);

// int main(void)
// {
// 	uint8_t who_am_i;
// 	uint8_t ctrl_reg1;
// 	int ret;

// 	if (!i2c_is_ready_dt(&mma8451)) {
// 		printk("I2C device is not ready\n");
// 		return 0;
// 	}

// 	printk("I2C controller is ready\n");

// 	/* Verify that the sensor responds */
// 	ret = i2c_reg_read_byte_dt(
// 		&mma8451,
// 		MMA8451_REG_WHO_AM_I,
// 		&who_am_i
// 	);

// 	if (ret < 0) {
// 		printk("Failed to read WHO_AM_I: %d\n", ret);
// 		return 0;
// 	}

// 	printk("WHO_AM_I: 0x%02X\n", who_am_i);

// 	if (who_am_i != MMA8451_WHO_AM_I_VALUE) {
// 		printk("Unexpected sensor ID; expected 0x%02X\n",
// 		       MMA8451_WHO_AM_I_VALUE);
// 		return 0;
// 	}

// 	/*
// 	 * Read CTRL_REG1, then set bit 0 to place the MMA8451
// 	 * into active measurement mode.
// 	 */
// 	ret = i2c_reg_read_byte_dt(
// 		&mma8451,
// 		MMA8451_REG_CTRL_REG1,
// 		&ctrl_reg1
// 	);

// 	if (ret < 0) {
// 		printk("Failed to read CTRL_REG1: %d\n", ret);
// 		return 0;
// 	}

// 	ctrl_reg1 |= 0x01;

// 	ret = i2c_reg_write_byte_dt(
// 		&mma8451,
// 		MMA8451_REG_CTRL_REG1,
// 		ctrl_reg1
// 	);

// 	if (ret < 0) {
// 		printk("Failed to activate MMA8451: %d\n", ret);
// 		return 0;
// 	}

// 	printk("MMA8451 active\n");

// 	while (1) {
// 		uint8_t data[6];

// 		/*
// 		 * Read six consecutive registers:
// 		 * X MSB, X LSB, Y MSB, Y LSB, Z MSB, Z LSB
// 		 */
// 		ret = i2c_burst_read_dt(
// 			&mma8451,
// 			MMA8451_REG_OUT_X_MSB,
// 			data,
// 			sizeof(data)
// 		);

// 		if (ret < 0) {
// 			printk("Failed to read acceleration: %d\n", ret);
// 			k_sleep(K_SECONDS(1));
// 			continue;
// 		}

// 		/*
// 		 * MMA8451 output is signed 14-bit data stored
// 		 * left-aligned in two bytes.
// 		 */
// 		int16_t x_raw =
// 			(int16_t)((data[0] << 8) | data[1]) >> 2;

// 		int16_t y_raw =
// 			(int16_t)((data[2] << 8) | data[3]) >> 2;

// 		int16_t z_raw =
// 			(int16_t)((data[4] << 8) | data[5]) >> 2;

// 		/*
// 		 * Default range is ±2 g, giving approximately
// 		 * 4096 counts per g.
// 		 */
// 		int32_t x_mg = ((int32_t)x_raw * 1000) / 4096;
// 		int32_t y_mg = ((int32_t)y_raw * 1000) / 4096;
// 		int32_t z_mg = ((int32_t)z_raw * 1000) / 4096;

// 		printk(
// 			"X: %d mg, Y: %d mg, Z: %d mg\n",
// 			x_mg,
// 			y_mg,
// 			z_mg
// 		);

// 		k_sleep(K_MSEC(200));
// 	}

// 	return 0;
// }

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <stdint.h>
#include <stdbool.h>

#define MMA8451_NODE DT_NODELABEL(mma8451)

/* MMA8451 register addresses */
#define MMA8451_REG_OUT_X_MSB  0x01
#define MMA8451_REG_WHO_AM_I   0x0D
#define MMA8451_REG_CTRL_REG1  0x2A

#define MMA8451_WHO_AM_I_VALUE 0x1A

static const struct i2c_dt_spec mma8451 =
	I2C_DT_SPEC_GET(MMA8451_NODE);

/*
 * Custom Bluetooth UUIDs
 *
 * Service:
 * 12345678-1234-5678-1234-56789abcdef0
 *
 * Accelerometer characteristic:
 * 12345678-1234-5678-1234-56789abcdef1
 */
#define BT_UUID_ACCEL_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, \
			   0x1234, 0x56789abcdef0)

#define BT_UUID_ACCEL_DATA_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, \
			   0x1234, 0x56789abcdef1)

static struct bt_uuid_128 accel_service_uuid =
	BT_UUID_INIT_128(BT_UUID_ACCEL_SERVICE_VAL);

static struct bt_uuid_128 accel_data_uuid =
	BT_UUID_INIT_128(BT_UUID_ACCEL_DATA_VAL);

/*
 * Six-byte packet:
 *
 * Bytes 0-1: X acceleration in mg
 * Bytes 2-3: Y acceleration in mg
 * Bytes 4-5: Z acceleration in mg
 */
static uint8_t accel_packet[6];

static bool notifications_enabled;

/*
 * Called when the phone reads the characteristic manually.
 */
static ssize_t read_accel(
	struct bt_conn *conn,
	const struct bt_gatt_attr *attr,
	void *buf,
	uint16_t len,
	uint16_t offset)
{
	return bt_gatt_attr_read(
		conn,
		attr,
		buf,
		len,
		offset,
		accel_packet,
		sizeof(accel_packet)
	);
}

/*
 * Called when the phone enables or disables notifications.
 */
static void accel_ccc_changed(
	const struct bt_gatt_attr *attr,
	uint16_t value)
{
	ARG_UNUSED(attr);

	notifications_enabled = (value == BT_GATT_CCC_NOTIFY);

	printk(
		"Notifications %s\n",
		notifications_enabled ? "enabled" : "disabled"
	);
}

/*
 * Attribute indexes:
 *
 * accel_service.attrs[0] = primary service
 * accel_service.attrs[1] = characteristic declaration
 * accel_service.attrs[2] = characteristic value
 * accel_service.attrs[3] = CCC descriptor
 */
BT_GATT_SERVICE_DEFINE(
	accel_service,

	BT_GATT_PRIMARY_SERVICE(&accel_service_uuid),

	BT_GATT_CHARACTERISTIC(
		&accel_data_uuid.uuid,
		BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_READ,
		read_accel,
		NULL,
		accel_packet
	),

	BT_GATT_CCC(
		accel_ccc_changed,
		BT_GATT_PERM_READ | BT_GATT_PERM_WRITE
	)
);

/*
 * Advertising packet.
 *
 * The device name will appear as "Smart Baton".
 */
static const struct bt_data advertising_data[] = {
	BT_DATA_BYTES(
		BT_DATA_FLAGS,
		BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR
	),

	BT_DATA(
		BT_DATA_NAME_COMPLETE,
		CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1
	)
};

/*
 * Put the custom service UUID in the scan response.
 */
static const struct bt_data scan_response_data[] = {
	BT_DATA_BYTES(
		BT_DATA_UUID128_ALL,
		BT_UUID_ACCEL_SERVICE_VAL
	)
};

static int bluetooth_start(void)
{
	int ret;

	ret = bt_enable(NULL);

	if (ret < 0) {
		printk("Bluetooth initialization failed: %d\n", ret);
		return ret;
	}

	printk("Bluetooth initialized\n");

	ret = bt_le_adv_start(
		BT_LE_ADV_CONN_FAST_1,
		advertising_data,
		ARRAY_SIZE(advertising_data),
		scan_response_data,
		ARRAY_SIZE(scan_response_data)
	);

	if (ret < 0) {
		printk("Advertising failed: %d\n", ret);
		return ret;
	}

	printk("Advertising as %s\n", CONFIG_BT_DEVICE_NAME);

	return 0;
}

int main(void)
{
	uint8_t who_am_i;
	uint8_t ctrl_reg1;
	int ret;

	if (!i2c_is_ready_dt(&mma8451)) {
		printk("I2C device is not ready\n");
		return 0;
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
		return 0;
	}

	printk("WHO_AM_I: 0x%02X\n", who_am_i);

	if (who_am_i != MMA8451_WHO_AM_I_VALUE) {
		printk(
			"Unexpected sensor ID; expected 0x%02X\n",
			MMA8451_WHO_AM_I_VALUE
		);

		return 0;
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
		return 0;
	}

	ctrl_reg1 |= 0x01;

	ret = i2c_reg_write_byte_dt(
		&mma8451,
		MMA8451_REG_CTRL_REG1,
		ctrl_reg1
	);

	if (ret < 0) {
		printk("Failed to activate MMA8451: %d\n", ret);
		return 0;
	}

	printk("MMA8451 active\n");

	/*
	 * Start Bluetooth only after confirming that the sensor works.
	 */
	ret = bluetooth_start();

	if (ret < 0) {
		return 0;
	}

	while (1) {
		uint8_t data[6];

		ret = i2c_burst_read_dt(
			&mma8451,
			MMA8451_REG_OUT_X_MSB,
			data,
			sizeof(data)
		);

		if (ret < 0) {
			printk("Failed to read acceleration: %d\n", ret);
			k_sleep(K_SECONDS(1));
			continue;
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
		int16_t x_mg =
			(int16_t)(((int32_t)x_raw * 1000) / 4096);

		int16_t y_mg =
			(int16_t)(((int32_t)y_raw * 1000) / 4096);

		int16_t z_mg =
			(int16_t)(((int32_t)z_raw * 1000) / 4096);

		printk(
			"X: %d mg, Y: %d mg, Z: %d mg\n",
			x_mg,
			y_mg,
			z_mg
		);

		/*
		 * Pack the three signed values into six bytes.
		 *
		 * Bluetooth does not inherently understand C structs,
		 * so defining an explicit byte format is safer.
		 */
		sys_put_le16((uint16_t)x_mg, &accel_packet[0]);
		sys_put_le16((uint16_t)y_mg, &accel_packet[2]);
		sys_put_le16((uint16_t)z_mg, &accel_packet[4]);

		/*
		 * Send the packet only after the phone has subscribed.
		 *
		 * Passing NULL sends to every connected peer that has
		 * enabled notifications on this characteristic.
		 */
		if (notifications_enabled) {
			ret = bt_gatt_notify(
				NULL,
				&accel_service.attrs[2],
				accel_packet,
				sizeof(accel_packet)
			);

			if (ret < 0) {
				printk(
					"Failed to send notification: %d\n",
					ret
				);
			}
		}

		k_sleep(K_MSEC(200));
	}

	return 0;
}


