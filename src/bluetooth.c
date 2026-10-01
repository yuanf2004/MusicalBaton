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
 * Motion characteristic:
 * 12345678-1234-5678-1234-56789abcdef1
 */
#define BT_UUID_MOTION_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, \
			   0x1234, 0x56789abcdef0)

#define BT_UUID_MOTION_DATA_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, \
			   0x1234, 0x56789abcdef1)

static struct bt_uuid_128 motion_service_uuid =
	BT_UUID_INIT_128(BT_UUID_MOTION_SERVICE_VAL);

static struct bt_uuid_128 motion_data_uuid =
	BT_UUID_INIT_128(BT_UUID_MOTION_DATA_VAL);

/*
 * Fixed 20-byte motion packet, encoded explicitly in little-endian order:
 *
 * Byte 0:     packet version
 * Byte 1:     validity/status flags
 * Bytes 2-3:  sequence number
 * Bytes 4-7:  nRF uptime timestamp in milliseconds
 * Bytes 8-13: accelerometer X/Y/Z in mg
 * Bytes 14-19: gyroscope X/Y/Z in tenths of a degree per second
 */
#define MOTION_PACKET_SIZE 20U
#define ADVERTISING_TIMEOUT_MS 60000

static uint8_t motion_packet[MOTION_PACKET_SIZE];
static uint16_t motion_sequence;

static bool notifications_enabled;
static bool bluetooth_initialized;
static atomic_t bluetooth_active;
static atomic_t advertising;
static struct bt_conn *current_connection;
static int64_t advertising_deadline_ms;
static K_MUTEX_DEFINE(connection_mutex);
static K_MUTEX_DEFINE(motion_packet_mutex);

/*
 * Called when the phone reads the characteristic manually.
 */
static ssize_t read_motion(
	struct bt_conn *conn,
	const struct bt_gatt_attr *attr,
	void *buf,
	uint16_t len,
	uint16_t offset)
{
	ssize_t result;

	k_mutex_lock(&motion_packet_mutex, K_FOREVER);
	result = bt_gatt_attr_read(
		conn,
		attr,
		buf,
		len,
		offset,
		motion_packet,
		sizeof(motion_packet)
	);
	k_mutex_unlock(&motion_packet_mutex);

	return result;
}

/*
 * Called when the phone enables or disables notifications.
 */
static void motion_ccc_changed(
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
 * motion_service.attrs[0] = primary service
 * motion_service.attrs[1] = characteristic declaration
 * motion_service.attrs[2] = characteristic value
 * motion_service.attrs[3] = CCC descriptor
 */
BT_GATT_SERVICE_DEFINE(
	motion_service,

	BT_GATT_PRIMARY_SERVICE(&motion_service_uuid),

	BT_GATT_CHARACTERISTIC(
		&motion_data_uuid.uuid,
		BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_READ,
		read_motion,
		NULL,
		motion_packet
	),

	BT_GATT_CCC(
		motion_ccc_changed,
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
		BT_UUID_MOTION_SERVICE_VAL
	)
};

static void advertising_timeout_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(advertising_timeout_work,
			       advertising_timeout_handler);

static void advertising_timeout_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	/* Serialize expiry with connection acceptance and advertising starts. */
	k_mutex_lock(&connection_mutex, K_FOREVER);
	if (!atomic_get(&bluetooth_active) || !atomic_get(&advertising) ||
	    current_connection != NULL) {
		k_mutex_unlock(&connection_mutex);
		return;
	}

	int64_t remaining_ms = advertising_deadline_ms - k_uptime_get();

	if (remaining_ms > 0) {
		/* An old queued expiry must not close a newly opened window. */
		k_work_reschedule(&advertising_timeout_work, K_MSEC(remaining_ms));
		k_mutex_unlock(&connection_mutex);
		return;
	}

	int ret = bt_le_adv_stop();

	if (ret < 0) {
		printk("Advertising timeout: failed to stop (%d); retrying\n", ret);
		k_work_reschedule(&advertising_timeout_work, K_SECONDS(1));
		k_mutex_unlock(&connection_mutex);
		return;
	}

	atomic_clear(&advertising);
	atomic_clear(&bluetooth_active);
	led_set_bluetooth_state(LED_BLUETOOTH_OFF);
	printk("Advertising timed out after 60 seconds; Bluetooth inactive. "
	       "Triple-press to restart\n");
	k_mutex_unlock(&connection_mutex);
}

static int start_advertising(void)
{
	int ret;

	k_mutex_lock(&connection_mutex, K_FOREVER);
	if (!atomic_get(&bluetooth_active)) {
		k_mutex_unlock(&connection_mutex);
		return -EACCES;
	}

	if (atomic_get(&advertising) || current_connection != NULL) {
		k_mutex_unlock(&connection_mutex);
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
		k_mutex_unlock(&connection_mutex);
		return ret;
	}

	atomic_set(&advertising, 1);
	advertising_deadline_ms = k_uptime_get() + ADVERTISING_TIMEOUT_MS;
	k_work_reschedule(&advertising_timeout_work,
			  K_MSEC(ADVERTISING_TIMEOUT_MS));
	led_set_bluetooth_state(LED_BLUETOOTH_ADVERTISING);
	printk("Advertising as %s (60-second connection window)\n",
	       CONFIG_BT_DEVICE_NAME);
	k_mutex_unlock(&connection_mutex);

	return 0;
}

static void advertising_restart_handler(struct k_work *work)
{
	int ret;

	if (!atomic_get(&bluetooth_active)) {
		return;
	}

	ret = start_advertising();
	if (ret < 0 && atomic_get(&bluetooth_active)) {
		printk("Retrying advertising restart\n");
		k_work_reschedule(k_work_delayable_from_work(work),
				  K_MSEC(500));
	}
}

static K_WORK_DELAYABLE_DEFINE(advertising_restart_work,
			       advertising_restart_handler);

static void schedule_advertising_restart(void)
{
	if (atomic_get(&bluetooth_active)) {
		k_work_reschedule(&advertising_restart_work, K_MSEC(100));
	}
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err != 0U) {
		printk("Bluetooth connection failed: 0x%02x\n", err);
		atomic_clear(&advertising);
		schedule_advertising_restart();
		return;
	}

	k_mutex_lock(&connection_mutex, K_FOREVER);
	atomic_clear(&advertising);
	k_work_cancel_delayable(&advertising_timeout_work);

	/* A deactivation may race with the controller completing a connection. */
	if (!atomic_get(&bluetooth_active)) {
		k_mutex_unlock(&connection_mutex);
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		return;
	}

	current_connection = bt_conn_ref(conn);
	led_set_bluetooth_state(LED_BLUETOOTH_CONNECTED);
	k_mutex_unlock(&connection_mutex);
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

	led_set_bluetooth_state(atomic_get(&bluetooth_active) ?
				LED_BLUETOOTH_ADVERTISING : LED_BLUETOOTH_OFF);

	/* Let the controller finish disconnecting before advertising again. */
	schedule_advertising_restart();
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

	motion_packet[0] = BLUETOOTH_MOTION_PACKET_VERSION;
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
	k_work_cancel_delayable(&advertising_restart_work);
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


int bluetooth_publish(const struct bluetooth_motion_sample *sample)
{
	int ret = 0;

	if (sample == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&motion_packet_mutex, K_FOREVER);

	motion_packet[0] = BLUETOOTH_MOTION_PACKET_VERSION;
	motion_packet[1] = sample->flags;
	sys_put_le16(motion_sequence++, &motion_packet[2]);
	sys_put_le32(sample->timestamp_ms, &motion_packet[4]);

	sys_put_le16((uint16_t)sample->accel_x_mg, &motion_packet[8]);
	sys_put_le16((uint16_t)sample->accel_y_mg, &motion_packet[10]);
	sys_put_le16((uint16_t)sample->accel_z_mg, &motion_packet[12]);

	sys_put_le16((uint16_t)sample->gyro_x_dps_tenths,
		     &motion_packet[14]);
	sys_put_le16((uint16_t)sample->gyro_y_dps_tenths,
		     &motion_packet[16]);
	sys_put_le16((uint16_t)sample->gyro_z_dps_tenths,
		     &motion_packet[18]);

	if (notifications_enabled) {
		ret = bt_gatt_notify(NULL, &motion_service.attrs[2],
				     motion_packet, sizeof(motion_packet));
	}

	k_mutex_unlock(&motion_packet_mutex);
	return ret;
}
