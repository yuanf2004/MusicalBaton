#ifndef MUSICAL_BATON_SENSOR_H
#define MUSICAL_BATON_SENSOR_H

#include <stdint.h>

struct acceleration {
	int16_t x_mg;
	int16_t y_mg;
	int16_t z_mg;
};

/* Verify the MMA8451 and enable measurement. Returns 0 or a negative error. */
int sensor_init(void);

/* Read acceleration in mg. Call after sensor_init(); returns 0 or an error. */
int sensor_read(struct acceleration *sample);

#endif
