#include "practice.h"
#include "sensor.h"
#include <zephyr/kernel.h>

/* Commands are idempotent; each active write renews a five-second lease.
 * Only the main sampling thread changes sensor configuration. */
K_MUTEX_DEFINE(practice_mutex);
static bool desired;
static uint16_t command_sequence, applied_sequence;
static uint8_t state; /* 0 idle, 1 active, 2 transitioning, 3 sensor failure, 4 calibrating */
static uint8_t request_sequence;
static int64_t renewed;
static bool sensor_active;
static int sensor_mode = -1; /* unknown, then 0 off / 1 idle / 2 practice */
static bool configured;

void practice_command(bool active, uint16_t sequence)
{
    k_mutex_lock(&practice_mutex, K_FOREVER);
    desired = active;
    command_sequence = sequence;
    renewed = k_uptime_get();
    k_mutex_unlock(&practice_mutex);
}
void practice_disconnect(void)
{
    k_mutex_lock(&practice_mutex, K_FOREVER);
    desired = false;
    k_mutex_unlock(&practice_mutex);
}
void practice_request_toggle(void)
{
    k_mutex_lock(&practice_mutex, K_FOREVER);
    request_sequence++;
    k_mutex_unlock(&practice_mutex);
}
void practice_snapshot(uint8_t packet[6])
{
    k_mutex_lock(&practice_mutex, K_FOREVER);
    packet[0] = 2; packet[1] = state; packet[2] = request_sequence; packet[3] = 0;
    packet[4] = applied_sequence & 0xff; packet[5] = applied_sequence >> 8;
    k_mutex_unlock(&practice_mutex);
}
bool practice_update(bool connected)
{
    k_mutex_lock(&practice_mutex, K_FOREVER);
    if (!connected) { desired = false; }
    if (desired && k_uptime_get() - renewed >= 5000) { desired = false; }
    bool target = desired;
    uint16_t sequence = command_sequence;
    int target_mode = !connected ? 0 : (target ? 2 : 1);
    bool change = !configured || target_mode != sensor_mode;
    if (change) { state = 2; }
    k_mutex_unlock(&practice_mutex);
    if (change) {
        int err;
        if (target_mode == 0) {
            err = sensor_power_down();
        } else {
            err = sensor_init();
            if (err == 0) { err = sensor_set_practicing(target); }
        }
        k_mutex_lock(&practice_mutex, K_FOREVER);
        configured = err == 0;
        if (err == 0) { sensor_mode = target_mode; sensor_active = target; state = target ? 4 : 0; }
        else { desired = false; state = 3; }
        k_mutex_unlock(&practice_mutex);
    }
    k_mutex_lock(&practice_mutex, K_FOREVER);
    if (configured && desired == target && command_sequence == sequence) {
        applied_sequence = sequence;
    }
    bool active = connected && configured && sensor_active && desired;
    k_mutex_unlock(&practice_mutex);
    return active;
}

void practice_set_calibrated(bool calibrated)
{
    k_mutex_lock(&practice_mutex, K_FOREVER);
    if (configured && sensor_active && desired) { state = calibrated ? 1 : 4; }
    k_mutex_unlock(&practice_mutex);
}
