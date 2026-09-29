#ifndef MUSICAL_BATON_BLUETOOTH_H
#define MUSICAL_BATON_BLUETOOTH_H

#include <stdint.h>

/* Enable BLE and advertise. Returns 0 or a negative error. */
int bluetooth_start(void);

/* Update the readable packet and notify subscribers, if any.
 * Values are in mg. Call after bluetooth_start(); returns 0 or an error.
 */
int bluetooth_publish(int16_t x_mg, int16_t y_mg, int16_t z_mg);

#endif
