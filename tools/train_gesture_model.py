#!/usr/bin/env python3
"""Clean labeled baton sessions and train a compact gesture decision tree."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path

import joblib
import numpy as np
from sklearn.metrics import accuracy_score, classification_report, confusion_matrix
from sklearn.tree import DecisionTreeClassifier, export_text

AXES = (
    "accel_x_mg", "accel_y_mg", "accel_z_mg",
    "gyro_x_dps_tenths", "gyro_y_dps_tenths", "gyro_z_dps_tenths",
)
STAT_NAMES = ("mean", "std", "min", "max", "rms", "peak_to_peak")
WINDOW_MS = 200
STRIDE_MS = 25
# Arrow-key labels are changed once per stroke; retain most of each short
# directional segment while excluding immediate host-key transition jitter.
TRANSITION_MARGIN_MS = 50


def load_clean(path: Path) -> tuple[list[dict[str, object]], dict[str, int]]:
    rows: list[dict[str, object]] = []
    rejected = 0
    missing = 0
    previous_sequence: int | None = None
    previous_timestamp: int | None = None
    with path.open(newline="", encoding="utf-8") as source:
        for raw in csv.DictReader(source):
            try:
                sequence = int(raw["sequence"])
                timestamp = int(raw["device_time_ms"])
                values = np.array([int(raw[name]) for name in AXES], dtype=np.float64)
            except (KeyError, TypeError, ValueError):
                rejected += 1
                continue
            if previous_sequence is not None:
                sequence_delta = (sequence - previous_sequence) & 0xFFFF
                timestamp_delta = (timestamp - previous_timestamp) & 0xFFFFFFFF
                if not 1 <= sequence_delta <= 1000 or not 1 <= timestamp_delta <= 1000:
                    rejected += 1
                    continue
                missing += sequence_delta - 1
            rows.append({"label": raw["label"], "timestamp": timestamp, "values": values})
            previous_sequence = sequence
            previous_timestamp = timestamp
    return rows, {"accepted_rows": len(rows), "rejected_rows": rejected,
                  "missing_packets": missing}


def feature_names() -> list[str]:
    channels = list(AXES) + ["accel_norm_mg", "gyro_norm_dps_tenths"]
    return [f"{channel}_{stat}" for channel in channels for stat in STAT_NAMES]


def window_features(values: np.ndarray) -> np.ndarray:
    accel_norm = np.linalg.norm(values[:, :3], axis=1)
    gyro_norm = np.linalg.norm(values[:, 3:], axis=1)
    channels = np.column_stack((values, accel_norm, gyro_norm))
    features = []
    for channel in channels.T:
        features.extend((channel.mean(), channel.std(), channel.min(), channel.max(),
                         np.sqrt(np.mean(channel * channel)), np.ptp(channel)))
    return np.asarray(features, dtype=np.float64)


def make_windows(rows: list[dict[str, object]]) -> tuple[np.ndarray, np.ndarray]:
    features: list[np.ndarray] = []
    labels: list[str] = []
    start = 0
    while start < len(rows):
        end = start + 1
        label = rows[start]["label"]
        while end < len(rows) and rows[end]["label"] == label:
            end += 1
        segment_start = int(rows[start]["timestamp"]) + TRANSITION_MARGIN_MS
        segment_end = int(rows[end - 1]["timestamp"]) - TRANSITION_MARGIN_MS
        cursor = start
        while cursor < end:
            window_end = cursor
            timestamp = int(rows[window_end]["timestamp"])
            if timestamp < segment_start + WINDOW_MS:
                cursor += 1
                continue
            if timestamp > segment_end:
                break
            window_start = window_end
            while (window_start > start and
                   timestamp - int(rows[window_start - 1]["timestamp"]) <= WINDOW_MS):
                window_start -= 1
            if window_end - window_start >= 40:
                values = np.stack([row["values"] for row in rows[window_start:window_end + 1]])
                features.append(window_features(values))
                labels.append(str(label))
            target = timestamp + STRIDE_MS
            cursor += 1
            while cursor < end and int(rows[cursor]["timestamp"]) < target:
                cursor += 1
        start = end
    return np.stack(features), np.asarray(labels)


def new_model() -> DecisionTreeClassifier:
    return DecisionTreeClassifier(
        max_depth=12, min_samples_leaf=20, class_weight="balanced", random_state=42
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sessions", nargs="+", type=Path)
    parser.add_argument("--output", type=Path, default=Path("models/gesture_v1"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    datasets: dict[str, tuple[np.ndarray, np.ndarray]] = {}
    cleaning: dict[str, dict[str, int]] = {}
    for path in args.sessions:
        rows, stats = load_clean(path)
        x_values, y_values = make_windows(rows)
        datasets[path.name] = (x_values, y_values)
        cleaning[path.name] = stats | {"windows": len(y_values)}
        print(f"{path.name}: {stats}, windows={len(y_values)}, "
              f"labels={dict(Counter(y_values))}")

    all_labels = sorted(set(np.concatenate([data[1] for data in datasets.values()])))
    evaluations: dict[str, object] = {}
    if len(datasets) >= 2:
        for held_out, (x_test, y_test) in datasets.items():
            train = [data for name, data in datasets.items() if name != held_out]
            x_train = np.concatenate([data[0] for data in train])
            y_train = np.concatenate([data[1] for data in train])
            model = new_model().fit(x_train, y_train)
            prediction = model.predict(x_test)
            evaluations[held_out] = {
                "accuracy": accuracy_score(y_test, prediction),
                "classification_report": classification_report(
                    y_test, prediction, labels=all_labels, output_dict=True,
                    zero_division=0),
                "confusion_matrix": confusion_matrix(
                    y_test, prediction, labels=all_labels).tolist(),
            }
            print(f"hold out {held_out}: accuracy={evaluations[held_out]['accuracy']:.3f}")

    x_all = np.concatenate([data[0] for data in datasets.values()])
    y_all = np.concatenate([data[1] for data in datasets.values()])
    final_model = new_model().fit(x_all, y_all)
    joblib.dump({"model": final_model, "feature_names": feature_names(),
                 "window_ms": WINDOW_MS, "stride_ms": STRIDE_MS,
                 "labels": all_labels}, args.output / "gesture_tree.joblib")
    (args.output / "gesture_tree.txt").write_text(
        export_text(final_model, feature_names=feature_names()), encoding="utf-8")
    report = {
        "sessions": [str(path) for path in args.sessions],
        "cleaning": cleaning,
        "labels": all_labels,
        "features": feature_names(),
        "window_ms": WINDOW_MS,
        "stride_ms": STRIDE_MS,
        "transition_margin_ms": TRANSITION_MARGIN_MS,
        "model": {"type": "DecisionTreeClassifier", "max_depth": 12,
                  "min_samples_leaf": 20, "node_count": final_model.tree_.node_count,
                  "actual_depth": final_model.tree_.max_depth},
        "leave_one_session_out": evaluations,
    }
    (args.output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"saved model and report to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
