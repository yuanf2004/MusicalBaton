#ifndef MUSICAL_BATON_CONDUCTING_H
#define MUSICAL_BATON_CONDUCTING_H

#include <stdbool.h>
#include "sensor.h"

/* Experimental ictus candidates, not a trained conducting/meter classifier.
 * All times use the same wrapping uptime clock as motion_sample.
 */
struct conducting_beat {
	uint32_t timestamp_ms;
	uint32_t detected_ms;
	uint16_t bpm_tenths; /* Zero until three consistent intervals exist. */
	uint8_t strength; /* Heuristic 0..100 score, NOT a probability. */
};

struct conducting_detector {
	uint8_t sensitivity;
	bool started, calibrated, armed, have_beat;
	uint32_t previous_ms, calibration_ms, stroke_ms, last_beat_ms;
	float gravity[3], bias[3], calibration_accel[3];
	float speed, peak, accel_peak;
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
