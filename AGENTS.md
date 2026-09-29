# Musical Baton firmware context

## Project purpose

This repository contains Zephyr/nRF Connect SDK firmware for a battery-powered
musical baton based on the nRF52840. The firmware reads motion data and exposes
it over Bluetooth Low Energy. Development currently uses an nRF52840 DK and an
MMA8451 breakout; the finished PCB uses an LSM6DSOX IMU.

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
| MMA8451 SDA | P0.26 | Development sensor only |
| MMA8451 SCL | P0.27 | Development sensor only |
| MMA8451 address | 0x1C | Current `sensor.c` target |

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
- A detected press prints `Baton button pressed`
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
- The custom GATT service publishes X/Y/Z acceleration as three little-endian
  signed 16-bit values in milligravity units

Current UUIDs:

- Service: `12345678-1234-5678-1234-56789abcdef0`
- Acceleration characteristic: `12345678-1234-5678-1234-56789abcdef1`

### LEDs

`src/led.c` owns LED behavior.

Bluetooth LED states:

- Bluetooth inactive: off
- Advertising: PWM breathing fade
- Connected: solid on

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

`src/sensor.c` currently talks directly to an MMA8451 for development testing.
It expects a devicetree node labeled `mma8451`.

The PCB overlay describes the final LSM6DSOX, but the sensor module has not yet
been migrated to it. A full PCB firmware build will remain incomplete until the
sensor implementation supports the LSM6DSOX. The PCB overlay itself has already
passed devicetree configuration validation.

## Important files

- `src/main.c`: initialization and main event/sample loop
- `src/button.c`, `src/button.h`: interrupt-driven button event module
- `src/bluetooth.c`, `src/bluetooth.h`: BLE state and acceleration service
- `src/led.c`, `src/led.h`: Bluetooth PWM LED and battery RGB states
- `src/sensor.c`, `src/sensor.h`: current MMA8451 test implementation
- `nrf52840dk_nrf52840.overlay`: development-kit wiring
- `boards/musical_baton.overlay`: final PCB wiring
- `prj.conf`: Zephyr features
- `CMakeLists.txt`: application source list

## Building

Run builds from an nRF Connect SDK terminal configured for NCS v3.4.0.

Development kit:

```sh
west build --no-sysbuild -p always -b nrf52840dk/nrf52840
```

Custom PCB while it still reuses the DK board definition:

```sh
west build --no-sysbuild -p always -b nrf52840dk/nrf52840 -- \
  -DDTC_OVERLAY_FILE=boards/musical_baton.overlay
```

Flash the most recent build:

```sh
west flash
```

A future improvement is to create a real `musical_baton/nrf52840` board target
so the PCB hardware description is selected automatically.

## Development rules

- Preserve the separation between reusable application behavior and pin mapping.
- Put pin changes in the appropriate overlay instead of C source files.
- Preserve active-low/active-high flags from the schematic.
- Keep interrupt callbacks short and defer work to queues or work items.
- Keep Bluetooth inactive by default after every reboot unless product behavior
  is intentionally changed.
- Build the DK target after firmware changes.
- Validate the PCB overlay after hardware-description changes.
- Do not assume the PCB sensor code works merely because the DK build succeeds;
  the MMA8451-to-LSM6DSOX migration is still outstanding.
