#ifndef MUSICAL_BATON_CONDUCTING_H
#define MUSICAL_BATON_CONDUCTING_H

#include <stdbool.h>
#include "sensor.h"

/* Experimental ictus candidates, not a trained conducting/meter classifier.
 * All times use the same wrapping uptime clock as motion_sample.
 */
struct conducting_beat {
	uint32_t timestamp_ms; /* Angular-speed peak or straight-stroke braking peak. */
	uint32_t detected_ms; /* Sample that confirmed the peak. */
	uint16_t bpm_tenths; /* Zero until three consistent intervals exist. */
	uint8_t strength; /* Heuristic 0..100 score, NOT a probability. */
};

/* Acceleration fallback: drive, opposite braking, then quiet rearm. */
struct straight_stroke {
	unsigned phase;
	uint32_t start_ms, brake_ms, peak_ms, quiet_ms, drive_ms;
	float filtered[3], direction[3];
	float drive_impulse, brake_impulse, brake_peak;
};

struct conducting_detector {
	uint8_t sensitivity;
	bool started, calibrated, armed, have_beat;
	uint32_t previous_ms, calibration_ms, stroke_ms, peak_ms, last_beat_ms;
	uint32_t last_emitted_ms;
	struct straight_stroke straight;
	float gravity[3], bias[3], calibration_accel[3];
	float speed, peak, accel_peak, release_dps;
	uint16_t intervals[5];
	unsigned interval_count;
};

void conducting_reset(struct conducting_detector *detector);
/* 0 balanced, 1 deliberate, 2 gentle. Preserves sensor calibration, clears
 * stroke/tempo history. Invalid IDs are ignored. Call on the sampling thread.
 */
void conducting_set_sensitivity(struct conducting_detector *detector, uint8_t id);
/* Single-threaded; accepts successful six-axis samples only. Output unchanged
 * unless true is returned. Hold still for one second after reset or >250 ms gap.
 */
bool conducting_process(struct conducting_detector *detector,
			const struct motion_sample *sample,
			struct conducting_beat *beat);

#endif
