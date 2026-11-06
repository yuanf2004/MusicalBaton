#!/usr/bin/env python3
"""Play Musical Baton audio cues through a Windows PC."""

from __future__ import annotations

import argparse
import asyncio
import json
import queue
import re
import struct
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

SERVICE_UUID = "12345678-1234-5678-1234-56789abcdef0"
BEAT_UUID = "12345678-1234-5678-1234-56789abcdef2"
SETTINGS_UUID = "12345678-1234-5678-1234-56789abcdef3"
SENSITIVITY_IDS = {"balanced": 0, "deliberate": 1, "gentle": 2}


@dataclass(frozen=True)
class Cue:
    frequency_hz: int
    duration_ms: int


@dataclass(frozen=True)
class Config:
    device_name: str
    device_address: str | None
    serial_port: str | None
    serial_baud: int
    beats_per_measure: int
    bpm: float
    emphasize_beat_1: bool
    beat_1: Cue
    other_beat: Cue
    sensitivity: str


def _cue(raw: Any, name: str) -> Cue:
    if not isinstance(raw, dict):
        raise ValueError(f"{name} must be an object")
    frequency = int(raw.get("frequency_hz", 0))
    duration = int(raw.get("duration_ms", 0))
    if not 37 <= frequency <= 32767:
        raise ValueError(f"{name}.frequency_hz must be 37..32767")
    if not 1 <= duration <= 5000:
        raise ValueError(f"{name}.duration_ms must be 1..5000")
    return Cue(frequency, duration)


def load_config(path: Path) -> Config:
    raw = json.loads(path.read_text(encoding="utf-8"))
    meter = int(raw.get("beats_per_measure", 4))
    if meter not in (2, 3, 4):
        raise ValueError("beats_per_measure must be 2, 3, or 4")
    bpm = float(raw.get("bpm", 100))
    if not 20 <= bpm <= 300:
        raise ValueError("bpm must be 20..300")
    emphasize_beat_1 = raw.get("emphasize_beat_1", False)
    if not isinstance(emphasize_beat_1, bool):
        raise ValueError("emphasize_beat_1 must be true or false")
    sensitivity = str(raw.get("sensitivity", "balanced")).lower()
    if sensitivity not in SENSITIVITY_IDS:
        raise ValueError("sensitivity must be balanced, deliberate, or gentle")
    address = raw.get("device_address")
    serial_port = raw.get("serial_port")
    serial_baud = int(raw.get("serial_baud", 115200))
    if not 1200 <= serial_baud <= 4000000:
        raise ValueError("serial_baud must be 1200..4000000")
    return Config(
        device_name=str(raw.get("device_name", "Smart Baton")),
        device_address=str(address) if address else None,
        serial_port=str(serial_port) if serial_port else None,
        serial_baud=serial_baud,
        beats_per_measure=meter,
        bpm=bpm,
        emphasize_beat_1=emphasize_beat_1,
        beat_1=_cue(raw.get("beat_1"), "beat_1"),
        other_beat=_cue(raw.get("other_beat"), "other_beat"),
        sensitivity=sensitivity,
    )


def parse_beat(data: bytearray) -> tuple[int, int, int, int]:
    if len(data) != 12:
        raise ValueError(f"expected 12-byte beat packet, received {len(data)}")
    version, event_type, sequence, timestamp_ms, bpm_x10, strength, reserved = (
        struct.unpack("<BBHIHBB", data)
    )
    if version != 1 or event_type != 1 or reserved != 0:
        raise ValueError("unsupported beat packet")
    return sequence, timestamp_ms, bpm_x10, strength


BEAT_LINE = re.compile(
    r"^BEAT:\s+t=(?P<timestamp>\d+)\s+detected=(?P<detected>\d+)\s+"
    r"strength=(?P<strength>\d+)\s+BPM=(?P<bpm>\d+(?:\.\d+)?)\s*$"
)


def parse_console_beat(line: str) -> dict[str, int | float] | None:
    """Decode the BEAT line printed for conducting.c event boundaries."""
    match = BEAT_LINE.match(line)
    if match is None:
        return None
    return {
        "timestamp_ms": int(match.group("timestamp")),
        "detected_ms": int(match.group("detected")),
        "strength": int(match.group("strength")),
        "bpm": float(match.group("bpm")),
    }


class AudioWorker:
    def __init__(self) -> None:
        self._items: queue.Queue[Cue | None] = queue.Queue()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self) -> None:
        self._thread.start()

    def play(self, cue: Cue) -> None:
        self._items.put(cue)

    def close(self) -> None:
        self._items.put(None)
        self._thread.join()

    def _run(self) -> None:
        import winsound

        while (cue := self._items.get()) is not None:
            winsound.Beep(cue.frequency_hz, cue.duration_ms)


def cue_for_beat(config: Config, beat_number: int) -> Cue:
    if config.emphasize_beat_1 and beat_number == 1:
        return config.beat_1
    return config.other_beat


def run_local(config: Config) -> None:
    """Play the configured meter continuously without Bluetooth."""
    if sys.platform != "win32":
        raise RuntimeError("PC audio cues currently require Windows")
    audio = AudioWorker()
    audio.start()
    interval = 60.0 / config.bpm
    beat_number = 1
    next_beat = time.monotonic()
    print(
        f"Playing {config.beats_per_measure}/4 cues at {config.bpm:g} BPM; "
        "press Ctrl+C to stop"
    )
    try:
        while True:
            audio.play(cue_for_beat(config, beat_number))
            print(f"beat={beat_number}/{config.beats_per_measure}")
            beat_number = beat_number % config.beats_per_measure + 1
            next_beat += interval
            time.sleep(max(0.0, next_beat - time.monotonic()))
    finally:
        audio.close()


def find_serial_port(config: Config) -> str:
    if config.serial_port:
        return config.serial_port
    from serial.tools import list_ports

    ports = list(list_ports.comports())
    likely = [
        port
        for port in ports
        if any(
            marker in f"{port.description} {port.manufacturer}".lower()
            for marker in ("j-link", "jlink", "segger", "nrf")
        )
    ]
    if len(likely) == 1:
        return likely[0].device
    if len(ports) == 1:
        return ports[0].device
    available = ", ".join(
        f"{port.device} ({port.description})" for port in ports
    ) or "none"
    raise RuntimeError(
        "could not select the baton serial port; set serial_port in the JSON. "
        f"Available ports: {available}"
    )


def run_serial(config: Config) -> None:
    """Play cues from conducting.c beat events received over the DK USB UART."""
    if sys.platform != "win32":
        raise RuntimeError("PC audio cues currently require Windows")
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError("missing dependency; run: py -m pip install pyserial") from exc

    port = find_serial_port(config)
    audio = AudioWorker()
    audio.start()
    beat_number = 0
    print(
        f"Listening for conducting.c beats on {port} at {config.serial_baud} baud; "
        "press Ctrl+C to stop"
    )
    try:
        with serial.Serial(port, config.serial_baud, timeout=0.25) as connection:
            while True:
                line = connection.readline().decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                event = parse_console_beat(line)
                if event is None:
                    print(line)
                    continue
                beat_number = beat_number % config.beats_per_measure + 1
                audio.play(cue_for_beat(config, beat_number))
                print(
                    f"beat={beat_number}/{config.beats_per_measure} "
                    f"device_ms={event['timestamp_ms']} bpm={event['bpm']:.1f} "
                    f"strength={event['strength']}"
                )
    finally:
        audio.close()


async def find_baton(config: Config):
    from bleak import BleakScanner

    if config.device_address:
        device = await BleakScanner.find_device_by_address(config.device_address, timeout=10)
    else:
        device = await BleakScanner.find_device_by_filter(
            lambda device, advertisement: (
                device.name == config.device_name
                or config.device_name == advertisement.local_name
                or SERVICE_UUID
                in [uuid.lower() for uuid in (advertisement.service_uuids or [])]
            ),
            timeout=15,
        )
    if device is None:
        raise RuntimeError(
            "baton not found; triple-click it to advertise, then try again"
        )
    return device


async def run(config: Config) -> None:
    if sys.platform != "win32":
        raise RuntimeError("PC audio cues currently require Windows")
    try:
        from bleak import BleakClient
    except ImportError as exc:
        raise RuntimeError("missing dependency; run: py -m pip install bleak") from exc

    device = await find_baton(config)
    audio = AudioWorker()
    audio.start()
    beat_number = 0
    last_sequence: int | None = None

    def on_beat(_characteristic: Any, data: bytearray) -> None:
        nonlocal beat_number, last_sequence
        try:
            sequence, timestamp_ms, bpm_x10, strength = parse_beat(data)
        except ValueError as exc:
            print(f"Ignoring notification: {exc}", file=sys.stderr)
            return
        if last_sequence is not None and sequence != (last_sequence + 1) & 0xFFFF:
            print(f"Sequence gap ({last_sequence} -> {sequence}); next cue is beat 1")
            beat_number = 0
        last_sequence = sequence
        beat_number = beat_number % config.beats_per_measure + 1
        audio.play(cue_for_beat(config, beat_number))
        bpm = "--" if bpm_x10 == 0 else f"{bpm_x10 / 10:.1f}"
        print(
            f"beat={beat_number}/{config.beats_per_measure} sequence={sequence} "
            f"device_ms={timestamp_ms} bpm={bpm} strength={strength}"
        )

    try:
        async with BleakClient(device) as client:
            preset = SENSITIVITY_IDS[config.sensitivity]
            await client.write_gatt_char(
                SETTINGS_UUID, bytes((1, 1, preset, 0)), response=True
            )
            await client.start_notify(BEAT_UUID, on_beat)
            print(f"Connected to {device.name or device.address}; press Ctrl+C to stop")
            await asyncio.Event().wait()
    finally:
        audio.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        type=Path,
        default=Path(__file__).with_name("pc_audio_cues.json"),
        help="JSON profile (default: tools/pc_audio_cues.json)",
    )
    parser.add_argument(
        "--test", action="store_true", help="play the configured downbeat and regular cue"
    )
    parser.add_argument(
        "--ble",
        action="store_true",
        help="drive cues from Bluetooth instead of the default USB serial console",
    )
    parser.add_argument(
        "--demo",
        action="store_true",
        help="play synthetic cues at the configured tempo without an IMU",
    )
    args = parser.parse_args()
    try:
        config = load_config(args.config)
        if args.test:
            if sys.platform != "win32":
                raise RuntimeError("PC audio cues currently require Windows")
            audio = AudioWorker()
            audio.start()
            audio.play(config.beat_1)
            audio.play(config.other_beat)
            audio.close()
        elif args.ble:
            asyncio.run(run(config))
        elif args.demo:
            run_local(config)
        else:
            run_serial(config)
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nStopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
