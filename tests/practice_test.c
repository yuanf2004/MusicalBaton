#include <assert.h>
#include <stdio.h>
#include "../src/practice.h"
int64_t test_now;
static int calls, fail, init_fail, init_calls, power_down_calls;
static bool sensor_active;
int sensor_init(void) { init_calls++; return init_fail ? -1 : 0; }
int sensor_power_down(void) {
    power_down_calls++;
    if (fail) return -1;
    sensor_active = false;
    return 0;
}
int sensor_set_practicing(bool active) {
    calls++;
    if (fail) return -1;
    sensor_active = active;
    return 0;
}
static void expect(uint8_t state, uint16_t sequence) {
    uint8_t p[6]; practice_snapshot(p);
    assert(p[0] == 2 && p[1] == state && p[3] == 0);
    assert((p[4] | (p[5] << 8)) == sequence);
}
int main(void) {
    assert(!practice_update(false) && !sensor_active); expect(0, 0);
    assert(init_calls == 0 && calls == 0 && power_down_calls == 1);
    assert(!practice_update(false) && power_down_calls == 1);
    assert(!practice_update(true) && !sensor_active); expect(0, 0);
    assert(init_calls == 1 && calls == 1);
    practice_command(true, 1);
    expect(0, 0); /* write receipt must not pretend the sensor is ready */
    assert(practice_update(true) && sensor_active); expect(4, 1);
    practice_set_calibrated(false); expect(4, 1);
    practice_set_calibrated(true); expect(1, 1);
    int prior = calls;
    test_now = 4000; practice_command(true, 1);
    assert(practice_update(true) && calls == prior); /* heartbeat never resets sensor */
    test_now = 8999; assert(practice_update(true));
    test_now = 9000; assert(!practice_update(true) && !sensor_active); expect(0, 1);
    practice_command(true, 2); practice_command(false, 3);
    assert(!practice_update(true)); expect(0, 3); /* stop supersedes queued start */
    practice_command(true, 4); assert(practice_update(true));
    practice_disconnect(); assert(!practice_update(true)); expect(0, 4);
    uint8_t p[6];
    for (int i = 0; i < 257; i++) practice_request_toggle();
    practice_snapshot(p); assert(p[2] == 1); /* sequence wraps, no sensor start */
    fail = 1; practice_command(true, 5); assert(!practice_update(true));
    practice_snapshot(p); assert(p[1] == 3);
    fail = 0; assert(!practice_update(true) && !sensor_active); expect(0, 5);
    practice_command(true, 65535); assert(practice_update(true)); expect(4, 65535);
    practice_set_calibrated(true); expect(1, 65535);
    practice_command(false, 0); assert(!practice_update(true)); expect(0, 0);
    practice_command(true, 9); assert(practice_update(true));
    int before_disconnect = init_calls;
    practice_disconnect(); assert(!practice_update(false) && !sensor_active);
    assert(power_down_calls == 2 && init_calls == before_disconnect);
    assert(!practice_update(false) && power_down_calls == 2);
    assert(!practice_update(true)); /* reconnect is idle, never resumes */
    expect(0, 9);
    practice_command(true, 10); assert(practice_update(true)); expect(4, 10);
    fail = 1; practice_disconnect(); assert(!practice_update(false));
    uint8_t failure[6]; practice_snapshot(failure); assert(failure[1] == 3);
    fail = 0; assert(!practice_update(false) && !sensor_active);
    int before_init_failure = calls;
    init_fail = 1; assert(!practice_update(true));
    practice_snapshot(failure); assert(failure[1] == 3);
    assert(calls == before_init_failure); /* no ODR configuration before init succeeds */
    init_fail = 0; assert(!practice_update(true)); expect(0, 10);
    practice_command(true, 11); assert(practice_update(true)); expect(4, 11);
    puts("Connection-gated IMU initialization, init failure, power-down retry and reconnect checks passed");
    puts("Practice idle/start/stop, applied acknowledgments, lease expiry, heartbeat, disconnect, failure and wrap checks passed");
}
