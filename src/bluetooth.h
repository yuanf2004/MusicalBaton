#ifndef MUSICAL_BATON_BLUETOOTH_H
#define MUSICAL_BATON_BLUETOOTH_H

#include <stdbool.h>
#include <stdint.h>

/* Initialize the Bluetooth stack without advertising. */
int bluetooth_init(void);

/* Start or stop Bluetooth availability. */
int bluetooth_activate(void);
int bluetooth_deactivate(void);
bool bluetooth_is_active(void);

/* Update the readable packet and notify subscribers, if any.
 * Values are in mg. Call after bluetooth_init(); returns 0 or an error.
 */
int bluetooth_publish(int16_t x_mg, int16_t y_mg, int16_t z_mg);

#endif
