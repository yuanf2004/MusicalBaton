Musical Baton
=============

Musical Baton is a Zephyr application for an nRF52840 development board
connected to an LSM6DSOX six-axis IMU. The current firmware reads motion
data over I2C and exposes it over Bluetooth Low Energy (BLE). It does not
yet generate music or interpret conducting gestures.

Current behavior
----------------

* Uses the Zephyr LSM6DSO driver to check the LSM6DSOX identity and configure
  both accelerometer and gyroscope.
* Configures both sensors at 52 Hz, with acceleration range +/-4 g and
  gyroscope range +/-1000 degrees/second.
* Enables the accelerometer hardware LPF2 at ODR/10 (approximately 5.2 Hz),
  smoothing all three acceleration axes before console and BLE output.
* Polls the latest X/Y/Z acceleration and angular velocity approximately every
  50 milliseconds (20 console samples/second; intermediate IMU samples are discarded).
  BLE publishes the latest sample approximately every 200 ms (5 updates/second).
* Prints initialization and error messages, plus the latest six-axis IMU
  readings approximately every 50 ms, one sample per line, to the serial console even when
  Bluetooth is inactive.
* Initializes Bluetooth without advertising after startup.
* Each debounced button press prints ``Baton button pressed``.
* Triple-pressing the user button toggles Bluetooth availability.
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
connected board, then open its serial console to view initialization, errors, and IMU readings.

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
local uptime taken after the sensor fetch. The packet version, UUIDs, and
20-byte layout are unchanged. Failed sensor reads are not published. Enable
notifications on the characteristic to receive updates.

Hardware verification
---------------------

After flashing with the LSM6DSOX connected, check for the ``chip id 0x6c``
and ``LSM6DSOX ready`` startup messages. Triple-press to enable Bluetooth,
connect a BLE client, and subscribe to the motion characteristic. Confirm
``flags = 0x03``, about 1000 mg along the gravity axis at rest, and gyroscope
values near zero. Rotate the board to verify angular velocity changes on all
three axes. Builds verify compilation and devicetree configuration; actual
sensor communication and motion accuracy still require this hardware check.

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
timing. The 50 ms IMU output continues during button handling.

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

Current overlays run both sensors at 52 Hz in their default high-performance
mode, with accelerometer LPF2 enabled at ODR/10 = 5.2 Hz. Gyroscope filter
configuration stays at the driver defaults. Without LPF2, the accelerometer
LPF1 bandwidth is ODR/2 = 26 Hz. Accelerometer LPF2 options include
ODR/4 = 13 Hz, ODR/10 = 5.2 Hz, and ODR/20 = 2.6 Hz. Stronger filtering
reduces fast noise but also smooths intended motion and changes timing.

The installed NCS v3.4.0 Zephyr LSM6DSO driver exposes ``accel-lp-filter``.
All supplied IMU overlays now configure::

   accel-lp-filter = <LSM6DSO_DT_LP_ODR_DIV_10>;

At 52 Hz this selects approximately 5.2 Hz. The driver's zero/default value
(``LSM6DSO_DT_LP_ODR_DIV_2``) skips enabling LPF2; it must not be treated as
an explicit ODR/4 LPF2 setting despite the hardware's zero cutoff encoding.
The driver has no matching gyroscope filter property or filter attribute;
those controls would require an explicit driver extension or checked register
updates after driver initialization. Verify against the installed driver,
not just a newer online binding.

AN5272 lists up to 20 sensor samples for ODR/10 or ODR/20 settling,
approximately 385 ms at 52 Hz. ``sensor_init()`` now waits 500 ms after driver
initialization before allowing reads. Revisit this wait when changing sensor
ODR or the filter cutoff, especially for stronger filtering.

For baton testing, start with low-pass filtering and compare stationary noise
and quick conducting motions before and after, including peak magnitude and
response delay. High-pass filtering removes DC/gravity information, so it is
not a default replacement for acceleration used for tilt/orientation. Filtering
also does not calibrate gyro zero-rate bias. The firmware reads at 20 Hz and
BLE publishes at about 5 Hz; evaluate bandwidth against the rate of the data
actually consumed, or increase acquisition/publication rates for faster motions.
The hardware filter applies to all acceleration samples sent to the terminal
and BLE; no software averaging is added. A low-pass filter preserves the
steady gravity signal. To compare against the default, omit ``accel-lp-filter``
from the selected overlay, rebuild, flash, and repeat the same stationary and
motion checks.

Troubleshooting IMU initialization
----------------------------------

The IMU nodes use ``zephyr,deferred-init``. ``sensor_init()`` waits 100 ms
before the first driver transaction, then waits another 500 ms after successful
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

A successful sample is printed approximately every 50 ms, without needing
to enable Bluetooth::

   IMU accel [mg]: X=12 Y=-8 Z=1001 | gyro [0.1 deg/s]: X=0 Y=-1 Z=2

Acceleration is in mg (1000 mg = 1 g). Gyroscope integers are in tenths of a
degree/second: ``-1`` means -0.1 deg/s and ``25`` means 2.5 deg/s. At rest,
the acceleration vector should have magnitude near 1000 mg and gyro values
should be near zero; tilt and rotate the board to check changes. The main
sampling loop runs approximately every 50 ms, while BLE updates remain at
approximately 200 ms intervals. ``CONFIG_LOG_PRINTK=n`` sends each ``printk``
directly to UART instead of collecting it in the deferred logger for up to a
second. Each loop prints one fresh sample and uses a new 50 ms deadline,
without a burst of catch-up prints. Failed reads print
an error instead of a stale sample. Timing may be delayed by other main-loop
work. Exit ``screen`` with Ctrl-A, then K, then Y.

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
