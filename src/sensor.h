#ifndef MUSICAL_BATON_SENSOR_H
#define MUSICAL_BATON_SENSOR_H

#include <stdint.h>

struct motion_sample {
	/* Local uptime captured immediately after a successful sensor fetch. */
	uint32_t timestamp_ms;
	int16_t accel_x_mg;
	int16_t accel_y_mg;
	int16_t accel_z_mg;

	/* Angular velocity in tenths of a degree per second. */
	int16_t gyro_x_dps_tenths;
	int16_t gyro_y_dps_tenths;
	int16_t gyro_z_dps_tenths;
};

/* Wait for power-up, initialize the LSM6DSOX driver, and allow filters to settle.
 * Measurement ranges and rates are configured by the baton-imu DT alias.
 * Call from thread context. Returns 0 or a negative error.
 */
int sensor_init(void);

/* Fetch all six axes and timestamp acquisition. Call from the main thread.
 * Returns 0 or a negative error; leaves the sample unchanged on failure.
 */
int sensor_read(struct motion_sample *sample);

#endif
