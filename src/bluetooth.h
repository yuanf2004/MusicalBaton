#ifndef MUSICAL_BATON_BLUETOOTH_H
#define MUSICAL_BATON_BLUETOOTH_H

#include <stdbool.h>
#include <stdint.h>

#define BLUETOOTH_MOTION_PACKET_VERSION 1U

#define BLUETOOTH_MOTION_FLAG_ACCEL_VALID (1U << 0)
#define BLUETOOTH_MOTION_FLAG_GYRO_VALID  (1U << 1)
#define BLUETOOTH_MOTION_FLAG_TIME_SYNCED (1U << 2)

struct bluetooth_motion_sample {
	uint32_t timestamp_ms;
	uint8_t flags;

	int16_t accel_x_mg;
	int16_t accel_y_mg;
	int16_t accel_z_mg;

	/* Gyroscope units are tenths of a degree per second. */
	int16_t gyro_x_dps_tenths;
	int16_t gyro_y_dps_tenths;
	int16_t gyro_z_dps_tenths;
};

int bluetooth_init(void);
int bluetooth_activate(void);
int bluetooth_deactivate(void);
bool bluetooth_is_active(void);
/* Requested sensitivity, set via versioned ...def3 read/write characteristic. */
uint8_t bluetooth_conducting_sensitivity(void);

/*
 * Update and optionally notify the fixed 20-byte motion packet.
 * Sequence numbers are assigned by the Bluetooth module.
 */
int bluetooth_publish(const struct bluetooth_motion_sample *sample);

/* Notify a fresh beat only; no retained/readable event and no retries.
 * 12 bytes: version=1, type=1, sequence LE16, uptime LE32, BPM*10 LE16,
 * strength (0..100), reserved=0. UUID ends in def2, under the motion service.
 */
int bluetooth_publish_beat(uint32_t timestamp_ms, uint16_t bpm_tenths,
			   uint8_t strength);

#endif
