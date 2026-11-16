#include <assert.h>
#include <stdio.h>
#include "../src/practice.h"
int64_t test_now;
static int calls, fail;
static bool sensor_active;
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
    assert(!practice_update() && !sensor_active); expect(0, 0);
    practice_command(true, 1);
    expect(0, 0); /* write receipt must not pretend the sensor is ready */
    assert(practice_update() && sensor_active); expect(4, 1);
    practice_set_calibrated(false); expect(4, 1);
    practice_set_calibrated(true); expect(1, 1);
    int prior = calls;
    test_now = 4000; practice_command(true, 1);
    assert(practice_update() && calls == prior); /* heartbeat never resets sensor */
    test_now = 8999; assert(practice_update());
    test_now = 9000; assert(!practice_update() && !sensor_active); expect(0, 1);
    practice_command(true, 2); practice_command(false, 3);
    assert(!practice_update()); expect(0, 3); /* stop supersedes queued start */
    practice_command(true, 4); assert(practice_update());
    practice_disconnect(); assert(!practice_update()); expect(0, 4);
    uint8_t p[6];
    for (int i = 0; i < 257; i++) practice_request_toggle();
    practice_snapshot(p); assert(p[2] == 1); /* sequence wraps, no sensor start */
    fail = 1; practice_command(true, 5); assert(!practice_update());
    practice_snapshot(p); assert(p[1] == 3);
    fail = 0; assert(!practice_update() && !sensor_active); expect(0, 5);
    practice_command(true, 65535); assert(practice_update()); expect(4, 65535);
    practice_set_calibrated(true); expect(1, 65535);
    practice_command(false, 0); assert(!practice_update()); expect(0, 0);
    puts("Practice idle/start/stop, applied acknowledgments, lease expiry, heartbeat, disconnect, failure and wrap checks passed");
}
