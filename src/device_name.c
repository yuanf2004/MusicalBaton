#include "device_name.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/printk.h>

BUILD_ASSERT(CONFIG_BT_DEVICE_NAME_MAX == BATON_NAME_MAX_BYTES);
BUILD_ASSERT(sizeof(CONFIG_BT_DEVICE_NAME) <= BATON_NAME_MAX_BYTES + 1);

static char saved_name[BATON_NAME_MAX_BYTES + 1] = CONFIG_BT_DEVICE_NAME;
static char pending_name[BATON_NAME_MAX_BYTES + 1];
static enum baton_name_status status;
static K_MUTEX_DEFINE(name_mutex);
static struct k_work_q name_queue;
static K_THREAD_STACK_DEFINE(name_stack, 2048);

static int load_name(const char *key, size_t len, settings_read_cb read_cb, void *arg)
{
	if (key == NULL || strcmp(key, "name") != 0) { return -ENOENT; }
	uint8_t value[BATON_NAME_MAX_BYTES];
	if (len == 0 || len > sizeof(value)) {
		printk("Ignoring invalid stored baton name\n");
		return 0;
	}
	ssize_t count = read_cb(arg, value, len);
	if (count < 0) { return (int)count; }
	if ((size_t)count != len || !baton_name_valid(value, len)) {
		printk("Ignoring invalid stored baton name\n");
		return 0;
	}
	memcpy(saved_name, value, len);
	saved_name[len] = '\0';
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(baton_name, "baton", NULL, load_name, NULL, NULL);

static void save_name(struct k_work *work)
{
	ARG_UNUSED(work);
	char candidate[BATON_NAME_MAX_BYTES + 1];
	k_mutex_lock(&name_mutex, K_FOREVER);
	memcpy(candidate, pending_name, sizeof(candidate));
	k_mutex_unlock(&name_mutex);

	/* Flash I/O runs on a dedicated queue, never in the BLE RX callback or
	 * the sampling loop. Keep reporting SAVING until persistence succeeds.
	 * BT_SETTINGS is deliberately off: this application owns baton/name.
	 */
	int ret = settings_save_one("baton/name", candidate, strlen(candidate));
	if (ret == 0) { ret = bt_set_name(candidate); }
	k_mutex_lock(&name_mutex, K_FOREVER);
	if (ret == 0) {
		memcpy(saved_name, candidate, sizeof(saved_name));
		status = BATON_NAME_SAVED;
	} else {
		status = BATON_NAME_FAILED;
	}
	k_mutex_unlock(&name_mutex);
	printk("Baton name save result: %d\n", ret);
}

static K_WORK_DEFINE(save_name_work, save_name);

int baton_device_name_init(void)
{
	int ret = settings_subsys_init();
	if (ret != 0) { return ret; }
	ret = settings_load_subtree("baton");
	if (ret != 0) { return ret; }
	ret = bt_set_name(saved_name);
	if (ret != 0) { return ret; }
	status = BATON_NAME_SAVED;
	k_work_queue_start(&name_queue, name_stack, K_THREAD_STACK_SIZEOF(name_stack),
			   7, NULL);
	return 0;
}

int baton_device_name_request(const uint8_t *name, size_t len)
{
	if (!baton_name_valid(name, len)) { return -EINVAL; }
	k_mutex_lock(&name_mutex, K_FOREVER);
	if (status == BATON_NAME_SAVING) {
		k_mutex_unlock(&name_mutex);
		return -EBUSY;
	}
	if (strlen(saved_name) == len && memcmp(saved_name, name, len) == 0) {
		status = BATON_NAME_SAVED;
		k_mutex_unlock(&name_mutex);
		return 0;
	}
	memcpy(pending_name, name, len);
	pending_name[len] = '\0';
	status = BATON_NAME_SAVING;
	k_mutex_unlock(&name_mutex);
	int ret = k_work_submit_to_queue(&name_queue, &save_name_work);
	if (ret < 0) {
		k_mutex_lock(&name_mutex, K_FOREVER);
		status = BATON_NAME_FAILED;
		k_mutex_unlock(&name_mutex);
		return ret;
	}
	return 0;
}

size_t baton_device_name_snapshot(uint8_t value[BATON_NAME_VALUE_MAX])
{
	k_mutex_lock(&name_mutex, K_FOREVER);
	size_t len = strlen(saved_name);
	value[0] = BATON_NAME_VERSION;
	value[1] = (uint8_t)status;
	memcpy(value + 2, saved_name, len);
	k_mutex_unlock(&name_mutex);
	return len + 2;
}

void baton_device_name_copy(char name[BATON_NAME_MAX_BYTES + 1])
{
	k_mutex_lock(&name_mutex, K_FOREVER);
	memcpy(name, saved_name, sizeof(saved_name));
	k_mutex_unlock(&name_mutex);
}
