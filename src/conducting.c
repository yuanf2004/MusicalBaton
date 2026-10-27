#include "conducting.h"

#include <math.h>
#include <string.h>

/* Physical units: mg, degrees/second, milliseconds. Tune on labeled recordings. */
static const struct {
	float min_stroke_dps, min_accel_mg, braking_fraction;
} sensitivity_settings[] = {
	{30.0f, 80.0f, 0.55f}, /* Balanced: existing default. */
	{40.0f, 120.0f, 0.45f}, /* Deliberate: firmer strokes. */
	{22.0f, 55.0f, 0.60f}, /* Gentle: smaller strokes; more false-trigger risk. */
};
#define SPEED_SMOOTHING_MS 15.0f
#define STOP_SPEED_DPS 30.0f
#define MIN_STROKE_MS 100U
#define MIN_BEAT_MS 250U
#define MAX_BEAT_MS 2000U
#define MAX_GAP_MS 250U

static float norm(const float v[3])
{
	return sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

void conducting_reset(struct conducting_detector *d)
{
	memset(d, 0, sizeof(*d));
}

static void clear_stroke(struct conducting_detector *d)
{
	d->armed = false;
	d->peak = d->accel_peak = 0.0f;
}

void conducting_set_sensitivity(struct conducting_detector *d, uint8_t id)
{
	if (id > 2 || id == d->sensitivity) {
		return;
	}
	d->sensitivity = id;
	clear_stroke(d);
	d->speed = 0.0f;
	d->have_beat = false;
	d->interval_count = 0;
}

static uint16_t tempo(struct conducting_detector *d, uint32_t t)
{
	if (d->have_beat) {
		uint32_t interval = t - d->last_beat_ms;
		if (interval >= MIN_BEAT_MS && interval <= MAX_BEAT_MS) {
			if (d->interval_count == 5) {
				memmove(d->intervals, d->intervals + 1, 4 * sizeof(uint16_t));
				d->interval_count--;
			}
			d->intervals[d->interval_count++] = (uint16_t)interval;
		} else {
			d->interval_count = 0;
		}
	}
	d->have_beat = true;
	d->last_beat_ms = t;
	if (d->interval_count < 3) {
		return 0;
	}
	uint16_t sorted[5];
	memcpy(sorted, d->intervals, d->interval_count * sizeof(uint16_t));
	for (unsigned i = 1; i < d->interval_count; i++) {
		for (unsigned j = i; j > 0 && sorted[j] < sorted[j-1]; j--) {
			uint16_t swap = sorted[j];
			sorted[j] = sorted[j-1];
			sorted[j-1] = swap;
		}
	}
	uint16_t median = sorted[d->interval_count / 2];
	/* Suppress misleading BPM when intervals disagree by more than 20%. */
	if (sorted[0] < median * 0.8f || sorted[d->interval_count-1] > median * 1.2f) {
		return 0;
	}
	return (uint16_t)(600000U / median);
}

bool conducting_process(struct conducting_detector *d,
			const struct motion_sample *s, struct conducting_beat *beat)
{
	float a[3] = {s->accel_x_mg, s->accel_y_mg, s->accel_z_mg};
	float w[3] = {s->gyro_x_dps_tenths * 0.1f,
		      s->gyro_y_dps_tenths * 0.1f, s->gyro_z_dps_tenths * 0.1f};
	uint32_t dt_ms = s->timestamp_ms - d->previous_ms;
	if (d->started && (dt_ms == 0 || dt_ms > INT32_MAX)) {
		return false; /* Duplicate or out-of-order; normal uptime wrap is valid. */
	}
	if (!d->started || dt_ms > MAX_GAP_MS) {
		uint8_t sensitivity = d->sensitivity;
		conducting_reset(d);
		d->sensitivity = sensitivity;
		d->started = true;
		d->previous_ms = s->timestamp_ms;
		memcpy(d->calibration_accel, a, sizeof(a));
		return false;
	}
	d->previous_ms = s->timestamp_ms;
	float an = norm(a);
	if (!d->calibrated) {
		float change[3];
		for (unsigned i = 0; i < 3; i++) {
			change[i] = a[i] - d->calibration_accel[i];
		}
		if (fabsf(an - 1000.0f) > 80.0f || norm(w) > 5.0f || norm(change) > 60.0f) {
			d->calibration_ms = 0;
			memset(d->bias, 0, sizeof(d->bias));
			memcpy(d->calibration_accel, a, sizeof(a));
			return false;
		}
		float weight = (float)dt_ms / (d->calibration_ms + dt_ms);
		for (unsigned i = 0; i < 3; i++) {
			d->bias[i] += weight * (w[i] - d->bias[i]);
			d->gravity[i] = a[i] * (1000.0f / an);
		}
		d->calibration_ms += dt_ms;
		d->calibrated = d->calibration_ms >= 1000;
		return false;
	}
	for (unsigned i = 0; i < 3; i++) {
		w[i] -= d->bias[i];
	}
	/* Rotate gravity into the new body frame with -omega (Rodrigues).
	 * Accelerometer correction is deliberately weak and gated during motion.
	 */
	float wn = norm(w);
	float angle = wn * 0.01745329252f * dt_ms * 0.001f;
	if (wn > 0.001f) {
		float u[3] = {w[0]/wn, w[1]/wn, w[2]/wn};
		float *g = d->gravity;
		float cross[3] = {u[1]*g[2]-u[2]*g[1], u[2]*g[0]-u[0]*g[2],
				  u[0]*g[1]-u[1]*g[0]};
		float dot = u[0]*g[0]+u[1]*g[1]+u[2]*g[2];
		float c = cosf(angle), sn = sinf(angle);
		for (unsigned i = 0; i < 3; i++) {
			g[i] = c*g[i] - sn*cross[i] + (1.0f-c)*u[i]*dot;
		}
	}
	float linear[3];
	for (unsigned i = 0; i < 3; i++) {
		linear[i] = a[i] - d->gravity[i];
	}
	float acceleration = norm(linear);
	if (fabsf(an - 1000.0f) < 80.0f && acceleration < 150.0f && wn < 20.0f) {
		float alpha = (float)dt_ms / (2000.0f + dt_ms);
		for (unsigned i = 0; i < 3; i++) {
			d->gravity[i] += alpha * (a[i] * (1000.0f/an) - d->gravity[i]);
		}
		float gn = norm(d->gravity);
		for (unsigned i = 0; i < 3; i++) {
			d->gravity[i] *= 1000.0f / gn;
		}
	}
	float previous_speed = d->speed;
	d->speed += ((float)dt_ms / (SPEED_SMOOTHING_MS + dt_ms)) * (wn - d->speed);
	if (d->have_beat && s->timestamp_ms - d->last_beat_ms > MAX_BEAT_MS) {
		d->have_beat = false;
		d->interval_count = 0;
	}
	if (d->armed && s->timestamp_ms - d->stroke_ms > 1500) {
		clear_stroke(d);
		return false;
	}
	if (!d->armed) {
		/* A fresh rise is required: continued braking cannot re-arm a beat. */
		if (d->speed < sensitivity_settings[d->sensitivity].min_stroke_dps || d->speed <= previous_speed ||
		    (d->have_beat && s->timestamp_ms - d->last_beat_ms < MIN_BEAT_MS)) {
			return false;
		}
		d->armed = true;
		d->stroke_ms = s->timestamp_ms;
	}
	d->peak = fmaxf(d->peak, d->speed);
	d->accel_peak = fmaxf(d->accel_peak, acceleration);
	/* Detect braking toward a stop, without waiting for a rebound. This is
	 * angular-speed evidence, not proof of zero translational velocity.
	 */
	if (d->speed >= d->peak * sensitivity_settings[d->sensitivity].braking_fraction || d->speed > STOP_SPEED_DPS) {
		return false;
	}
	bool accepted = d->accel_peak >= sensitivity_settings[d->sensitivity].min_accel_mg &&
		s->timestamp_ms - d->stroke_ms >= MIN_STROKE_MS &&
		(!d->have_beat || s->timestamp_ms - d->last_beat_ms >= MIN_BEAT_MS);
	if (accepted) {
		beat->timestamp_ms = s->timestamp_ms;
		beat->detected_ms = s->timestamp_ms;
		beat->bpm_tenths = tempo(d, beat->timestamp_ms);
		beat->strength = (uint8_t)fminf(100.0f,
			50.0f * (1.0f - d->speed/d->peak) +
			50.0f * fminf(1.0f, d->accel_peak / 500.0f));
	}
	clear_stroke(d);
	return accepted;
}
