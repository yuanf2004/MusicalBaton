#!/usr/bin/env python3
"""Record labeled 416 Hz Musical Baton IMU packets to CSV."""

from __future__ import annotations

import argparse
import csv
import json
import struct
import sys
import threading
import time
from pathlib import Path

MARKER = b"MBR1"
PACKET_SIZE = 22
LABEL_KEYS = {
    "0": "idle",
}
ARROW_KEYS = {
    "H": "diagonal_up",
    "P": "down",
    "K": "left",
    "M": "right",
}


def parse_packet(packet: bytes) -> tuple[int, ...]:
    if len(packet) != PACKET_SIZE or packet[:4] != MARKER:
        raise ValueError("invalid MBR1 training packet")
    return struct.unpack_from("<HIhhhhhh", packet, 4)


def keyboard_worker(state: dict[str, object]) -> None:
    import msvcrt

    while not state["stop"]:
        key = msvcrt.getwch().lower()
        if key in ("\x00", "\xe0"):
            label = ARROW_KEYS.get(msvcrt.getwch())
            if label is not None:
                state["label"] = label
                print(f"\nlabel={label}", flush=True)
            continue
        if key == "q":
            state["stop"] = True
        elif key in LABEL_KEYS:
            state["label"] = LABEL_KEYS[key]
            print(f"\nlabel={state['label']}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="output CSV path")
    parser.add_argument("--port", help="serial port; defaults to pc_audio_cues.json")
    parser.add_argument("--baud", type=int, default=1000000)
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("interactive labeling currently requires Windows")

    if args.port is None:
        config_path = Path(__file__).with_name("pc_audio_cues.json")
        args.port = json.loads(config_path.read_text(encoding="utf-8")).get("serial_port")
    if not args.port:
        parser.error("set --port or serial_port in tools/pc_audio_cues.json")

    try:
        import serial
    except ImportError:
        parser.error("missing dependency; run: py -m pip install pyserial")

    state: dict[str, object] = {"label": "idle", "stop": False}
    threading.Thread(target=keyboard_worker, args=(state,), daemon=True).start()
    print("Labels: 0 idle, arrows: down/left/right/diagonal-up, q stop")
    print(f"Recording {args.port} to {args.output}; current label=idle")

    fields = (
        "host_time_ns", "session_time_ms", "label", "sequence",
        "device_time_ms", "accel_x_mg", "accel_y_mg", "accel_z_mg",
        "gyro_x_dps_tenths", "gyro_y_dps_tenths", "gyro_z_dps_tenths",
    )
    buffer = bytearray()
    first_host_ns = time.perf_counter_ns()
    last_sequence: int | None = None
    dropped = 0
    rows = 0
    next_flush = 416
    with args.output.open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output)
        writer.writerow(fields)
        with serial.Serial(args.port, args.baud, timeout=0.1) as connection:
            while not state["stop"]:
                buffer.extend(connection.read(4096))
                while True:
                    marker = buffer.find(MARKER)
                    if marker < 0:
                        del buffer[:-3]
                        break
                    if marker:
                        del buffer[:marker]
                    if len(buffer) < PACKET_SIZE:
                        break
                    packet = bytes(buffer[:PACKET_SIZE])
                    del buffer[:PACKET_SIZE]
                    values = parse_packet(packet)
                    sequence = values[0]
                    if last_sequence is not None:
                        dropped += (sequence - last_sequence - 1) & 0xFFFF
                    last_sequence = sequence
                    host_ns = time.perf_counter_ns()
                    writer.writerow((host_ns, (host_ns - first_host_ns) // 1_000_000,
                                     state["label"], *values))
                    rows += 1
                if rows >= next_flush:
                    output.flush()
                    next_flush = rows + 416
                    print(f"\rrows={rows} dropped={dropped} label={state['label']}",
                          end="", flush=True)
    print(f"\nSaved {rows} samples; inferred dropped packets={dropped}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
