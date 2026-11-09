Musical Baton
=============

Musical Baton is a Zephyr application for an nRF52840 development board
connected to an LSM6DSOX six-axis IMU. The current firmware reads motion
data over I2C and exposes it over Bluetooth Low Energy (BLE). An experimental
six-axis detector reports conducting beat candidates to the console. It does
not generate music or classify conducting patterns/time signatures.

Current behavior
----------------

* Uses the Zephyr LSM6DSO driver to check the LSM6DSOX identity and configure
  both accelerometer and gyroscope.
* Configures both sensors at 416 Hz, with acceleration range +/-4 g and
  gyroscope range +/-1000 degrees/second.
* Enables the accelerometer hardware LPF2 at ODR/10 (approximately 41.6 Hz),
  smoothing all three acceleration axes before console and BLE output.
* Polls the latest X/Y/Z acceleration and angular velocity every 2,404 microseconds
  (approximately 416 polls/second; polling is not data-ready synchronized).
  BLE retains a 50 ms minimum publication interval, checked on each poll; normal
  2.404 ms cycles yield roughly 50-53 ms between notifications (about 19-20 updates/second).
* Prints initialization, errors, calibration status, and beat/BPM messages.
  Continuous six-axis console output is disabled for beat testing; set
  ``PRINT_MOTION_SAMPLES`` to ``1`` in ``src/main.c`` and rebuild to restore it.
  Sensor acquisition and BLE streaming continue with sample prints disabled.
* Initializes Bluetooth without advertising after startup.
* Each debounced button press prints ``Baton button pressed``.
* Triple-pressing the user button toggles Bluetooth availability.
* Double-pressing the user button restarts beat-detector calibration. Hold still
  afterward until ``Beat detector calibrated`` appears; the BLE connection stays up.
* When enabled, advertises as ``Smart Baton`` and accepts one connection.
* If no phone connects within 60 seconds, stops advertising, turns the breathing
  LED off, and marks Bluetooth inactive. Triple-press starts a new window.
  A phone connection cancels the timeout; a disconnect opens a fresh 60-second
  advertising window. This checks a live connection, not stored pairing/bonding.
  IMU sampling continues while Bluetooth is inactive.
* When disabled, stops advertising and disconnects the connected phone.
* The Bluetooth LED flashes three times on phone connection/disconnection:
  100 ms on and 100 ms off per flash (600 ms total). It then becomes solid
  when connected, breathes when advertising, or turns off when inactive.
* Holding the user button for ten seconds reboots the baton.
* Allows a BLE client to read six-axis motion or subscribe to notifications.

Experimental conducting detector
--------------------------------

``src/conducting.c`` uses a peak-speed detector plus a low-rotation straight-stroke
acceleration fallback. Advanced
direction models remain a separate experiment and do not alter this module or
its public interface.
Its interface in ``src/conducting.h`` is ordinary C, independent of Zephyr and
pin mappings. The main loop processes successful sensor samples and submits beat notifications
before routine motion publication.
Beat events print to the console and notify subscribed phones using a separate
12-byte BLE characteristic; the existing 20-byte motion packet is unchanged.

Hold the baton still for at least one second after startup until
``Beat detector calibrated`` appears. Calibration requires acceleration near 1 g,
low angular speed, and a stable acceleration vector; it estimates gyro bias.
It cannot prove stillness; sufficiently slow motion can bias calibration.
A sampling gap longer than 250 ms resets calibration and tempo history.
To recalibrate manually, double-click DK Button 1 (the normal baton button on the
PCB), then hold still. The existing click collector waits 600 ms after the final
release to distinguish two clicks from three. The console then prints
``Recalibration requested: hold still for one second``. The main thread clears
gyro bias, gravity, stroke, and tempo history and restores the selected sensitivity.
No beat events are produced during calibration; raw sampling/BLE motion continue.
Completion prints ``Beat detector calibrated``. Motion can extend the stillness
wait. Triple-click and ten-second reboot behavior are unchanged. Beat events are
unmetered, so recalibration does not require measure realignment.
Duplicate/backward timestamps are ignored; normal 32-bit uptime wrap is supported.
A caller replaying a new recording must explicitly reset the detector.

Processing uses all six axes:

* Propagate a body-frame gravity vector with bias-corrected gyroscope rotation,
  apply weak accelerometer correction near 1 g, and subtract gravity to obtain
  the stroke's acceleration evidence.
* Smooth total three-axis angular speed with a 15 ms exponential filter. Arm on
  rising motion at the selected threshold (30 degrees/second for Balanced),
  retain the maximum, and confirm it after speed falls below 85% of that maximum.
* Require a minimum 100 ms stroke, sufficient gravity-subtracted acceleration
  (80 mg for Balanced), and at least 250 ms since the previous accepted peak.
  Strokes expire after 1.5 seconds.
* After a beat, require speed to fall below the selected release level (55% of
  the peak for Balanced, or the arm threshold when larger) before another stroke
  can arm. Holding still therefore cannot repeatedly trigger beats.
* For straight strokes below the selected angular arm speed, smooth the
  gravity-subtracted acceleration vector over 15 ms. Require a coherent drive
  followed by opposite braking along the drive direction (no fixed sensor axis).
  Balanced/Deliberate/Gentle use acceleration floors of 120/180/80 mg. Drive
  evidence must last at least 40 ms and accumulate 8 mg-seconds. Braking must
  peak above the floor, last at least 40 ms, accumulate at least 8 mg-seconds
  and 40% of the drive impulse, then fall below 60% of its peak. The whole stroke
  must last at least 100 ms and finish within 800 ms. Sideways acceleration
  exceeding the 0.8 direction-cosine criterion cancels the candidate.
* After a straight candidate or rotational beat, require 120 ms of quiet
  acceleration below 35% of the straight-stroke floor before rearming that path.
  Rotation above the angular arm threshold cancels the straight candidate so
  the existing rotational detector takes priority. Both paths share a 250 ms
  minimum separation on event timestamps AND emission times. Older candidates
  are rejected; no additional BLE packets or source flag were introduced.
* ``timestamp_ms`` is the retained angular-speed peak or straight-stroke braking
  peak; ``detected_ms`` is the
  later sample that confirmed the drop. Tempo is estimated only after accepted
  events and expires after two seconds without a beat.

Illustrative output::

   BEAT: t=2400 detected=2460 strength=71 BPM=100.0

``strength`` is a heuristic 0-100 score, not a probability. Rotational beats
retain their existing peak-speed/acceleration score. Straight strokes use 50
points for braking peak relative to twice the preset acceleration floor and 50
points for braking impulse relative to drive impulse, each capped at 50. This
avoids assigning all near-zero-rotation strokes a weak gyro-based score. The
phone's strength cutoff and optional idle ML gate still apply: a newly detected
straight beat can still be suppressed on the phone. ML was not retrained here.

Thresholds need tuning on labeled hardware recordings. This fallback detects
biphasic translation evidence, not proof of a downward gesture or zero velocity.
One-sided bumps and tiny vibration are rejected by synthetic tests; ordinary
handling can still resemble a stroke. Gravity drift, rapid repeated translations
without 120 ms quiet, very small gestures, and curved low-rotation motion remain
limitations. Direction is stroke-relative, so any grip/axis can qualify. Straight
beats are timestamped near braking rather than the rotational speed maximum;
compare perceived audio timing on hardware before tuning confirmation thresholds.

Polling now targets 2,404 us; BLE retains its 50 ms publication gate (roughly 50-53 ms
between sends). The IMU ODR is 416 Hz on both sensor channels.
Sampling, hardware filtering, and smoothing still add latency; equal event and
detection timestamps do not mean zero physical motion-to-output delay. Beat lines add UART load.
Measure timing with logging enabled and disabled before performance claims.

To test on the DK, flash ``west flash -d build-lsm6dsox-dk``, open the TTY at
115200 baud, and hold still until ``Beat detector calibrated``. Make a clear
rotating stroke, then stop and hold: expect one ``BEAT:`` line near the fastest
part of the stroke, with no repeats
while still. Repeat strokes to see BPM settle; zero BPM does not suppress beat
output. Raw acceleration/gyro printing remains disabled. If IMU initialization
fails, detection cannot start.

The true return from ``conducting_process`` is the event boundary for both the
console and a BLE beat notification. The companion iPhone app's Conducting Mode
subscribes to beat events and plays a prepared click while enabled. The MCU sends
an event, not audio data. It does not require stable BPM or raw-motion subscription.
No speaker/audio playback is implemented on the MCU itself.

Windows PC audio cues (no phone app)
------------------------------------

``tools/pc_audio_cues.py`` listens to the DK's USB serial console and plays a
short tone through the PC's default audio output. Every audible event uses the
same 25 ms cue. There is no measure counter, downbeat, or configured time
signature. Each cue is driven by the ``BEAT:`` event that
``conducting.c`` produces from real IMU samples. This path needs no Bluetooth
adapter, phone app, or sound files.

Connect the DK by USB, install the serial dependency and low-latency audio
backend, and run the listener::

   py -m venv .venv
   .venv\Scripts\python -m pip install pyserial sounddevice
   .venv\Scripts\python tools\pc_audio_cues.py

Using the virtual-environment interpreter is important: installing a package
with one Python and launching the script with another silently selects the
``winsound`` fallback. The project ``.gitignore`` excludes ``.venv``.

The script automatically selects a single SEGGER/J-Link/nRF serial port when
possible. If selection is ambiguous, set ``serial_port`` (for example ``COM5``)
in ``tools/pc_audio_cues.json``. Keep ``serial_baud`` at the firmware's 115200.
Close any other serial terminal first because only one program can own the COM
port at a time.
Hold the baton still until calibration completes, then conduct; non-beat console
messages remain visible and a detected ``BEAT:`` line plays only when its
strength is strictly above ``strength_threshold`` in
``tools/pc_audio_cues.json`` (50 by default). Events at or below the threshold
are still printed and recorded but do not play audio. The same filter applies
to serial and BLE live modes. The audio device remains open between cues to avoid per-click startup
latency. A continuously running 128-frame callback inserts or replaces the cue
within about 2.7 ms at 48 kHz instead of queuing blocking audio writes. Only the newest pending cue is retained, and 100 ms at or below 5 deg/s
clears it. Audio is queued before CSV writes, and diagnostic files flush once
per second rather than once per sample. On Windows, the script selects the
WASAPI instance matching the default output device when available; this avoids
the much larger MME buffer. It prints the selected API, device, and reported
latency at startup. Without ``sounddevice``, the script falls back to ``winsound.Beep``.
Stop with Ctrl+C.

Use ``--test`` to play the configured tone once, or ``--demo`` to play an
unmetered synthetic pulse at the configured ``bpm`` without the IMU. Edit the
JSON's single ``beat`` object to change its frequency and duration.
Set ``strength_threshold`` from 0 through 99 to tune the audible-event floor;
playback requires ``strength > strength_threshold``.

Recording conducting diagnostics
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The firmware emits a compact ``CD`` diagnostic line at approximately 40 Hz
when no phone is connected, for
real-motion tuning while continuing to acquire the IMU at 416 Hz. Record a run
and play cues at the same time with::

   py tools/pc_audio_cues.py --record conducting-60.csv

Repeat with clear, steady trials near 60, 100, 140, 170, and 200 BPM, using both
discrete strokes and continuous loops. Hold still for calibration before each
trial and leave several seconds of stillness at its beginning and end. Use a
separate filename for every trial and note the intended tempo/style.

The main CSV contains device time, all three gyro and accelerometer axes,
smoothed angular-speed magnitude, gravity-subtracted acceleration, arm/release
state, prediction state, relative-orientation phase, and filtered phase
rate. The motion-mode field is ``0`` while undecided, ``1`` for discrete
linear strokes, and ``2`` for continuous loops. The final field records linear
direction coherence from 0 through 1000. It is diagnostic only; curved strokes
with low coherence remain eligible. A second ``*-beats.csv`` file records every emitted
event, including its predicted flag; keep both files together. The diagnostic
recorder also accepts the earlier diagnostic format; its unavailable raw
accelerometer-axis columns are left empty. The diagnostic
stream is deliberately decimated to reduce UART load; raw acquisition remains
416 Hz. ``PRINT_CONDUCTING_DIAGNOSTICS`` in ``src/main.c`` can be set to ``0``
after data collection. UART output still adds timing load, so final latency must
also be measured with diagnostics disabled.

Lossless gesture-training capture
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For MLC/FSM dataset collection, make a separate DK build with the training
configuration and 1 Mbaud UART overlay::

   west build --no-sysbuild -p always -d build-training -b nrf52840dk/nrf52840 -- \
     -DDTC_OVERLAY_FILE="nrf52840dk_nrf52840.overlay;boards/training_capture.overlay"

The presence of the training overlay disables the text ``CD`` stream and writes one
22-byte binary ``MBR1`` record for every successful 416 Hz sample. Each record
contains a 16-bit sequence, 32-bit device timestamp, accelerometer XYZ in mg,
and gyroscope XYZ in 0.1 degrees/second, all little-endian. The sequence exposes
dropped records. At 416 Hz this consumes about 9.2 kB/s. The dedicated overlay
uses 1 Mbaud so streaming does not consume most of each 2.404 ms sampling period;
avoid opening a second serial client and keep unrelated console activity low.

Install ``pyserial`` and start a labeled recording with::

   .venv\Scripts\python tools\capture_training.py direction-session-001.csv --port COM3

The recorder resynchronizes on ``MBR1`` markers and writes host time, session
time, the active label, sequence, device time, and all six axes. Press ``0`` for
idle and the down/left/right arrow keys for their matching
stroke classes, the up arrow for diagonal-up, and ``q`` to stop. Clockwise and
counterclockwise are not labels. Change the label before each prompted gesture and
include idle between trials. Do not record ordinary motion as a competing
``non_beat_motion`` direction class: an ordinary movement can have the same
short-window geometry as an intentional beat.
The keyboard label is
host-timed, so later segmentation should retain margin around label changes.

Offline gesture-model baseline
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Install scikit-learn in the project virtual environment, then train the compact
gesture decision-tree baseline from complete sessions::

   .venv\Scripts\python -m pip install scikit-learn
   .venv\Scripts\python tools\train_gesture_model.py direction-session-001.csv direction-session-002.csv direction-session-003.csv

The trainer rejects corrupt/nonmonotonic packets, reports inferred losses,
removes 50 ms around host label changes, and extracts causal 200 ms windows at
25 ms strides. Features are mean, standard deviation, minimum, maximum, RMS, and
peak-to-peak range for all six axes plus acceleration and gyro norms. It trains a
depth-limited, class-balanced decision tree and evaluates by holding out each
entire session; it never randomly mixes neighboring windows across train/test.
Artifacts are written to ``models/gesture_v1``: a joblib model, readable tree,
and JSON report with per-class metrics and confusion matrices.

The earlier three-session baseline included ``non_beat_motion`` as a peer class
and is retained only as an experiment; collect a new direction-only dataset for
the next model. The first three-session baseline is not ready for firmware or MLC deployment.
After adapting the transition margin to per-stroke arrow-key labels, held-out
accuracies are 65.2%, 64.8%, and 71.3%. Direction classification is now learned
but remains uneven: held-out direction F1 ranges from 0.29 to 0.67, with left
and right generally weaker than down and diagonal-up. The current
gesture-level label spans preparation, intended stroke, and recovery, so it can
train a coarse activity classifier but does not identify the physical beat
instant or prove early prediction. Add more session-independent data and
beat-phase annotations before converting a tree to an LSM6DSOX UCF model.

The trained ``joblib`` artifact is a PC-side direction classifier, not firmware
and not a beat-timing detector. Keep the simple ``conducting_process()`` peak
detector as the timing gate. A separate classifier can consume rolling 200 ms
motion windows, smooth consecutive direction outputs, and associate the latest
stable direction with an accepted peak. Validate that combination by replaying
held-out sessions before implementing it on the baton. A scikit-learn random
forest cannot run directly in the LSM6DSOX MLC; deployment requires either a
small embedded-C model on the nRF52840 or an MLC-compatible decision tree
exported as an ST ``.ucf`` configuration. Neither path requires changes to
``src/conducting.c`` or ``src/conducting.h``.

The serial cue program exposes that combined experiment without changing the
firmware detector. Use the committed peak detector alone with::

   .venv\Scripts\python tools\pc_audio_cues.py --detector peak

Or require the baseline direction tree to classify the current window as
non-idle before playing the same peak event::

   .venv\Scripts\python tools\pc_audio_cues.py --detector peak-ml --model models\gesture_v1\gesture_tree.joblib

``peak-ml`` reports the selected direction on each played event. It uses the
40 Hz ``CD`` diagnostic stream, while the current tree was trained from 416 Hz
capture, so this is an integration experiment rather than validated accuracy.
It does not replace the peak timestamp with an ML-generated timestamp. The ML
choice currently requires USB serial mode; BLE beat notifications do not carry
the six-axis window. Both modes still apply ``strength_threshold`` from
``tools/pc_audio_cues.json``; use zero temporarily when checking event flow.

An optional live-baton mode is also available. It requires a Bluetooth adapter
and the ``bleak`` package::

   py -m pip install bleak
   py tools/pc_audio_cues.py --ble

For live mode, triple-click the baton first so that it is advertising. The
client writes the configured sensitivity, subscribes, and prints each beat,
sequence, device timestamp, BPM, and strength. Windows may request Bluetooth
permission on the first run.

The same JSON can select ``balanced``, ``deliberate``, or ``gentle`` sensitivity
for live mode. Leave ``device_address`` as ``null`` to discover by the
``Smart Baton`` name, or set it to a specific BLE address if multiple batons are
nearby. A BLE sequence gap resets the next audible cue to beat 1. This is a test
path only: it does not change firmware behavior, persist a profile on the baton,
or provide the companion app's recorded voices. PC Bluetooth/audio latency still
must be measured before making timing claims.

GestureSim review and upgrade recommendations
--------------------------------------------

Reviewed ``GestureSIm/GestureSim/GestureSim/Algorithms/conducting-motion-lab``.
Its algorithms use normalized pointer X/Y positions and derivatives, not trained
weights. Its computed ``omega`` is planar trajectory turning rate, not a directly
interchangeable gyro axis. The simulator was not modified by this firmware change.

* ``App.tsx`` initializes ``extrema`` and offers ``threshold``; neither exists in
  ``createAlgorithm``, so both silently fall back to ``IctusDetector``. Several
  descriptions refer to obsolete strategies. Fix UI/factory mappings before
  comparing algorithms.
* Algorithms impose a 300 ms minimum interval; some predict missing beats.
  Separate measured beats from predictions and clear tempo after stream gaps.
* The peak predictor uses a fixed 30% period offset. Its latency comments are
  not evidence of performance on this IMU or real conducting recordings.

Recommended next steps, in order:

1. Record timestamped six-axis packets and sequence numbers with independently
   labeled ictus times. Include different people/grips, 60-180 BPM, 2/3/4 patterns,
   tempo changes, pauses, walking, and pickup/putdown. Evaluate precision/recall,
   false beats per idle minute, tempo error, signed timing error, and confirmation
   latency. Split evaluation by person/session.
2. Add a separate IMU replay input to GestureSim: decode mg and 0.1 deg/s, validate
   packet version/flags, detect sequence gaps, and use device timestamps. Do not
   feed acceleration into pointer X/Y or double-integrate it into position.
   Accelerometers observe both gravity and motion; see
   `NXP AN3461 <https://www.nxp.com/docs/en/application-note/AN3461.pdf>`_.
3. Evaluate roughly 50 Hz acquisition/publication and data-ready/FIFO operation,
   decimating console prints. Measure the existing 41.6 Hz filter delay before
   selecting wider bandwidth. Revalidate overlays if hardware settings change.
4. Calibrate grip orientation for directional beat-pattern features beyond the
   axis-independent speed and relative-rotation baseline. Train a small temporal
   classifier once labeled recordings justify it, including a non-conducting class.
5. Add separately marked prediction/audio scheduling only after measured accuracy
   and device-to-phone clock mapping are established.

Host regression tests
---------------------

Run deterministic C tests without Zephyr::

   cc -std=c11 -Wall -Wextra -Werror -Isrc tests/conducting_test.c src/conducting.c -lm -o /tmp/baton-conducting-test
   /tmp/baton-conducting-test

Tests cover synthetic strokes at multiple sample intervals, peak-aligned beat
timestamps, rejection of gyro-only motion, straight acceleration/braking strokes, stop-without-rebound triggers, no repeated triggers
while held still, rearming on a new stroke, calibration, gaps, duplicate/backward times,
uptime wrap, and gravity tracking during tilt. They do not establish real conducting
accuracy. Flash the matching build, hold still to calibrate, and compare candidate
and confirmation timestamps against independently labeled hardware recordings.

Hardware and configuration
--------------------------

Both the DK and PCB overlays configure an LSM6DSOX at I2C address ``0x6a``
with a 400 kHz bus. Connect sensor power and ground according to the sensor
module and board requirements. For I2C operation, CS must be held high and
SA0 low for ``0x6a``. If your breakout uses SA0 high (``0x6b``), change its
node address and ``reg`` in the selected overlay. Ensure the I2C bus has pull-ups.

* DK development wiring: SDA ``P0.27``, SCL ``P0.26`` (now matches the PCB).
* Custom PCB wiring: SDA ``P0.27``, SCL ``P0.26`` (from the schematic).
* Sampling uses polling; INT1 and INT2 do not need to be connected on the DK.
  The PCB overlay retains INT1 on ``P0.13`` and INT2 on ``P0.14`` for future use.

The ``baton-imu`` devicetree alias selects the IMU without hard-coded GPIOs
or I2C addresses in C. NCS v3.4.0 uses the ``st,lsm6dso`` compatible and
``CONFIG_LSM6DSO`` driver for the LSM6DSOX's basic acceleration/gyro registers
and matching ``WHO_AM_I`` value (``0x6c``). See the
`Zephyr LSM6DSO binding <https://docs.zephyrproject.org/latest/build/dts/api/bindings/sensor/st%2Clsm6dso-i2c.html>`_
and `ST LSM6DSOX datasheet <https://www.st.com/resource/en/datasheet/lsm6dsox.pdf>`_.
Embedded machine-learning features
are not used. Both overlays explicitly set nonzero output data rates because
the binding defaults to power-down.

The nRF52840 DK overlay maps the baton button to the development kit's
built-in Button 1 on ``P0.11``. This is a development-only mapping; the
custom baton PCB uses its external user button on ``P0.19``.

* ``nrf52840dk_nrf52840.overlay`` configures the nRF52840 DK.
* ``boards/musical_baton.overlay`` configures the custom PCB.
* ``nrf21540dk_nrf52840.overlay`` contains auxiliary nRF21540 DK IMU wiring;
  its BLE LED mapping remains incomplete, so it is not a validated build target.
* ``prj.conf`` enables I2C, the Zephyr sensor/LSM6DSO driver, console output,
  logging, and BLE peripheral support.
* ``src/main.c`` initializes the modules and runs the sampling loop.
* ``src/sensor.c`` waits 100 ms before explicitly initializing the deferred
  IMU driver, waits another 500 ms for filters/measurements to settle, and converts
  six-axis readings to mg and 0.1 degrees/second.
* ``src/sensor.h`` defines ``struct motion_sample``. Failed reads leave the
  caller's sample unchanged; values are rounded and clamped to signed 16 bits.
* ``src/bluetooth.c`` handles BLE advertising, reads, and notifications.
* ``src/button.c`` handles debouncing, click counting, and long holds.
* ``src/sensor.h`` and ``src/bluetooth.h`` declare the module interfaces.

Building and running
--------------------

Use an installed Nordic nRF Connect SDK and its matching toolchain. In the
nRF Connect extension for VS Code, open this directory as the application,
create a build configuration for your development board, and select the
matching overlay if it is not selected automatically. Build and flash the
connected board, then open its serial console to view initialization, errors, and beat candidates.

From an NCS v3.4.0 terminal, build the development kit::

   west build --no-sysbuild -p always -d build-lsm6dsox-dk -b nrf52840dk/nrf52840 -- -DDTC_OVERLAY_FILE=nrf52840dk_nrf52840.overlay

Build the custom PCB (currently using the DK board definition)::

   west build --no-sysbuild -p always -d build-lsm6dsox-pcb -b nrf52840dk/nrf52840 -- -DDTC_OVERLAY_FILE=boards/musical_baton.overlay

Flash the DK build::

   west flash -d build-lsm6dsox-dk

For the custom PCB, use ``west flash -d build-lsm6dsox-pcb``.

The separate builds have these pin assignments:

===================== ====================== ======================
Function              DK build               PCB build
===================== ====================== ======================
User button           P0.11 (DK Button 1)     P0.19
Bluetooth LED         P0.03 (Arduino A0)      P0.21
IMU SDA               P0.27                  P0.27
IMU SCL               P0.26                  P0.26
Battery RGB           Not mapped             P0.22 / P0.23 / P0.24
===================== ====================== ======================

On startup, the DK should print ``Button initialized: ... pin 11``; the
PCB should print ``pin 19``. Always pass the corresponding build directory
to ``west flash``.

The SDK is installed separately from this repository. Generated build
files in ``nrf52840dk/`` are excluded from Git.

Bluetooth data format
---------------------

Connect to ``Smart Baton`` using a BLE client. The custom service UUID is
``12345678-1234-5678-1234-56789abcdef0``. Its readable and notifiable motion
characteristic UUID is ``12345678-1234-5678-1234-56789abcdef1``.

Each value is a fixed 20-byte, little-endian packet:

* Byte 0: packet format version, currently 1.
* Byte 1: flags (bit 0 acceleration valid, bit 1 gyroscope valid, bit 2 time synchronized).
* Bytes 2-3: unsigned 16-bit sequence number.
* Bytes 4-7: unsigned 32-bit nRF uptime in milliseconds.
* Bytes 8-9: signed 16-bit X acceleration in mg.
* Bytes 10-11: signed 16-bit Y acceleration in mg.
* Bytes 12-13: signed 16-bit Z acceleration in mg.
* Bytes 14-15: signed 16-bit X angular velocity in 0.1 degrees/second.
* Bytes 16-17: signed 16-bit Y angular velocity in 0.1 degrees/second.
* Bytes 18-19: signed 16-bit Z angular velocity in 0.1 degrees/second.

Successful LSM6DSOX reads populate all six axes and set both validity bits
(``flags = 0x03``). The time-synchronized bit remains clear; the timestamp is
local uptime captured immediately after the sensor fetch, before conversion,
serial printing, and notification submission. This is a host acquisition timestamp,
not the IMU internal measurement time or phone delivery time. The packet version, UUIDs, and
20-byte layout are unchanged. Failed sensor reads are not published. Enable
notifications on the characteristic to receive updates.

Conducting sensitivity settings
-------------------------------

The firmware and PC cue client treat the beat stream as unmetered. The event
sequence is a transport counter, not a musical beat number. Meter and downbeat
selection are intentionally absent. A separate companion app is outside this
repository and must apply the same policy independently.

New characteristic ``12345678-1234-5678-1234-56789abcdef3`` in the existing service
supports read and write-with-response. Its four-byte packet is
``[1, 1, sensitivityID, 0]``: version 1, set-sensitivity command 1, preset, reserved.
Read returns the current requested setting in the same format. Wrong length,
nonzero offset, prepared writes, and unsupported bytes/presets are rejected.

================= ============ ============ ==================
Preset / ID       Arm speed    Rearm below  Straight accel floor
================= ============ ============ ==================
Balanced / 0      30 deg/s     55% of peak  120 mg
Deliberate / 1    40 deg/s     45% of peak  180 mg
Gentle / 2        22 deg/s     60% of peak  80 mg
================= ============ ============ ==================

Rotational beats confirm below 85% of the speed peak, require at least 100 ms
of stroke and acceleration corroboration (80/120/55 mg). Both paths enforce
250 ms event and emission separation. The straight-stroke rules are described
above. Quaternion loop detection is not present in this implementation; older
experiment notes below do not describe the current detector.
Gentle may increase false positives. BLE callbacks only store an atomic preset
request; the main sampling thread applies changes before detection and clears
stroke/tempo history while preserving calibration. A sensor gap recalibrates but
keeps the selected preset. Reboot resets to Balanced. There is no flash storage
for profiles in the MCU. A client may reapply its selected preset on activation;
readback confirms the request, not the exact sample at which it took effect.
Automatic gesture-pattern recognition is not implemented. Validate sensitivity
and audio timing on hardware.

Bluetooth beat events
---------------------

Characteristic ``12345678-1234-5678-1234-56789abcdef2`` under the existing motion
service is notify-only. Subscribe from the companion app's Conducting Mode to
receive fresh detected beats. There is no readable last beat or replay on subscribe.
The fixed 12-byte little-endian format is:

* Byte 0: version 1; byte 1: event type 1 (detected beat).
* Bytes 2-3: unsigned 16-bit beat sequence.
* Bytes 4-7: unsigned 32-bit observed beat uptime in milliseconds.
* Bytes 8-9: BPM times 10; zero indicates an unstable/unavailable estimate.
* Byte 10: motion strength, 0-100; byte 11: reserved, zero.

Beat sequences advance on each detection, including while inactive/unsubscribed,
and wrap at 65536. Timestamp wrap is unchanged. Notifications use the live
connection's subscription, independently of the 50 ms raw-motion gate, and precede
the beat's UART line. Failed notifications print an error and are not retried.
The phone must connect and subscribe; triple-click still activates Bluetooth.
No events are stored for future connections. Radio/app delivery is not guaranteed
by a successful local send; measure motion-to-sound latency on hardware.

Flash this firmware, install the updated mobile app, connect, open Conducting
Mode, and enable its toggle. Use the two Test Sound buttons to check the phone's media volume/output.
Hold still to calibrate, then make strokes and compare clicks against UART beats.
Closing the screen, app inactivity, or disconnection stops the app's sound mode.
Reconnection requires enabling the mode again. Existing motion clients remain
compatible because the motion UUID and packet format have not changed.

Hardware verification
---------------------

After flashing with the LSM6DSOX connected, check for the ``chip id 0x6c``
and ``LSM6DSOX ready`` startup messages. Triple-press to enable Bluetooth,
connect a BLE client, and subscribe to the motion characteristic. Confirm
``flags = 0x03``, about 1000 mg along the gravity axis at rest, and gyroscope
values near zero. Rotate the board to verify angular velocity changes on all
three axes. Builds verify compilation and devicetree configuration; actual
sensor communication and motion accuracy still require this hardware check.

Streaming timing verification and next step
-------------------------------------------

On the DK, subscribe from the iOS app and record packet timestamps, sequence
numbers, and phone receipt times for at least a minute, including motion and
button activity. Compare unsigned timestamp differences (allow 32-bit wrap):
expect roughly 50-53 ms between BLE packets during normal 416 Hz polling, with larger gaps on failed reads
or delayed work. Check sequence differences modulo 65536: normally +1 during
an uninterrupted subscribed session. Sequences count publication attempts,
including updates while advertising and failed notifications, rather than
acknowledged delivery; sensor failures produce timestamp gaps without necessarily
producing sequence gaps. Track notification errors on the UART and distinguish
packet timing from batches delivered to the app. Repeat connect/disconnect,
triple-click activation/deactivation, advertising expiry, and LED checks.
Build success alone does not establish delivery rate or motion accuracy.

Acquisition now targets 2,404 us. After validating this baseline, optionally reduce
the BLE publication interval if higher-rate streaming is required. The 416 Hz IMU produces a new output
about every 4.8 ms; polling is not synchronized to data-ready, so consider
interrupt/data-ready acquisition or FIFO for consistent fresh measurements.
Reduce or decimate sample printing at this rate. Measure the actual negotiated
BLE connection interval and peripheral latency on iOS; evaluate a requested
15-20 ms interval with zero peripheral latency and confirm what the phone
accepts. Connection parameters are documented in the `Zephyr connection API
<https://docs.zephyrproject.org/latest/doxygen/html/group__bt__conn.html>`_.
Faster publication alone does not establish lower end-to-end latency.

The current baseline uses a 416 Hz ODR and ODR/10 accelerometer LPF2. Before precise
beat-to-audio timing claims, measure accelerometer LPF2 response delay at its
approximately 41.6 Hz cutoff, gyroscope response, acquisition jitter, BLE delivery
jitter, phone audio latency, and clock offset/drift (time synchronization is
still absent). Revisit filter bandwidth and settling if changing ODR/filter
settings. The 416 Hz acquisition target is enabled; BLE retains its 50 ms gate.

Bluetooth LED troubleshooting
-----------------------------

On the DK the BLE LED is an external LED on Arduino A0 / P0.03. Wire
A0 through a 330 ohm to 1 kohm resistor to the LED anode, and connect the
cathode to GND. The long leg usually marks the anode; the flat case edge
usually marks the cathode. Built-in DK LEDs are not selected by ``ble-led``.

``Advertising as Smart Baton`` and ``Bluetooth active`` confirm that the
Bluetooth code requested the advertising LED state. The LED should breathe
with a three-second cycle. Connecting a BLE client produces three quick
100 ms on/off flashes before solid full brightness. Disconnecting produces
the same flashes before breathing resumes, or before the LED turns off if
Bluetooth was deactivated while connected. Activating/deactivating Bluetooth
without a connected phone does not produce the connection flashes.

The animation uses delayed work and does not sleep in Bluetooth callbacks
or interrupt the IMU loop. The BLE connection/disconnection itself happens
immediately; only the LED's transition to steady behavior is delayed by
600 ms. Advertising restarting during disconnect flashes updates the final
LED state without cutting the flashes short. Repeated state requests do not
restart an animation; a new connection/disconnection restarts the three flashes.
A connected client's steady full brightness can help distinguish a fade
problem from an electrical issue.
Check the console for PWM update errors.

If the external LED stays dark, disconnect power before changing wiring.
Check LED polarity, resistor placement, and breadboard rows. Test the LED
with its resistor from VDD to anode and cathode to GND, disconnecting the
A0 connection for that test. If it does not light, check the LED circuit and
supply voltage; if it does, restore A0 wiring and investigate the PWM output.
Retain the series resistor in either arrangement. A multimeter on A0 while
advertising can check for a changing average voltage, but an oscilloscope or
logic analyzer is needed to inspect the PWM waveform reliably.

Software validation (2026-10-01): the generated DK pinctrl maps PWM channel
0 to P0.03. Mocked host checks passed for a full breathing cycle, connected
full brightness, and inactive output. These checks do not verify physical
PWM output, the connected LED circuit, or the actual workqueue timing.

Button troubleshooting
----------------------

The DK firmware listens to built-in Button 1 (P0.11), not the RESET button
or an external button on the PCB's P0.19 mapping. Startup prints the GPIO
controller and pin, followed by ``Triple-press to toggle Bluetooth`` after
all modules initialize. If sensor initialization fails, startup exits before
button/Bluetooth initialization; correct that error first.

Every debounced press now enqueues a ``BUTTON_EVENT_PRESSED`` event, and the
main loop prints ``Baton button pressed``. GPIO interrupts only schedule
work; the event callback only queues events. Three presses/releases, each
lasting longer than the 30 ms debounce time and less than 600 ms apart,
produce ``Detected 3 clicks`` and ``Triple-click detected`` approximately
600 ms after the final release. The LED then breathes while advertising.
If individual press messages are missing, check the selected overlay and
button connection. If presses print but the count is different, check click
timing. IMU sampling continues during button handling.

If startup prints ``Button initialized: ... pin 19`` while using the DK,
the flashed firmware uses the custom-PCB button mapping. The DK build should
print ``pin 11``. The PCB overlay also moves the BLE LED to P0.21 instead of
DK A0/P0.03, so select the correct build rather
than changing the button GPIO in C. DK pin assignments do not overlap: Button 1
P0.11, BLE LED P0.03, IMU SDA P0.27, and IMU SCL P0.26.
DK and PCB now share the same IMU SDA/SCL wiring.

Explicitly select the DK overlay and flash its build directory::

   west build --no-sysbuild -p always -d build-lsm6dsox-dk -b nrf52840dk/nrf52840 -- -DDTC_OVERLAY_FILE=nrf52840dk_nrf52840.overlay
   west flash -d build-lsm6dsox-dk

In VS Code/nRF Connect, select the DK build configuration and its DK overlay,
then flash that configuration. Both DK and PCB builds currently share the
``nrf52840dk/nrf52840`` board target, so the board name alone does not identify
the wiring. Keep the build directories separate to avoid flashing the PCB
configuration onto the DK.

IMU noise filtering
-------------------

The LSM6DSOX has hardware low-pass and high-pass filters. Relevant registers
for the primary I2C output path are:

* Accelerometer: ``CTRL1_XL`` (0x10), ``LPF2_XL_EN`` enables the additional
  low-pass filter; ``CTRL8_XL`` (0x17), ``HPCF_XL`` selects its cutoff and
  ``HP_SLOPE_XL_EN`` selects the high-pass path instead.
* Gyroscope: ``CTRL4_C`` (0x13), ``LPF1_SEL_G`` enables the optional LPF1;
  ``CTRL6_C`` (0x15), ``FTYPE`` selects bandwidth. ``CTRL7_G`` (0x16)
  contains the high-pass enable/cutoff controls. Gyro LPF2 remains in the
  normal output chain, with bandwidth tied to the output data rate.

See the `ST datasheet filtering/register tables <https://www.st.com/resource/en/datasheet/lsm6dsox.pdf>`_
and `ST AN5272, sections 3.8 and 3.10 <https://www.st.com/resource/en/application_note/an5272-lsm6dsox-alwayson-3d-accelerometer-and-3d-gyroscope-stmicroelectronics.pdf>`_.

Current overlays run both sensors at 416 Hz in their default high-performance
mode, with accelerometer LPF2 enabled at ODR/10 = 41.6 Hz. Gyroscope filter
configuration stays at the driver defaults. Without LPF2, the accelerometer
LPF1 bandwidth is ODR/2 = 104 Hz. Accelerometer LPF2 options include
ODR/4 = 104 Hz, ODR/10 = 41.6 Hz, and ODR/20 = 20.8 Hz. Stronger filtering
reduces fast noise but also smooths intended motion and changes timing.

The installed NCS v3.4.0 Zephyr LSM6DSO driver exposes ``accel-lp-filter``.
All supplied IMU overlays now configure::

   accel-lp-filter = <LSM6DSO_DT_LP_ODR_DIV_10>;

At 416 Hz this selects approximately 41.6 Hz. The driver's zero/default value
(``LSM6DSO_DT_LP_ODR_DIV_2``) skips enabling LPF2; it must not be treated as
an explicit ODR/4 LPF2 setting despite the hardware's zero cutoff encoding.
The driver has no matching gyroscope filter property or filter attribute;
those controls would require an explicit driver extension or checked register
updates after driver initialization. Verify against the installed driver,
not just a newer online binding.

AN5272 lists up to 20 sensor samples for ODR/10 or ODR/20 settling,
approximately 48 ms at 416 Hz. ``sensor_init()`` waits 100 ms after driver
initialization before allowing reads. Revisit this wait when changing sensor
ODR or the filter cutoff, especially for stronger filtering.

For baton testing, start with low-pass filtering and compare stationary noise
and quick conducting motions before and after, including peak magnitude and
response delay. High-pass filtering removes DC/gravity information, so it is
not a default replacement for acceleration used for tilt/orientation. Filtering
also does not calibrate gyro zero-rate bias (the experimental detector estimates
bias internally without changing raw samples). The firmware polls at roughly 416 Hz and
BLE publishes at about 19 Hz with the current gate; evaluate bandwidth against the rate of the data
actually consumed, or increase acquisition/publication rates for faster motions.
The hardware filter applies to all acceleration samples sent to the terminal
and BLE; no software averaging is added. A low-pass filter preserves the
steady gravity signal. To compare against the default, omit ``accel-lp-filter``
from the selected overlay, rebuild, flash, and repeat the same stationary and
motion checks.

Troubleshooting IMU initialization
----------------------------------

The IMU nodes use ``zephyr,deferred-init``. ``sensor_init()`` waits 100 ms
before the first driver transaction, then waits another 100 ms after successful
initialization before sampling. This avoids accessing the sensor during power-up;
the LSM6DSOX datasheet lists a typical turn-on time of 35 ms. The earlier wait
only happened after driver initialization and could not protect the first write.

``Failed to set user bank`` means the driver's first I2C write failed, before
the identity check. On initialization failure the firmware now reads the
``WHO_AM_I`` register at the configured address and the alternate SA0 address
(``0x6a`` / ``0x6b``), printing either an identity or an I2C error. An identity
of ``0x6c`` at the alternate address indicates that the overlay address should
be changed. If both probes fail, check power, ground, SDA/SCL wiring, soldered
header connections, and the CS level. Address probes do not prove the cause
of a failed transaction and do not automatically change the configuration.

For the Adafruit LSM6DSOX breakout used on the DK, connect VIN to the DK's
VDD (check that it is within the breakout's 3-5 V input range), GND to GND,
SDA to P0.27, and SCL to P0.26. The breakout has I2C pull-ups and defaults to
``0x6a`` unless the bottom DO/address pin or jumper selects ``0x6b``. See
`Adafruit pinouts <https://learn.adafruit.com/lsm6dsox-and-ism330dhc-6-dof-imu/pinouts>`_.

Serial IMU readings
-------------------

After building and flashing, open the DK's serial port in a serial terminal
at 115200 baud, 8 data bits, no parity, 1 stop bit, and no flow control.
On macOS, find the DK port with ``ls /dev/cu.usbmodem*`` and open the matching
port, for example::

   screen /dev/cu.usbmodemYOUR_PORT 115200

Raw sample printing is currently disabled so beat/BPM messages are easier to see.
To restore it, set ``PRINT_MOTION_SAMPLES`` to ``1`` in ``src/main.c``, rebuild,
and flash. With that option enabled, the loop attempts to print each successful
416 Hz sample without needing to enable Bluetooth::

   IMU accel [mg]: X=12 Y=-8 Z=1001 | gyro [0.1 deg/s]: X=0 Y=-1 Z=2

Acceleration is in mg (1000 mg = 1 g). Gyroscope integers are in tenths of a
degree/second: ``-1`` means -0.1 deg/s and ``25`` means 2.5 deg/s. At rest,
the acceleration vector should have magnitude near 1000 mg and gyro values
should be near zero; tilt and rotate the board to check changes. The main
sampling loop targets 2,404 us; BLE retains a 50 ms minimum gate, typically producing
about 53 ms publication spacing with this polling loop.
``SENSOR_POLL_INTERVAL_US`` and ``BLE_PUBLISH_INTERVAL_MS`` in ``src/main.c``
name these intervals; only samples fetched after the publication gate expires
are published while Bluetooth is active. Notifications require a subscribed connection. ``CONFIG_LOG_PRINTK=n`` sends each ``printk``
directly to UART instead of collecting it in the deferred logger for up to a
second. With sample printing enabled, each loop prints one fresh sample and uses a new 2,404 us deadline,
without a burst of catch-up prints. Failed reads print
an error instead of a stale sample. Timing may be delayed by sensor I/O,
scheduling, Bluetooth calls, or button
handling. Publication precedes sample printing. The longest sample line
(102 ASCII bytes with six signed 16-bit values) occupies about 8.9 ms at
115200 baud, 8N1 (about 9 ms with CR/LF translation), which exceeds the 2.404 ms
sampling budget; formatting and other console messages add overhead. Leave raw
printing disabled for 416 Hz acquisition and confirm timing on hardware
with serial output enabled, and compare against a build with sample printing
disabled if jitter is significant. Failed reads retain the existing one-second
backoff and publish nothing. Notification failures print their error code; the
failed packet is not retried, and the next attempt uses a fresh sample. There
are no catch-up notification bursts. Exit ``screen`` with Ctrl-A, then K, then Y.

The custom PCB USB-C port supplies power only; its console requires a separate
UART/debug connection rather than a USB data connection through that port.

Development rules
-----------------

* Update this README with every project change so wiring, behavior, build
  instructions, and limitations stay current.
* Keep board pin mappings in overlays and application code independent of GPIO numbers.
* Build the nRF52840 DK target after firmware changes and validate the PCB
  configuration after hardware-description changes.

Recent changes
--------------

Low-latency phone playback: preferred BLE connection interval is now 15 ms
(min=max=12 in 1.25 ms units), peripheral latency 0, supervision timeout 4 s.
Zephyr's automatic parameter update requests these after the standard 5 s GAP
post-connect delay. The phone may choose a longer interval; UART prints actual
interval/latency/timeout at connection and each update. No rejected requests are
retried in an application loop. Shorter intervals may increase battery use.

Beat processing/notification submission now precedes the same cycle's motion
publication and UART logging. The 50 ms gate applies only to motion, never beats.
The 40 Hz CD UART diagnostic stream pauses while any phone is connected and
resumes on disconnection, preserving USB-only PC classifier operation. Running
the PC serial ML client while a phone is connected no longer supplies its CD
input; disconnect the phone for that workflow. Beat console lines still include
peak and confirmation timestamps, so ``detected - t`` measures detector delay.
The binary training overlay is unchanged and is not the latency-testing build.

Packet formats, motion streaming rate, detector thresholds and LED/button behavior
are unchanged. iOS must be updated separately for the persistent audio engine;
flashing this firmware does not install the mobile app. Check the phone speaker
first, wait at least five seconds after connecting, and record actual connection
parameters plus app audio timing. Compare recorded physical gesture and audible
click before claiming end-to-end improvement. Device/phone clocks are not synced;
subtracting their timestamps cannot measure Bluetooth latency. There is no
zero-latency guarantee; braking/peak confirmation still contributes delay.


Straight-stroke fallback: added gravity-subtracted acceleration/braking detection
for low-rotation translation, including vertical gestures. Existing rotational
thresholds are preserved; shared deduplication also guards emission times. Tests
cover positive/negative X/Y/Z and diagonal strokes at 2/3/20/50 ms intervals,
uptime wrap, sensitivity, mixed rotation/translation, impacts, vibration and
one-sided acceleration, sideways rejection, and mid-stroke sensitivity/gap
cancellation. The NCS v3.4.0 DK build passed (155,148 bytes flash / 26,360 bytes
RAM); host C tests passed with AddressSanitizer and UndefinedBehaviorSanitizer.
Hardware accuracy, false positives and end-to-end mobile filtering need validation.
No pins, sensor settings, BLE formats or mobile code changed. Flash the new DK
build explicitly to try it; a build alone does not update the device.

Earlier experiment notes (some describe superseded, uncommitted algorithms):


416 Hz training-capture foundation: both overlays now configure accelerometer
and gyroscope at 416 Hz, the polling target is 2,404 us, accelerometer LPF2 is
ODR/10 (approximately 41.6 Hz), and the post-init settling wait is 100 ms. The
25 ms speed history grew to 16 entries so the higher rate does not truncate its
window. An optional 22-byte ``MBR1`` UART stream and interactive PC recorder
capture every successful sample with labels for down, lateral, diagonal-up,
loop, idle, and non-beat motion. BLE remains gated at 50 ms. Hardware must still
verify actual freshness, UART loss rate, filter response, and sensor timing.

Curved-stroke and unmetered playback update: linear peak acceptance no longer
requires an 85% fixed-axis direction-coherence score, allowing diagonal and
arcing gestures. Direction coherence remains in diagnostics. To prevent a curved
follow-through from producing clustered events, Balanced now requires speed to
fall below 60% of the accepted peak before rearming; Deliberate uses 55% and
Gentle 65%. All event sources share a 300 ms output guard. The PC cue client now
has one ``beat`` sound and no measure/downbeat/time-signature state. Host tests
cover curved strokes, shallow follow-through ripples, and unmetered configuration.

Linear jitter filtering: replaced the 15 ms exponential speed filter with a
causal, recency-weighted 25 ms rolling window. Entries expire by timestamp, so
the filter remains bounded when sample spacing varies. At 416 Hz it spans about
eleven samples while weighting recent motion most heavily. A host regression adds
alternating 25 deg/s sample jitter to smooth 500 ms strokes and verifies one
event per gesture. Abrupt drops can still confirm immediately after the required
12% prominence. Smooth-loop processing was not changed by this update.

Gesture-baseline simplification: removed all beat prediction, tempo-dependent
acceptance, acceleration corroboration, and exclusive mode ownership. Linear
gestures are 3D angular-speed maxima with 12% prominence; smooth loops are net
3D quaternion turns. Both share only a wrap-safe 250 ms deduplication gate.
Tempo is computed after accepted gestures and cannot suppress detections. The
diagnostic ``predicted`` field remains for compatibility and is always zero.
Host tests cover 200 BPM, tempo doubling, shallow speed valleys, arbitrary IMU
rotation axes, and partial-turn reversal. Hardware validation is still required.

Orientation-based loop detection: smooth U-turns now integrate a quaternion from
all three gyro axes and compare net relative orientation with the preceding turn.
The axis can be any combination of IMU X/Y/Z, and a partial turn followed by its
reverse cancels rather than clustering multiple events. Loop mode locks only
after a complete near-180-degree turn, so it cannot prematurely suppress the
linear speed-maximum detector. Output retains the requested 250 ms duplicate
guard. Host regressions
cover an arbitrary diagonal axis, tempo acceleration, and repeated 100-degree
out-and-back rotations. Hardware false-trigger testing is still required.

Superseded candidate/rearm experiment: tempo-relative refractory and predictive
acceptance were evaluated and removed by the gesture-baseline simplification
above because they caused harmonic lockout and cross-source duplicates.

Peak-speed beats: the detector now reports each beat at the stroke's angular-speed
maximum, confirmed when speed falls below 85% of it, instead of waiting for speed
to brake below 30 deg/s. The BLE/console timestamp is the peak sample; the
detection time follows by the confirmation delay. Rearming now requires speed to
fall below the preset's fraction of the previous peak. Strength uses peak speed
rather than braking depth. Packet formats are unchanged. DK build and host tests
passed; hardware timing and false-trigger rate still need verification.

Manual recalibration: a completed double-click now resets the beat detector on
the main thread, preserves the selected sensitivity, and prompts for a one-second
still period. Bluetooth remains connected and raw motion continues. Double-click
selection uses the existing 600 ms collection window; triple-click still toggles BLE.

Saved-profile support: added the ``...def3`` read/write settings characteristic
and three detector sensitivity presets. Phone profiles select meter and voices;
the 12-byte beat notification remains compatible. DK build and detector host tests
passed, including Gentle acceptance and preset retention across sampling gaps.
No board wiring or hardware filter changes. Hardware and phone testing remain.


Conducting Mode transport: added a notify-only beat characteristic (``...def2``)
with a versioned 12-byte event packet. The MCU sends each fresh beat to a subscribed
phone at the console event boundary; no retries/backlog or changes to the motion
packet. DK build passed. Live BLE-to-audio timing requires testing with the updated
mobile app; neither firmware flashing nor phone installation was performed.


Latency experiment: increased polling from 50 ms to 20 ms and reduced angular-speed
smoothing from 30 ms to 15 ms. Raw sample prints remain disabled. The sensor ODR,
hardware filters, beat thresholds, and 50 ms BLE gate are unchanged; the gate now
normally produces 60 ms BLE spacing because it is checked on 20 ms polling cycles.
Host regressions include an ideal abrupt-stop test at the new polling rate.
Actual response delay and false-trigger rate require a hardware comparison.


Sensitivity tuning: lowered stroke arming from 40 to 30 deg/s and acceleration
corroboration from 120 to 80 mg. Braking now triggers below 55% of peak speed
(previously 45%); the 30 deg/s stop ceiling, 100 ms minimum stroke duration,
250 ms beat separation, and fresh-motion rearm requirement remain. BPM stability
rules are unchanged. This may recover gentle strokes but can increase false
triggers; validate with both conducting and ordinary movements. Host tests cover
35 deg/s, 100 mg strokes and small idle disturbances. Hardware improvement has
not yet been measured.

Stop-trigger console prototype: replaced rebound confirmation with stroke-braking
threshold detection and a fresh-motion rearm requirement. Prints ``BEAT:`` at the
future BLE event boundary. Host tests include two move-then-hold strokes with one
beat each. DK build and host regression tests passed; hardware detection accuracy
and latency still need validation. No flashing or audio/BLE event implementation.

Beat-console testing: disabled continuous acceleration/gyro sample printing via
``PRINT_MOTION_SAMPLES=0``. Calibration, beat/BPM, and error messages remain
enabled. Acquisition, BLE streaming, and detection logic are unchanged.

Conducting prototype (2026-10-26): added calibrated six-axis gravity estimation,
stroke/rebound candidate detection, conservative tempo reporting, and host tests.
NCS v3.4.0 DK build and host C regression tests passed. The macOS sanitizer run
did not complete; the successful host run used the macOS 14.4 SDK without
sanitizers. No firmware was flashed and no real conducting accuracy was measured.
Removed a pre-existing stray backtick in ``src/button.c`` that blocked compilation.
No overlay, BLE protocol, or polling-rate changes were made.

Streaming validation (2026-10-05): incremental DK and PCB builds passed with
NCS v3.4.0 and their respective overlays. Hardware timestamp spacing, sequence
continuity, UART impact, and notification delivery remain to be verified;
the firmware has not been flashed as part of this change.

* Increased BLE publication from 200 ms to a 50 ms target, using each fresh
  successful sample and acquisition timestamps; moved sample prints after
  publication and retained notification error reporting without retries.

* Replaced the MMA8451 implementation with LSM6DSOX six-axis polling through
  the Zephyr sensor API; migrated the associated overlays and driver config.
* Added gyroscope values and their validity flag to the existing BLE packet.
* Added six-axis serial-console output approximately every 50 ms and
  documented terminal settings, units, and basic hardware checks.
* Routed ``printk`` directly to UART to remove deferred-log batching; shortened
  sampling to 50 ms while preserving the 200 ms BLE update interval.
* Deferred IMU initialization until after a power-up wait and added read-only
  identity probes at both SA0 addresses when initialization fails.
* Enabled accelerometer hardware LPF2 at approximately 5.2 Hz in the IMU
  overlays and extended measurement settling to 500 ms. Documented filter
  controls, driver limitations, and before/after hardware checks.
* Restored per-press button messages using queued events and added the
  configured controller/pin to the startup message for troubleshooting.
* Identified missing DK Button 1 events as a flashed PCB mapping (pin 19).
  Documented explicit DK/PCB overlay selection and separate flash directories.
* Updated DK IMU wiring to match the PCB (SDA P0.27 / SCL P0.26), while
  keeping separate DK/PCB overlays, builds, button pins, and LED pins.
* Added three nonblocking 100 ms on/off LED flashes on phone connection and
  disconnection, followed by the latest connected/advertising/inactive state.
* Added a 60-second advertising timeout to save battery: stop unconnected
  advertising and breathing, retain live connections, and allow triple-press
  restart. Every successful advertising restart gets a fresh window.
* Made README updates a project development requirement.

Migration validation (2026-10-01): full DK and PCB firmware builds passed
with NCS v3.4.0. Host checks with mocked sensor I/O passed for six-axis unit
conversion, signs, rounding, saturation, initialization, and preservation of
the output on fetch/channel errors. Hardware readings have not yet been verified.

Serial-output validation (2026-10-01): the DK firmware build passed.

Startup-fix validation (2026-10-01): DK and PCB firmware builds passed.
Mocked host checks passed for waiting before driver initialization, settling
afterward, initialization errors, both identity probes, and repeated successful
initialization calls. The reported hardware failure remains unconfirmed until
this build is flashed and its startup output is checked.

Accelerometer-filter validation (2026-10-01): DK and PCB firmware builds passed;
both generated devicetrees select 52 Hz acceleration with ODR/10 LPF2. Host
checks passed for the 500 ms settling wait and initialization error handling.
Noise reduction and motion response still require comparison on hardware.

Button-diagnostic validation (2026-10-01): the DK build passed. Mocked host
checks passed for interrupt/debounce scheduling, per-press events, triple-click
collection, and long-hold click suppression. The reported missing triple-click
output still requires checking the new per-press messages on the DK.

Shared-I2C wiring validation (2026-10-01): clean DK and PCB builds passed.
Generated devicetrees confirm SDA P0.27 / SCL P0.26 in both active and sleep
states, with DK button/LED on P0.11/P0.03 and PCB button/LED on P0.19/P0.21.

LED-transition and advertising-timeout validation (2026-10-01): DK and PCB
firmware builds passed. Mocked host checks passed for three-pulse transitions,
steady LED states, rapid reconnects, preserving flashes across advertising
restarts, timeout expiry, protecting connected clients, fresh windows, stale
expiry work, stop-failure retry, and rejecting late connections after timeout.
Hardware timing and LED visibility still require testing on the device.

To check on hardware, triple-press without connecting a phone. After 60 seconds,
the console should print ``Advertising timed out after 60 seconds; Bluetooth
inactive. Triple-press to restart`` and the LED should go off. Triple-press again
and connect with a BLE client: three flashes should precede solid light, and the
connection should remain active past 60 seconds. Disconnect to see three flashes
and a fresh breathing/advertising window that expires after another 60 seconds.

Low-latency update validation: the NCS v3.4.0 nRF52840 DK build passed
(155,608 bytes flash, 26,360 bytes RAM). Generated configuration confirms
15 ms preferred interval, zero peripheral latency, 4 s supervision and the
5 s parameter-update delay. Conducting C tests and all 15 Python host tests
passed. No hardware was flashed and actual negotiated latency is unmeasured.
