#include "bluetooth.h"
#include "led.h"

#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>

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
static bool bluetooth_initialized;
static atomic_t bluetooth_active;
static atomic_t advertising;
static struct bt_conn *current_connection;
static K_MUTEX_DEFINE(connection_mutex);

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

static int start_advertising(void)
{
	int ret;

	if (atomic_get(&advertising)) {
		return 0;
	}

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

	atomic_set(&advertising, 1);
	led_set_bluetooth_state(LED_BLUETOOTH_ADVERTISING);
	printk("Advertising as %s\n", CONFIG_BT_DEVICE_NAME);

	return 0;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err != 0U) {
		printk("Bluetooth connection failed: 0x%02x\n", err);
		atomic_clear(&advertising);
		if (atomic_get(&bluetooth_active)) {
			start_advertising();
		}
		return;
	}

	atomic_clear(&advertising);

	/* A deactivation may race with the controller completing a connection. */
	if (!atomic_get(&bluetooth_active)) {
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		return;
	}

	k_mutex_lock(&connection_mutex, K_FOREVER);
	current_connection = bt_conn_ref(conn);
	k_mutex_unlock(&connection_mutex);

	led_set_bluetooth_state(LED_BLUETOOTH_CONNECTED);
	printk("Phone connected\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);

	printk("Phone disconnected: 0x%02x\n", reason);
	notifications_enabled = false;

	k_mutex_lock(&connection_mutex, K_FOREVER);
	if (current_connection != NULL) {
		bt_conn_unref(current_connection);
		current_connection = NULL;
	}
	k_mutex_unlock(&connection_mutex);

	/* Resume advertising after an unexpected disconnect while active. */
	if (atomic_get(&bluetooth_active)) {
		start_advertising();
	}
}

BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

int bluetooth_init(void)
{
	int ret;

	if (bluetooth_initialized) {
		return 0;
	}

	ret = bt_enable(NULL);
	if (ret < 0) {
		printk("Bluetooth initialization failed: %d\n", ret);
		return ret;
	}

	bluetooth_initialized = true;
	led_set_bluetooth_state(LED_BLUETOOTH_OFF);
	printk("Bluetooth initialized; advertising is off\n");

	return 0;
}

int bluetooth_activate(void)
{
	int ret;

	if (!bluetooth_initialized) {
		return -EACCES;
	}

	if (!atomic_cas(&bluetooth_active, 0, 1)) {
		return 0;
	}

	ret = start_advertising();
	if (ret < 0) {
		atomic_clear(&bluetooth_active);
		return ret;
	}

	printk("Bluetooth active\n");
	return 0;
}

int bluetooth_deactivate(void)
{
	struct bt_conn *conn = NULL;
	int ret = 0;
	int disconnect_ret;

	if (!atomic_cas(&bluetooth_active, 1, 0)) {
		return 0;
	}

	/* Clear this first so the disconnect callback does not advertise again. */
	notifications_enabled = false;
	led_set_bluetooth_state(LED_BLUETOOTH_OFF);

	if (atomic_cas(&advertising, 1, 0)) {
		ret = bt_le_adv_stop();
		if (ret < 0) {
			printk("Failed to stop advertising: %d\n", ret);
		}
	}

	k_mutex_lock(&connection_mutex, K_FOREVER);
	if (current_connection != NULL) {
		conn = bt_conn_ref(current_connection);
	}
	k_mutex_unlock(&connection_mutex);

	if (conn != NULL) {
		disconnect_ret = bt_conn_disconnect(
			conn,
			BT_HCI_ERR_REMOTE_USER_TERM_CONN
		);
		bt_conn_unref(conn);

		if (disconnect_ret < 0) {
			printk("Failed to disconnect phone: %d\n", disconnect_ret);
			if (ret == 0) {
				ret = disconnect_ret;
			}
		}
	}

	printk("Bluetooth inactive\n");
	return ret;
}

bool bluetooth_is_active(void)
{
	return atomic_get(&bluetooth_active) != 0;
}


int bluetooth_publish(int16_t x_mg, int16_t y_mg, int16_t z_mg)
{
	/* Three signed 16-bit values, explicitly encoded little-endian. */
	sys_put_le16((uint16_t)x_mg, &accel_packet[0]);
	sys_put_le16((uint16_t)y_mg, &accel_packet[2]);
	sys_put_le16((uint16_t)z_mg, &accel_packet[4]);

	if (!notifications_enabled) {
		return 0;
	}

	/* Notify connected peers that subscribed to this characteristic. */
	return bt_gatt_notify(NULL, &accel_service.attrs[2],
			      accel_packet, sizeof(accel_packet));
}
