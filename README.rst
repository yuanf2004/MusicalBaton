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
* Advertises as ``Smart Baton`` after successful sensor initialization.
* Allows a BLE client to read acceleration or subscribe to notifications.

Hardware and configuration
--------------------------

The supplied overlays configure an MMA8451 at I2C address ``0x1c`` with
SDA on ``P0.26`` and SCL on ``P0.27``. Connect sensor power and ground
according to the sensor module and board requirements.

* ``nrf52840dk_nrf52840.overlay`` configures the nRF52840 DK.
* ``nrf21540dk_nrf52840.overlay`` provides configuration for the nRF21540 DK.
* ``prj.conf`` enables I2C, console output, logging, and BLE peripheral support.
* ``src/main.c`` initializes the modules and runs the sampling loop.
* ``src/sensor.c`` handles MMA8451 initialization and acceleration readings.
* ``src/bluetooth.c`` handles BLE advertising, reads, and notifications.
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
``12345678-1234-5678-1234-56789abcdef0``. Its readable and notifiable
acceleration characteristic UUID is
``12345678-1234-5678-1234-56789abcdef1``.

Each value is a six-byte packet containing three signed 16-bit integers
in little-endian order, measured in mg:

* Bytes 0-1: X acceleration.
* Bytes 2-3: Y acceleration.
* Bytes 4-5: Z acceleration.

Enable notifications on the characteristic to receive updates. The
firmware converts raw readings using the sensor's default +/-2 g range.
