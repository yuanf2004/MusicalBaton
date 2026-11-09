#!/usr/bin/env python3
"""Play Musical Baton audio cues through a Windows PC."""

from __future__ import annotations

import argparse
import array
import asyncio
import csv
import json
import math
import queue
import re
import struct
import sys
import threading
import time
from collections import Counter, deque
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
    bpm: float
    beat: Cue
    sensitivity: str
    strength_threshold: int


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
    bpm = float(raw.get("bpm", 100))
    if not 20 <= bpm <= 300:
        raise ValueError("bpm must be 20..300")
    sensitivity = str(raw.get("sensitivity", "balanced")).lower()
    if sensitivity not in SENSITIVITY_IDS:
        raise ValueError("sensitivity must be balanced, deliberate, or gentle")
    strength_threshold = raw.get("strength_threshold", 50)
    if (isinstance(strength_threshold, bool) or
            not isinstance(strength_threshold, int) or
            not 0 <= strength_threshold <= 99):
        raise ValueError("strength_threshold must be an integer from 0 through 99")
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
        bpm=bpm,
        beat=_cue(raw.get("beat"), "beat"),
        sensitivity=sensitivity,
        strength_threshold=strength_threshold,
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
    r"strength=(?P<strength>\d+)\s+BPM=(?P<bpm>\d+(?:\.\d+)?)"
    r"(?:\s+predicted=(?P<predicted>[01]))?\s*$"
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
        "predicted": match.group("predicted") == "1",
    }


def normalize_diagnostic_fields(fields: list[str]) -> list[str] | None:
    """Normalize old and current firmware CD records to the current CSV schema."""
    if len(fields) == 19:
        return fields[1:]
    if len(fields) == 18:
        return fields[1:] + [""]
    if len(fields) == 17:
        return fields[1:] + ["", ""]
    if len(fields) == 15:
        return fields[1:] + ["", "", "", ""]
    if len(fields) == 12:
        # Older firmware did not print the three raw accelerometer axes.
        return fields[1:5] + ["", "", ""] + fields[5:] + ["", "", "", ""]
    return None


class AudioWorker:
    def __init__(self) -> None:
        self._items: queue.Queue[Cue | None] = queue.Queue(maxsize=1)
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._stream = None
        self._lock = threading.Lock()
        self._active_pcm: bytes | None = None
        self._active_offset = 0
        self._cache: dict[Cue, bytes] = {}
        self._sample_rate = 48000

    def start(self) -> None:
        try:
            import sounddevice

            default_output = sounddevice.default.device[1]
            default_name = sounddevice.query_devices(default_output)["name"]
            output_device = default_output
            for index, device in enumerate(sounddevice.query_devices()):
                host_name = sounddevice.query_hostapis(device["hostapi"])["name"]
                if (device["max_output_channels"] > 0 and
                        host_name == "Windows WASAPI" and
                        device["name"] == default_name):
                    output_device = index
                    break
            self._stream = sounddevice.RawOutputStream(
                samplerate=self._sample_rate, channels=1, dtype="int16",
                latency="low", blocksize=128, callback=self._audio_callback,
                device=output_device
            )
            self._stream.start()
            selected = sounddevice.query_devices(output_device)
            host = sounddevice.query_hostapis(selected["hostapi"])["name"]
            print(f"PC audio backend: sounddevice callback, {host}, "
                  f"{selected['name']} "
                  f"({self._stream.latency * 1000:.1f} ms reported latency)")
        except Exception as exc:
            # Keep the no-extra-dependency path available. PortAudio device
            # failures also fall back rather than stopping beat collection.
            self._stream = None
            print(f"PC audio backend: winsound ({exc}); "
                  "install/configure sounddevice for lower latency")
            self._thread.start()

    def _pcm(self, cue: Cue) -> bytes:
        pcm = self._cache.get(cue)
        if pcm is not None:
            return pcm
        count = max(1, self._sample_rate * cue.duration_ms // 1000)
        attack = max(1, self._sample_rate // 1000)
        release = max(1, self._sample_rate * 3 // 1000)
        samples = array.array("h")
        for index in range(count):
            envelope = min(1.0, index / attack, (count - index) / release)
            samples.append(int(12000 * envelope * math.sin(
                2 * math.pi * cue.frequency_hz * index / self._sample_rate
            )))
        pcm = samples.tobytes()
        self._cache[cue] = pcm
        return pcm

    def _audio_callback(self, output, frames, timing, status) -> None:
        del frames, timing, status
        output[:] = bytes(len(output))
        with self._lock:
            if self._active_pcm is None:
                return
            count = min(len(output), len(self._active_pcm) - self._active_offset)
            output[:count] = self._active_pcm[
                self._active_offset:self._active_offset + count
            ]
            self._active_offset += count
            if self._active_offset == len(self._active_pcm):
                self._active_pcm = None
                self._active_offset = 0

    def play(self, cue: Cue) -> None:
        if self._stream is not None:
            with self._lock:
                # A new beat replaces any unfinished click immediately.
                self._active_pcm = self._pcm(cue)
                self._active_offset = 0
            return
        self.clear()
        self._items.put_nowait(cue)

    def clear(self) -> None:
        if self._stream is not None:
            with self._lock:
                self._active_pcm = None
                self._active_offset = 0
            return
        try:
            self._items.get_nowait()
        except queue.Empty:
            pass

    def close(self) -> None:
        if self._stream is not None:
            self.clear()
            self._stream.stop()
            self._stream.close()
            self._stream = None
            return
        self.clear()
        self._items.put_nowait(None)
        self._thread.join()

    def _run(self) -> None:
        import winsound

        while (cue := self._items.get()) is not None:
            winsound.Beep(cue.frequency_hz, cue.duration_ms)


def should_play_beat(config: Config, strength: int) -> bool:
    """Only audible beats strictly exceed the configured strength threshold."""
    return strength > config.strength_threshold


class GestureClassifier:
    """PC-side direction classifier used only to gate embedded peak events."""

    def __init__(self, model_path: Path) -> None:
        try:
            import joblib
            import numpy as np
        except ImportError as exc:
            raise RuntimeError(
                "peak-ml requires numpy, joblib, and scikit-learn; install "
                "them in the project virtual environment"
            ) from exc
        artifact = joblib.load(model_path)
        self._np = np
        self._model = artifact["model"]
        self._window_ms = int(artifact.get("window_ms", 200))
        self._expected_features = list(artifact["feature_names"])
        self._samples: deque[tuple[int, list[float]]] = deque()
        self._votes: deque[str] = deque(maxlen=int(artifact.get("smoothing_windows", 5)))
        self.label: str | None = None

    @staticmethod
    def _baseline_features(np, values):
        accel_norm = np.linalg.norm(values[:, :3], axis=1)
        gyro_norm = np.linalg.norm(values[:, 3:], axis=1)
        channels = np.column_stack((values, accel_norm, gyro_norm))
        features = []
        for channel in channels.T:
            features.extend((channel.mean(), channel.std(), channel.min(),
                             channel.max(), np.sqrt(np.mean(channel * channel)),
                             np.ptp(channel)))
        return np.asarray(features, dtype=np.float64)

    def add_diagnostic(self, diagnostic: list[str]) -> str | None:
        timestamp = int(diagnostic[0])
        # Training features use acceleration XYZ followed by gyro XYZ.
        values = [float(diagnostic[index]) for index in (4, 5, 6, 1, 2, 3)]
        self._samples.append((timestamp, values))
        while self._samples and timestamp - self._samples[0][0] > self._window_ms:
            self._samples.popleft()
        if len(self._samples) < 6 or timestamp - self._samples[0][0] < self._window_ms - 30:
            return self.label
        matrix = self._np.asarray([sample for _, sample in self._samples])
        features = self._baseline_features(self._np, matrix)
        if len(features) != len(self._expected_features):
            raise RuntimeError(
                "the selected model does not use the baseline 48-feature set; "
                "select models/gesture_v1/gesture_tree.joblib"
            )
        prediction = str(self._model.predict(features.reshape(1, -1))[0])
        self._votes.append(prediction)
        self.label = Counter(self._votes).most_common(1)[0][0]
        return self.label


def run_local(config: Config) -> None:
    """Play the configured meter continuously without Bluetooth."""
    if sys.platform != "win32":
        raise RuntimeError("PC audio cues currently require Windows")
    audio = AudioWorker()
    audio.start()
    interval = 60.0 / config.bpm
    next_beat = time.monotonic()
    print(f"Playing unmetered cues at {config.bpm:g} BPM; press Ctrl+C to stop")
    try:
        while True:
            audio.play(config.beat)
            print("beat")
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


def run_serial(config: Config, record_path: Path | None = None,
               detector: str = "peak", model_path: Path | None = None) -> None:
    """Play cues from conducting.c beat events received over the DK USB UART."""
    if sys.platform != "win32":
        raise RuntimeError("PC audio cues currently require Windows")
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError("missing dependency; run: py -m pip install pyserial") from exc

    port = find_serial_port(config)
    classifier = GestureClassifier(model_path) if detector == "peak-ml" else None
    audio = AudioWorker()
    audio.start()
    record_file = None
    record_writer = None
    beat_file = None
    beat_writer = None
    quiet_start_ms = None
    last_record_flush_ms = None
    if record_path is not None:
        record_file = record_path.open("w", newline="", encoding="utf-8")
        record_writer = csv.writer(record_file)
        record_writer.writerow(
            ("timestamp_ms", "gyro_x_dps_tenths", "gyro_y_dps_tenths",
             "gyro_z_dps_tenths", "accel_x_mg", "accel_y_mg", "accel_z_mg",
             "speed_dps_tenths", "linear_accel_mg",
             "armed", "release_dps_tenths", "prediction_valid",
             "prediction_emitted", "predicted_beat_ms",
             "direction_phase_degrees_tenths",
             "direction_rate_dps_tenths", "motion_mode",
             "linear_direction_coherence_milli")
        )
        beat_path = record_path.with_name(record_path.stem + "-beats.csv")
        beat_file = beat_path.open("w", newline="", encoding="utf-8")
        beat_writer = csv.writer(beat_file)
        beat_writer.writerow(
            ("timestamp_ms", "emitted_ms", "bpm", "strength", "predicted")
        )
        print(f"Recording conducting diagnostics to {record_path}")
        print(f"Recording emitted beats to {beat_path}")
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
                if line.startswith("CD,"):
                    fields = line.split(",")
                    diagnostic = normalize_diagnostic_fields(fields)
                    if diagnostic is not None:
                        timestamp_ms = int(diagnostic[0])
                        speed_dps = int(diagnostic[7]) * 0.1
                        if speed_dps <= 5.0:
                            if quiet_start_ms is None:
                                quiet_start_ms = timestamp_ms
                            elif timestamp_ms - quiet_start_ms >= 100:
                                audio.clear()
                        else:
                            quiet_start_ms = None
                        if classifier is not None:
                            classifier.add_diagnostic(diagnostic)
                    if record_writer is not None and diagnostic is not None:
                        record_writer.writerow(diagnostic)
                        timestamp_ms = int(diagnostic[0])
                        if (last_record_flush_ms is None or
                                timestamp_ms - last_record_flush_ms >= 1000):
                            record_file.flush()
                            last_record_flush_ms = timestamp_ms
                    continue
                event = parse_console_beat(line)
                if event is None:
                    print(line)
                    continue
                if beat_writer is not None:
                    beat_writer.writerow(
                        (event["timestamp_ms"], event["detected_ms"], event["bpm"],
                         event["strength"], int(event["predicted"]))
                    )
                    beat_file.flush()
                if not should_play_beat(config, int(event["strength"])):
                    print(
                        f"ignored beat: device_ms={event['timestamp_ms']} "
                        f"strength={event['strength']} "
                        f"threshold={config.strength_threshold}"
                    )
                    continue
                if classifier is not None and classifier.label in (None, "idle"):
                    print(
                        f"ignored beat: device_ms={event['timestamp_ms']} "
                        f"ML direction={classifier.label or 'warming-up'}"
                    )
                    continue
                audio.play(config.beat)
                direction = (f" direction={classifier.label}"
                             if classifier is not None else "")
                print(
                    f"beat device_ms={event['timestamp_ms']} bpm={event['bpm']:.1f} "
                    f"strength={event['strength']} predicted={event['predicted']}"
                    f"{direction}"
                )
    finally:
        audio.close()
        if record_file is not None:
            record_file.close()
        if beat_file is not None:
            beat_file.close()


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
    last_sequence: int | None = None

    def on_beat(_characteristic: Any, data: bytearray) -> None:
        nonlocal last_sequence
        try:
            sequence, timestamp_ms, bpm_x10, strength = parse_beat(data)
        except ValueError as exc:
            print(f"Ignoring notification: {exc}", file=sys.stderr)
            return
        if last_sequence is not None and sequence != (last_sequence + 1) & 0xFFFF:
            print(f"Sequence gap ({last_sequence} -> {sequence})")
        last_sequence = sequence
        if not should_play_beat(config, strength):
            print(
                f"ignored beat: sequence={sequence} device_ms={timestamp_ms} "
                f"strength={strength} threshold={config.strength_threshold}"
            )
            return
        audio.play(config.beat)
        bpm = "--" if bpm_x10 == 0 else f"{bpm_x10 / 10:.1f}"
        print(
            f"beat sequence={sequence} "
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
        "--record",
        type=Path,
        help="save embedded conducting diagnostics to a CSV file",
    )
    parser.add_argument(
        "--test", action="store_true", help="play the configured beat cue once"
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
    parser.add_argument(
        "--detector", choices=("peak", "peak-ml"), default="peak",
        help="peak uses firmware events; peak-ml additionally gates them with the PC direction model",
    )
    parser.add_argument(
        "--model", type=Path,
        default=Path("models/gesture_v1/gesture_tree.joblib"),
        help="baseline joblib artifact used by --detector peak-ml",
    )
    args = parser.parse_args()
    try:
        config = load_config(args.config)
        if args.detector == "peak-ml" and (args.ble or args.demo or args.test):
            raise ValueError("--detector peak-ml currently requires USB serial mode")
        if args.test:
            if sys.platform != "win32":
                raise RuntimeError("PC audio cues currently require Windows")
            audio = AudioWorker()
            audio.start()
            audio.play(config.beat)
            audio.close()
        elif args.ble:
            asyncio.run(run(config))
        elif args.demo:
            run_local(config)
        else:
            run_serial(config, args.record, args.detector, args.model)
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nStopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
