# Musical Baton firmware context

## Project purpose

This repository contains Zephyr/nRF Connect SDK firmware for a battery-powered
musical baton based on the nRF52840. The firmware reads motion data and exposes
it over Bluetooth Low Energy. Development currently uses an nRF52840 DK and an
LSM6DSOX breakout; the finished PCB also uses an LSM6DSOX IMU.

Treat the KiCad schematic as the source of truth for PCB wiring. The schematic
used to verify the pin map is currently located at:

`/Users/yuan/Documents/Smart Baton/L Smart Baton/L Smart Baton.kicad_sch`

Do not hard-code board-specific GPIO numbers in C modules. Application code
uses devicetree aliases, while overlays map those aliases to development or PCB
pins.

## Hardware configurations

### nRF52840 DK development setup

The normal DK build automatically loads `nrf52840dk_nrf52840.overlay`.

| Function | Development connection | Notes |
|---|---|---|
| User button | DK Button 1, P0.11 | Active-low; the button connects the pin to GND |
| Bluetooth LED | Arduino A0 / P0.03 | PWM output; LED anode goes through a 330 ohm to 1 kohm resistor to P0.03, cathode to GND |
| LSM6DSOX SDA | P0.27 | Matches PCB wiring |
| LSM6DSOX SCL | P0.26 | Matches PCB wiring |
| LSM6DSOX address | 0x6A | SA0 low; I2C fast mode |

P0.19 and P0.21 are connected to the DK's external QSPI flash by default and
are not routed for normal GPIO use at connector P24. Using P0.19 requires
cutting SB11 and shorting SB21. Using P0.21 requires cutting SB14 and shorting
SB24. The current development overlay avoids these changes.

The development setup has no RGB battery LED mapping. The RGB functions compile
as no-ops on the DK, while the Bluetooth LED remains fully functional.

### Custom Musical Baton PCB

Use `boards/musical_baton.overlay` for the custom PCB.

| Function | nRF52840 pin | Electrical behavior |
|---|---:|---|
| User button | P0.19 | Active-low to GND with internal pull-up |
| Charger status | P0.20 | Active-low open-collector signal with pull-up |
| Bluetooth LED | P0.21 | Active-high PWM output; LED cathode is grounded |
| RGB red | P0.22 | Active-low, common-anode RGB LED |
| RGB green | P0.23 | Active-low, common-anode RGB LED |
| RGB blue | P0.24 | Active-low, common-anode RGB LED |
| Battery sense | P0.02 / AIN0 | 1 Mohm / 1 Mohm divider with 100 nF capacitor |
| IMU INT1 | P0.13 | Active-high |
| IMU INT2 | P0.14 | Active-high |
| LSM6DSOX SCL | P0.26 | I2C fast mode |
| LSM6DSOX SDA | P0.27 | I2C fast mode |
| LSM6DSOX address | 0x6A | SA0 is tied to GND |
| Reset | P0.18 | Hardware reset |
| Low-frequency crystal | P0.00, P0.01 | 32.768 kHz crystal |
| SWD | Dedicated SWD pins | Programming and debugging |

The PCB's USB-C connector is used for power and charging, not USB data. The
power switch is a physical power-path switch and is not controlled by an MCU
GPIO.

## Devicetree aliases

C code depends on these logical names:

| Alias | Purpose |
|---|---|
| `baton-imu` | Six-axis LSM6DSOX IMU |
| `baton-button` | Main user button |
| `ble-led` | PWM-controlled Bluetooth status LED |
| `rgb-red-led` | Battery RGB red channel |
| `rgb-green-led` | Battery RGB green channel |
| `rgb-blue-led` | Battery RGB blue channel |

The DK overlay maps `baton-button` to P0.11 and `ble-led` to P0.03. The PCB
overlay maps them to P0.19 and P0.21. Keep application code independent of this
difference.

## Current firmware behavior

### Button

`src/button.c` uses GPIO interrupts on both edges.

- Debounce time: 30 ms
- Multi-click collection window: 600 ms
- Three quick presses toggle Bluetooth
- Holding for 10 seconds performs a cold reboot
- A detected press queues `BUTTON_EVENT_PRESSED`; the main loop prints
  `Baton button pressed`. Initialization prints the configured controller/pin.
- Completed click sequences are delivered as generic click-count events so
  more actions can be added later

Button callbacks enqueue events; slow work runs from the main loop rather than
inside the GPIO interrupt handler.

### Bluetooth

Bluetooth initializes after reboot but starts inactive and does not advertise.

- Triple-click while inactive starts advertising
- Triple-click while active stops advertising
- Deactivation also disconnects the connected phone
- An unexpected disconnect while Bluetooth remains active restarts advertising
- Advertising without a live connection times out after 60 seconds: advertising
  stops, the LED turns off, and Bluetooth becomes inactive. Triple-click opens
  another window. Connecting cancels expiry; disconnecting starts a new window.
  Stored pairing/bonding does not count as a live connection.
- The custom GATT service publishes a fixed, versioned 20-byte motion packet

Motion packet version 1, in little-endian order:

- Byte 0: version
- Byte 1: flags (acceleration valid, gyroscope valid, time synchronized)
- Bytes 2-3: sequence number
- Bytes 4-7: nRF uptime in milliseconds
- Bytes 8-13: accelerometer X/Y/Z as signed 16-bit mg values
- Bytes 14-19: gyroscope X/Y/Z as signed 16-bit values in 0.1 degrees/second
- Successful LSM6DSOX reads populate all six axes and set acceleration-valid
  and gyroscope-valid flags. Time synchronization is not implemented.

Current UUIDs:

- Service: `12345678-1234-5678-1234-56789abcdef0`
- Motion characteristic: `12345678-1234-5678-1234-56789abcdef1`

### LEDs

`src/led.c` owns LED behavior.

Bluetooth LED states:

- Bluetooth inactive: off
- Advertising: PWM breathing fade
- Connected: three 100 ms on/off flashes, then solid on
- Leaving connected: three 100 ms on/off flashes, then breathing if advertising
  or off if inactive. Bluetooth callbacks request states; delayed LED work
  animates the 600 ms transition without delaying connection/disconnection.
  Repeated states and advertising restarts preserve ongoing disconnect flashes.

The current fade uses 25 brightness steps at 60 ms per step. It takes 1.5
seconds to fade on and 1.5 seconds to fade off, for a three-second complete
cycle.

Battery RGB states are prepared for future battery-monitor code:

- Charging: blue
- Above 50%: green
- 20% through 50%: yellow
- Below 20%: red
- Off: all channels off

The PCB RGB LED is common-anode, and devicetree polarity handles its active-low
channels.

### Sensor status

`src/sensor.c` uses the Zephyr sensor API and the `baton-imu` devicetree alias.
NCS v3.4.0 uses `st,lsm6dso` / `CONFIG_LSM6DSO` for basic LSM6DSOX motion
registers (matching WHO_AM_I value 0x6C). Both DK and PCB overlays configure
52 Hz acceleration and gyro, +/-4 g and +/-1000 degrees/second ranges.
Accelerometer LPF2 uses ODR/10 (approximately 5.2 Hz) via `accel-lp-filter`;
all console/BLE acceleration samples are filtered in hardware. Gyroscope filter
settings remain at the driver defaults.
The application polls and publishes fresh successful samples every 50 ms as a target;
interrupts and FIFO are not used. Named intervals live in `src/main.c`. Packet
timestamps are captured immediately after sensor fetch, before conversion,
printing, and notification submission. Notifications precede sample prints;
failed notifications are reported without retries or catch-up bursts.
Successful six-axis samples print to the 115200-baud serial console approximately
every 50 ms, one sample per line, even when Bluetooth is inactive.
`CONFIG_LOG_PRINTK=n` routes prints directly to UART to avoid deferred-log batching.
IMU nodes use `zephyr,deferred-init`; `sensor_init()` waits 100 ms before
calling `device_init()` and another 500 ms after initialization for filter/measurement
settling (20 accelerometer samples at 52 Hz take approximately 385 ms). Revisit
this wait when adjusting ODR or filtering. Initialization failure prints identity probes at both SA0 addresses. Readings are converted to mg and
0.1 degrees/second, rounded, and clamped to signed 16 bits. Failed reads leave
the output unchanged and are not published over BLE.

DK and PCB both use SDA P0.27 / SCL P0.26. Their button and LED mappings
remain distinct; always build/flash with the matching overlay/build directory.
Hardware communication and accuracy must be verified on a connected LSM6DSOX.

## Important files

- `src/main.c`: initialization and main event/sample loop
- `src/button.c`, `src/button.h`: interrupt-driven button event module
- `src/bluetooth.c`, `src/bluetooth.h`: BLE state and six-axis motion service
- `src/led.c`, `src/led.h`: Bluetooth PWM LED and battery RGB states
- `src/sensor.c`, `src/sensor.h`: LSM6DSOX six-axis sensor module
- `nrf52840dk_nrf52840.overlay`: development-kit wiring
- `boards/musical_baton.overlay`: final PCB wiring
- `prj.conf`: Zephyr features
- `CMakeLists.txt`: application source list

## Building

Run builds from an nRF Connect SDK terminal configured for NCS v3.4.0.

Development kit:

```sh
west build --no-sysbuild -p always -d build-lsm6dsox-dk -b nrf52840dk/nrf52840 -- \
  -DDTC_OVERLAY_FILE=nrf52840dk_nrf52840.overlay
```

Custom PCB while it still reuses the DK board definition:

```sh
west build --no-sysbuild -p always -d build-lsm6dsox-pcb -b nrf52840dk/nrf52840 -- \
  -DDTC_OVERLAY_FILE=boards/musical_baton.overlay
```

Flash the DK build explicitly (use `-d build-lsm6dsox-pcb` for the PCB):

```sh
west flash -d build-lsm6dsox-dk
```

A future improvement is to create a real `musical_baton/nrf52840` board target
so the PCB hardware description is selected automatically.

The DK startup button message must report pin 11. Pin 19 identifies PCB
button wiring; both builds use the same board target, so verify the overlay
and build directory before flashing.

## Development rules

- Preserve the separation between reusable application behavior and pin mapping.
- Put pin changes in the appropriate overlay instead of C source files.
- Preserve active-low/active-high flags from the schematic.
- Keep interrupt callbacks short and defer work to queues or work items.
- Keep Bluetooth inactive by default after every reboot unless product behavior
  is intentionally changed.
- Build the DK target after firmware changes.
- Validate the PCB overlay after hardware-description changes.
- Verify sensor communication and motion readings on hardware; successful builds
  alone do not verify electrical wiring or accuracy.
- Update `README.rst` with every project change to reflect current behavior,
  wiring, build instructions, and limitations.
