/* Compile the production persistence worker against deterministic host fakes.
 * These check state/commit semantics, not the nRF flash driver or thread safety.
 */
#include "device_name.h"
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

extern test_setting_setter test_name_setter;
static struct k_work *queued;
static char flash_name[21] = "Stored baton";
static char gap_name[21];
static int save_error, submit_error, writes;
static ssize_t read_flash(void *arg, void *buf, size_t size)
{ (void)arg; memcpy(buf, flash_name, size); return (ssize_t)size; }
int settings_subsys_init(void) { return 0; }
int settings_load_subtree(const char *path)
{
    assert(strcmp(path, "baton") == 0);
    assert(test_name_setter(NULL, 0, read_flash, NULL) == -ENOENT);
    return test_name_setter("name", strlen(flash_name), read_flash, NULL);
}
int settings_save_one(const char *key, const void *value, size_t len)
{
    assert(strcmp(key, "baton/name") == 0);
    writes++;
    if (save_error) { return save_error; }
    memcpy(flash_name, value, len); flash_name[len] = 0; return 0;
}
int bt_set_name(const char *name) { strcpy(gap_name, name); return 0; }
int k_work_submit_to_queue(struct k_work_q *q, struct k_work *w)
{ (void)q; if (submit_error) { return submit_error; } queued = w; return 1; }
static void run_worker(void)
{ assert(queued); struct k_work *work = queued; queued = NULL; work->handler(work); }
static void expect(enum baton_name_status status, const char *name)
{
    uint8_t value[BATON_NAME_VALUE_MAX];
    size_t len = baton_device_name_snapshot(value);
    assert(value[0] == 1 && value[1] == status && len == strlen(name)+2);
    assert(memcmp(value+2, name, len-2) == 0);
    char copied[21]; baton_device_name_copy(copied); assert(strcmp(copied, name) == 0);
}
int main(void)
{
    assert(baton_device_name_init() == 0);
    expect(BATON_NAME_SAVED, "Stored baton");
    assert(strcmp(gap_name, "Stored baton") == 0);
    assert(baton_device_name_request((const uint8_t *)"New baton", 9) == 0);
    expect(BATON_NAME_SAVING, "Stored baton");
    assert(writes == 0 && strcmp(flash_name, "Stored baton") == 0);
    assert(baton_device_name_request((const uint8_t *)"Other", 5) == -EBUSY);
    run_worker();
    expect(BATON_NAME_SAVED, "New baton");
    assert(strcmp(flash_name, "New baton") == 0 && strcmp(gap_name, "New baton") == 0);
    assert(baton_device_name_request((const uint8_t *)"New baton", 9) == 0 && writes == 1);
    assert(baton_device_name_request((const uint8_t *)"Failed", 6) == 0);
    save_error = -EIO; run_worker();
    expect(BATON_NAME_FAILED, "New baton");
    assert(strcmp(flash_name, "New baton") == 0 && strcmp(gap_name, "New baton") == 0);
    save_error = 0;
    assert(baton_device_name_request((const uint8_t *)"Retry", 5) == 0);
    run_worker(); expect(BATON_NAME_SAVED, "Retry");
    submit_error = -EIO;
    assert(baton_device_name_request((const uint8_t *)"No queue", 8) == -EIO);
    expect(BATON_NAME_FAILED, "Retry");
    assert(baton_device_name_request((const uint8_t *)"bad\n", 4) == -EINVAL);
    strcpy(flash_name, "Restored");
    assert(baton_device_name_init() == 0); expect(BATON_NAME_SAVED, "Restored");
    puts("Name restore, deferred commit, busy rejection, save/queue failures, retry and readback passed");
}
