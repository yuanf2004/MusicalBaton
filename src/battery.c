#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/atomic.h>
#include "battery.h"
#include "led.h"

#if DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
static const struct adc_dt_spec sense = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
static atomic_t level = ATOMIC_INIT(-1);
static ssize_t read_level(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                         void *buf, uint16_t len, uint16_t offset)
{
    int value = atomic_get(&level);
    if (value < 0) { return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY); }
    uint8_t percentage = value;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &percentage, 1);
}
/* Read-only standard Battery Service; the app refreshes every 15 seconds. */
BT_GATT_SERVICE_DEFINE(battery_service,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_BAS),
    BT_GATT_CHARACTERISTIC(BT_UUID_BAS_BATTERY_LEVEL, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, read_level, NULL, NULL));

/* Approximate resting single-cell LiPo curve, NOT a calibrated fuel gauge.
 * Charging and load change voltage; validate against the assembled battery. */
static int estimate(int mv)
{
    static const int voltage[] = {3000, 3500, 3650, 3700, 3750, 3800,
                                  3850, 3900, 4000, 4100, 4200};
    if (mv <= voltage[0]) { return 0; }
    for (int i = 1; i < ARRAY_SIZE(voltage); ++i) {
        if (mv < voltage[i]) {
            return (i - 1) * 10 + (mv - voltage[i - 1]) * 10 /
                   (voltage[i] - voltage[i - 1]);
        }
    }
    return 100;
}
void battery_poll(void)
{
    static int64_t next;
    static bool configured;
    static int filtered;
    int64_t now = k_uptime_get();
    if (now < next) { return; }
    next = now + 10000;
    int err = 0;
    if (!configured) {
        if (!adc_is_ready_dt(&sense)) { err = -ENODEV; }
        else { err = adc_channel_setup_dt(&sense); }
        configured = err == 0;
    }
    int16_t raw = 0;
    struct adc_sequence sequence = { .buffer = &raw, .buffer_size = sizeof(raw) };
    if (!err) { err = adc_sequence_init_dt(&sense, &sequence); }
    if (!err) { err = adc_read_dt(&sense, &sequence); }
    int32_t mv = raw;
    if (!err) { err = adc_raw_to_millivolts_dt(&sense, &mv); }
    mv *= 2; /* Equal 1 Mohm resistors: ADC reads half the battery voltage. */
    if (err || mv < 2500 || mv > 4350) {
        atomic_set(&level, -1);
        filtered = 0;
        led_set_battery_state(LED_BATTERY_OFF);
        return;
    }
    filtered = filtered ? (filtered * 3 + mv) / 4 : mv;
    int percentage = estimate(filtered);
    atomic_set(&level, percentage);
    led_set_battery_level(percentage, false);
}
#else
/* DK has no battery divider; do not advertise a fictitious battery level. */
void battery_poll(void) {}
#endif
