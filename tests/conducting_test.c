#include "conducting.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static struct motion_sample sample(uint32_t t, float gyro, int accel)
{
	return (struct motion_sample){.timestamp_ms=t, .accel_x_mg=accel,
		.accel_z_mg=1000, .gyro_z_dps_tenths=(int16_t)(gyro*10)};
}

static void calibrate(struct conducting_detector *d, uint32_t start)
{
	conducting_reset(d);
	struct conducting_beat b;
	for (unsigned t=0; t<=1100; t+=50) {
		struct motion_sample s=sample(start+t, 2, 0);
		assert(!conducting_process(d, &s, &b));
	}
	assert(d->calibrated);
	assert(fabsf(d->bias[2]-2) < 0.01f);
}

static void periodic(unsigned step, uint32_t start, bool accel, bool gyro)
{
	struct conducting_detector d;
	calibrate(&d, start);
	unsigned count=0;
	uint32_t last=0;
	struct conducting_beat b={0};
	for (unsigned t=1150; t<7150; t+=step) {
		float phase=(t-1150)*3.14159265f/600.0f;
		struct motion_sample s=sample(start+t, 2+(gyro ? 120*sinf(phase) : 0),
			accel ? (int)(350*cosf(phase)) : 0);
		if (conducting_process(&d, &s, &b)) {
			assert(accel && gyro);
			assert(b.detected_ms-b.timestamp_ms <= 250);
			/* |sin| peaks 300 ms into each 600 ms half-cycle. */
			unsigned phase_ms=(b.timestamp_ms-start-1150)%600;
			assert(phase_ms>=260 && phase_ms<=360);
			if (count) assert(b.timestamp_ms-last >= 250);
			last=b.timestamp_ms;
			count++;
		}
	}
	if (accel && gyro) {
		assert(count >= 8 && count <= 10);
		assert(b.bpm_tenths >= 950 && b.bpm_tenths <= 1050);
	} else {
		assert(count == 0);
	}
	/* The final stroke may end in one beat, but rest must never repeat it. */
	unsigned stop_count=0;
	for (unsigned t=7200; t<10200; t+=50) {
		struct motion_sample s=sample(start+t, 2, 0);
		if (conducting_process(&d, &s, &b)) {
			assert(accel && gyro && t <= 7350);
			stop_count++;
		}
	}
	assert(stop_count <= 1);
	assert(!d.have_beat && d.interval_count == 0);
}

static void gentle_strokes(void)
{
	struct conducting_detector d;
	calibrate(&d, 0);
	/* Below the old 40 deg/s and 120 mg gates; two deliberate small strokes. */
	for (unsigned stroke=0; stroke<2; stroke++) {
		unsigned beats=0;
		for (unsigned offset=0; offset<1000; offset+=50) {
			uint32_t t=1150+stroke*1000+offset;
			struct motion_sample s=sample(t, offset<300 ? 37 : 2,
						     offset<300 ? 100 : 0);
			struct conducting_beat b;
			if (conducting_process(&d, &s, &b)) {
				assert(offset>=300 && offset<=400);
				beats++;
			}
		}
		assert(beats==1);
	}
	/* Small alternating disturbances while holding still must not arm. */
	for (unsigned t=3150; t<6150; t+=50) {
		struct motion_sample s=sample(t, t%100 ? 7 : -3, t%100 ? 20 : -20);
		struct conducting_beat b;
		assert(!conducting_process(&d, &s, &b));
	}
}

/* A straight stroke accelerates then brakes along the same axis, with almost
 * no rotation. Test body X/Y/Z, reverse direction, diagonal grip, and uptime wrap.
 */
static void straight_strokes(unsigned step, uint32_t start, unsigned axis,
			     int amplitude, unsigned sensitivity, bool mixed,
			     unsigned expected)
{
	struct conducting_detector d;
	calibrate(&d, start);
	conducting_set_sensitivity(&d, sensitivity);
	unsigned count = 0;
	uint32_t last = 0;
	for (unsigned t = 1150; t < 4150; t += step) {
		unsigned phase = (t - 1150) % 1000;
		int acceleration = phase < 120 ? amplitude : (phase < 240 ? -amplitude : 0);
		struct motion_sample s = sample(start+t, mixed && phase<240 ? 122 : 2, 0);
		if (axis == 0) { s.accel_x_mg = acceleration; }
		if (axis == 1) { s.accel_y_mg = acceleration; }
		if (axis == 2) { s.accel_z_mg += acceleration; }
		if (axis == 3) {
			s.accel_x_mg = acceleration * 0.6f;
			s.accel_z_mg += acceleration * 0.8f;
		}
		struct conducting_beat b;
		if (conducting_process(&d, &s, &b)) {
			assert(b.detected_ms == s.timestamp_ms);
			assert(b.detected_ms - b.timestamp_ms <= 250);
			if (count) { assert(b.timestamp_ms-last >= 250); }
			if (!mixed) { assert(b.strength > 50); }
			last = b.timestamp_ms;
			count++;
		}
	}
	assert(count == expected);
}

static void straight_rejections(void)
{
	for (unsigned kind = 0; kind < 3; kind++) {
		struct conducting_detector d;
		calibrate(&d, 0);
		for (unsigned t = 1150; t < 4150; t += 5) {
			unsigned phase = (t-1150)%1000;
			/* single impact, one-sided acceleration, and small vibration */
			int a = kind == 0 ? (phase < 10 ? 900 : 0) :
				kind == 1 ? (phase < 200 ? 400 : 0) : (phase%20 < 10 ? 35 : -35);
			struct motion_sample s = sample(t, 2, a);
			struct conducting_beat b = {.strength=77};
			assert(!conducting_process(&d, &s, &b));
			assert(b.strength == 77);
		}
	}
}

static void straight_cancellation(void)
{
	/* Do not finish an old drive after a preset change, gap, or sideways turn. */
	for (unsigned reason=0; reason<3; reason++) {
		struct conducting_detector d;
		calibrate(&d, 0);
		struct conducting_beat b = {.strength=77};
		for (unsigned t=1150; t<1270; t+=5) {
			struct motion_sample s=sample(t, 2, 300);
			assert(!conducting_process(&d, &s, &b));
		}
		assert(d.straight.phase == 1);
		if (reason == 0) { conducting_set_sensitivity(&d, 2); }
		unsigned delay = reason == 1 ? 500 : 0;
		for (unsigned t=1270; t<2000; t+=5) {
			struct motion_sample s=sample(t+delay, 2, t<1390 ? -300 : 0);
			if (reason == 2) { s.accel_y_mg=s.accel_x_mg; s.accel_x_mg=0; }
			assert(!conducting_process(&d, &s, &b));
		}
		assert(b.strength == 77);
	}
}

int main(void)
{
	/* Sensitivity profiles distinguish weak strokes without losing calibration. */
	for (unsigned sensitivity=0; sensitivity<3; sensitivity++) {
		struct conducting_detector profile;
		calibrate(&profile, 0);
		conducting_set_sensitivity(&profile, sensitivity);
		assert(profile.calibrated);
		unsigned beats=0;
		for (unsigned offset=0; offset<1000; offset+=20) {
			struct motion_sample s=sample(1120+offset, offset<300 ? 27 : 2,
						     offset<300 ? 65 : 0);
			struct conducting_beat b;
			beats += conducting_process(&profile, &s, &b);
		}
		assert(beats == (sensitivity==2 ? 1U : 0U));
		conducting_set_sensitivity(&profile, 255);
		assert(profile.sensitivity==sensitivity);
		struct motion_sample gap=sample(3000, 2, 0);
		struct conducting_beat b;
		assert(!conducting_process(&profile, &gap, &b));
		assert(!profile.calibrated && profile.sensitivity==sensitivity);
	}
	gentle_strokes();
	/* At the 208 Hz polling rate, an ideal abrupt stop from 120 deg/s
	 * should trigger within two stop samples (not a hardware latency claim).
	 */
	struct conducting_detector fast;
	calibrate(&fast, 0);
	unsigned fast_beats=0;
	for (unsigned offset=0; offset<1000; offset+=20) {
		struct motion_sample s=sample(1120+offset, offset<300 ? 122 : 2,
					     offset<300 ? 350 : 0);
		struct conducting_beat b;
		if (conducting_process(&fast, &s, &b)) {
			assert(offset>=300 && offset<=320);
			fast_beats++;
		}
	}
	assert(fast_beats==1);
	periodic(50, 0, true, true);
	periodic(20, 0, true, true);
	periodic(5, 0, true, true);
	periodic(37, 0, true, true);
	periodic(50, UINT32_MAX-3000, true, true);
	periodic(50, 0, false, true);
	/* Acceleration-only strokes are now intentionally accepted when biphasic. */
	for (unsigned axis=0; axis<4; axis++) {
		straight_strokes(2, 0, axis, 300, 0, false, 3);
		straight_strokes(3, 0, axis, -300, 0, false, 3);
		straight_strokes(20, 0, axis, 300, 0, false, 3);
		straight_strokes(50, UINT32_MAX-2000, axis, -300, 0, false, 3);
	}
	for (unsigned sensitivity=0; sensitivity<3; sensitivity++) {
		straight_strokes(5, 0, 2, 100, sensitivity, false, sensitivity==2 ? 3 : 0);
		straight_strokes(5, 0, 0, 400, sensitivity, true, 3);
	}
	straight_rejections();
	straight_cancellation();
	struct conducting_detector d;
	/* Two move-then-hold strokes: exactly one peak event each, no rebound. */
	calibrate(&d, 0);
	for (unsigned stroke=0; stroke<2; stroke++) {
		unsigned beats=0;
		for (unsigned offset=0; offset<1000; offset+=50) {
			uint32_t t=1150+stroke*1000+offset;
			struct motion_sample s=sample(t, offset<300 ? 122 : 2,
						     offset<300 ? 350 : 0);
			struct conducting_beat b;
			if (conducting_process(&d, &s, &b)) {
				assert(offset>=300 && offset<=400);
				/* Event time is the speed peak, before the drop that confirms it. */
				assert(b.detected_ms==t && b.timestamp_ms<t);
				assert(b.timestamp_ms>=1150+stroke*1000);
				beats++;
			}
		}
		assert(beats==1);
	}
	calibrate(&d, 0);
	struct conducting_beat b={.strength=77};
	struct motion_sample s=sample(1100, 100, 500);
	assert(!conducting_process(&d, &s, &b));
	s.timestamp_ms=1090;
	assert(!conducting_process(&d, &s, &b));
	assert(d.previous_ms == 1100 && b.strength == 77);
	s.timestamp_ms=1600;
	assert(!conducting_process(&d, &s, &b));
	assert(!d.calibrated && !d.armed && !d.have_beat);
	/* Motion during startup must not be learned as gyro bias. */
	conducting_reset(&d);
	for (unsigned t=0; t<2000; t+=50) {
		s=sample(t, 30, 0);
		assert(!conducting_process(&d, &s, &b));
	}
	assert(!d.calibrated);
	/* Pure tilt: analytically rotate gravity with the gyro, no translation. */
	calibrate(&d, 0);
	for (unsigned t=1150; t<6000; t+=50) {
		float angle=(t-1100)*0.001f*0.523598776f;
		s=sample(t, 2, 0);
		s.gyro_x_dps_tenths=300;
		s.accel_y_mg=(int16_t)(1000*sinf(angle));
		s.accel_z_mg=(int16_t)(1000*cosf(angle));
		assert(!conducting_process(&d, &s, &b));
	}
	assert(fabsf(d.gravity[1]-s.accel_y_mg) < 5);
	assert(fabsf(d.gravity[2]-s.accel_z_mg) < 5);
	puts("conducting tests passed");
}
