#!/usr/bin/env python3
"""Read-only check of the PCB's resolved wiring/configuration and Intel HEX."""
import argparse
import hashlib
import os
from pathlib import Path
import sys

parser = argparse.ArgumentParser()
parser.add_argument("build", type=Path)
parser.add_argument("--zephyr-base", default=os.environ.get("ZEPHYR_BASE"), required=not os.environ.get("ZEPHYR_BASE"))
args = parser.parse_args()
sys.path.insert(0, str(Path(args.zephyr_base) / "scripts/dts/python-devicetree/src"))
from devicetree import dtlib
root = args.build / "zephyr"
dt = dtlib.DT(str(root / "zephyr.dts"))
config = (root / ".config").read_text().splitlines()
def enabled(name): return name + "=y" in config
def nums(node, key):
    # dtlib resolves phandles into cells but distinguishes them from plain ints.
    raw = node.props[key].value
    assert len(raw) % 4 == 0
    return [int.from_bytes(raw[i:i+4], "big") for i in range(0, len(raw), 4)]
assert "Musical Baton" in dt.root.props["model"].to_string()
gpio = dt.label2node["gpio0"]
reserved = nums(gpio, "gpio-reserved-ranges")
assert reserved == [0, 2, 18, 1]
for alias, port, pin, flags in [("baton-button", "gpio0", 19, 17), ("rgb-red-led", "gpio1", 0, 1), ("rgb-green-led", "gpio0", 23, 1), ("rgb-blue-led", "gpio0", 25, 1)]:
    value = nums(dt.alias2node[alias], "gpios")
    assert value == [dt.label2node[port].props["phandle"].to_num(), pin, flags], (alias, value)
    if port == "gpio0":
        assert not any(start <= pin < start + count for start, count in zip(reserved[::2], reserved[1::2]))
imu = dt.alias2node["baton-imu"]
assert nums(imu, "reg") == [0x6a]
assert "zephyr,deferred-init" in imu.props
assert nums(imu, "accel-odr") == nums(imu, "gyro-odr") == [6]
# Nordic PSEL packs function into high bits and pin/port into low 9 bits.
i2c_pins = [v & 0x1ff for v in nums(dt.label2node["baton_i2c0_default"].nodes["group1"], "psels")]
pwm_pins = [v & 0x1ff for v in nums(dt.label2node["baton_pwm0_default"].nodes["group1"], "psels")]
assert i2c_pins == [27, 26] and pwm_pins == [6]
assert nums(dt.alias2node["ble-led"], "pwms")[1:] == [0, 1000000, 0]
for label in ["qspi", "spi0", "spi1", "spi2", "spi3", "uart0", "uart1", "nfct", "pwm_led0", "led0", "led1", "led2", "led3", "button0", "button1", "button2", "button3"]:
    assert dt.label2node[label].props["status"].to_string() == "disabled", label
assert "gpio-as-nreset" in dt.label2node["uicr"].props
assert enabled("CONFIG_RTT_CONSOLE") and enabled("CONFIG_LOG_BACKEND_RTT_MODE_DROP")
assert not enabled("CONFIG_UART_CONSOLE") and not enabled("CONFIG_BOOTLOADER_MCUBOOT")
assert enabled("CONFIG_CLOCK_CONTROL_NRF_K32SRC_XTAL")
assert "CONFIG_FLASH_LOAD_OFFSET=0" in config
assert not enabled("CONFIG_NRF_APPROTECT_LOCK")
image = root / "zephyr.hex"
memory = {}; base = 0; eof = False
for line in image.read_text().splitlines():
    assert line.startswith(":")
    data = bytes.fromhex(line[1:]); assert sum(data) % 256 == 0
    length, address, kind = data[0], int.from_bytes(data[1:3], "big"), data[3]
    assert len(data) == length + 5
    payload = data[4:-1]
    if kind == 4: base = int.from_bytes(payload, "big") << 16
    elif kind == 2: base = int.from_bytes(payload, "big") << 4
    elif kind == 0:
        for offset, value in enumerate(payload):
            pos = base + address + offset
            assert pos < 0x100000 or 0x10001000 <= pos < 0x10002000
            assert pos not in memory or memory[pos] == value
            memory[pos] = value
    elif kind == 1: eof = True
    else: assert kind in (3, 5), kind
assert eof
stack = int.from_bytes(bytes(memory[i] for i in range(4)), "little")
reset = int.from_bytes(bytes(memory[i] for i in range(4, 8)), "little")
assert 0x20000000 <= stack <= 0x20040000
assert reset & 1 and (reset & ~1) in memory
print("PCB pin reservations, aliases, polarity, IMU, disabled DK peripherals, RTT, crystal and standalone boot checks passed")
print("HEX checksum/address/vector checks passed:", image.resolve())
print("SHA256:", hashlib.sha256(image.read_bytes()).hexdigest())
print("Address span:", hex(min(memory)), "..", hex(max(memory)))
print("Initial SP:", hex(stack), "Reset vector:", hex(reset))
