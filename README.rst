Musical Baton
=============

Musical Baton is a Zephyr application for an nRF52840 development board
connected to an MMA8451 accelerometer. The current firmware reads motion
data over I2C and exposes it over Bluetooth Low Energy (BLE). It does not
yet generate music or interpret conducting gestures.

Current behavior
----------------

* Checks the accelerometer identity and enables measurement mode.
* Reads X, Y, and Z acceleration approximately every 200 milliseconds.
* Prints acceleration in milligravity (mg) to the serial console.
* Initializes Bluetooth without advertising after startup.
* Triple-pressing the user button toggles Bluetooth availability.
* When enabled, advertises as ``Smart Baton`` and accepts one connection.
* When disabled, stops advertising and disconnects the connected phone.
* Holding the user button for ten seconds reboots the baton.
* Allows a BLE client to read acceleration or subscribe to notifications.

Hardware and configuration
--------------------------

The supplied overlays configure an MMA8451 at I2C address ``0x1c`` with
SDA on ``P0.26`` and SCL on ``P0.27``. Connect sensor power and ground
according to the sensor module and board requirements.

The nRF52840 DK overlay maps the baton button to the development kit's
built-in Button 1 on ``P0.11``. This is a development-only mapping; the
custom baton PCB uses its external user button on ``P0.19``.

* ``nrf52840dk_nrf52840.overlay`` configures the nRF52840 DK.
* ``nrf21540dk_nrf52840.overlay`` provides configuration for the nRF21540 DK.
* ``prj.conf`` enables I2C, console output, logging, and BLE peripheral support.
* ``src/main.c`` initializes the modules and runs the sampling loop.
* ``src/sensor.c`` handles MMA8451 initialization and acceleration readings.
* ``src/bluetooth.c`` handles BLE advertising, reads, and notifications.
* ``src/button.c`` handles debouncing, click counting, and long holds.
* ``src/sensor.h`` and ``src/bluetooth.h`` declare the module interfaces.

Building and running
--------------------

Use an installed Nordic nRF Connect SDK and its matching toolchain. In the
nRF Connect extension for VS Code, open this directory as the application,
create a build configuration for your development board, and select the
matching overlay if it is not selected automatically. Build and flash the
connected board, then open its serial console to view sensor readings.

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

The MMA8451 development sensor fills the acceleration fields, sets bit 0, and
leaves all gyroscope fields at zero. The final LSM6DSOX implementation will
also set bit 1 and populate the gyroscope fields. Enable notifications on the
characteristic to receive updates.
