#!/usr/bin/env python3
"""Compare session-held-out gesture models and orientation-reduced features."""

from __future__ import annotations

import argparse
import json
from collections import Counter, deque
from pathlib import Path

import joblib
import numpy as np
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import accuracy_score, f1_score
from sklearn.tree import DecisionTreeClassifier

import train_gesture_model as base

WINDOW_MS = 200
STRIDE_MS = 25


def rich_feature_names() -> list[str]:
    names: list[str] = []
    for axis in base.AXES[:3]:
        names += [f"{axis}_hp_{name}" for name in
                  ("std", "min", "max", "rms", "ptp", "delta", "peak_time",
                   "jerk_rms", "fft_low", "fft_mid", "fft_high")]
    for axis in base.AXES[3:]:
        names += [f"{axis}_{name}" for name in
                  ("mean", "std", "min", "max", "rms", "ptp", "delta",
                   "peak_time", "jerk_rms", "fft_low", "fft_mid", "fft_high")]
    for name in ("accel_dynamic_norm", "gyro_norm"):
        names += [f"{name}_{stat}" for stat in
                  ("mean", "std", "max", "rms", "ptp", "peak_time")]
    names += [f"correlation_{left}_{right}" for left, right in
              ((0, 1), (0, 2), (1, 2), (3, 4), (3, 5), (4, 5),
               (0, 3), (1, 4), (2, 5))]
    return names


def spectral_bands(signal: np.ndarray) -> tuple[float, float, float]:
    centered = signal - signal.mean()
    power = np.abs(np.fft.rfft(centered)) ** 2
    total = power[1:].sum()
    if total <= 0:
        return 0.0, 0.0, 0.0
    return (float(power[1:3].sum() / total),
            float(power[3:6].sum() / total),
            float(power[6:].sum() / total))


def channel_features(signal: np.ndarray, include_mean: bool) -> list[float]:
    values = signal if include_mean else signal - signal.mean()
    peak_time = float(np.argmax(np.abs(values))) / max(1, len(values) - 1)
    jerk = np.diff(values)
    result = []
    if include_mean:
        result.append(float(values.mean()))
    result += [float(values.std()), float(values.min()), float(values.max()),
               float(np.sqrt(np.mean(values * values))), float(np.ptp(values)),
               float(values[-1] - values[0]), peak_time,
               float(np.sqrt(np.mean(jerk * jerk))) if len(jerk) else 0.0]
    result += list(spectral_bands(values))
    return result


def rich_features(values: np.ndarray) -> np.ndarray:
    accel_hp = values[:, :3] - values[:, :3].mean(axis=0)
    gyro = values[:, 3:]
    result: list[float] = []
    for channel in accel_hp.T:
        result += channel_features(channel, False)
    for channel in gyro.T:
        result += channel_features(channel, True)
    for norm in (np.linalg.norm(accel_hp, axis=1), np.linalg.norm(gyro, axis=1)):
        result += [float(norm.mean()), float(norm.std()), float(norm.max()),
                   float(np.sqrt(np.mean(norm * norm))), float(np.ptp(norm)),
                   float(np.argmax(norm)) / max(1, len(norm) - 1)]
    channels = np.column_stack((accel_hp, gyro))
    for left, right in ((0, 1), (0, 2), (1, 2), (3, 4), (3, 5), (4, 5),
                        (0, 3), (1, 4), (2, 5)):
        if channels[:, left].std() < 1e-9 or channels[:, right].std() < 1e-9:
            result.append(0.0)
        else:
            result.append(float(np.corrcoef(channels[:, left], channels[:, right])[0, 1]))
    return np.asarray(result)


def make_windows(rows, margin_ms: int, extractor):
    features, labels, segments = [], [], []
    start = 0
    segment_id = 0
    while start < len(rows):
        end = start + 1
        label = rows[start]["label"]
        while end < len(rows) and rows[end]["label"] == label:
            end += 1
        first_time = int(rows[start]["timestamp"]) + margin_ms
        last_time = int(rows[end - 1]["timestamp"]) - margin_ms
        cursor = start
        while cursor < end:
            timestamp = int(rows[cursor]["timestamp"])
            if timestamp < first_time + WINDOW_MS:
                cursor += 1
                continue
            if timestamp > last_time:
                break
            window_start = cursor
            while (window_start > start and
                   timestamp - int(rows[window_start - 1]["timestamp"]) <= WINDOW_MS):
                window_start -= 1
            if cursor - window_start >= 40:
                values = np.stack([row["values"] for row in rows[window_start:cursor + 1]])
                features.append(extractor(values))
                labels.append(str(label))
                segments.append(segment_id)
            target = timestamp + STRIDE_MS
            cursor += 1
            while cursor < end and int(rows[cursor]["timestamp"]) < target:
                cursor += 1
        start = end
        segment_id += 1
    return np.stack(features), np.asarray(labels), np.asarray(segments)


def smooth(prediction: np.ndarray, segments: np.ndarray, width: int = 5) -> np.ndarray:
    output = prediction.copy()
    votes: deque[str] = deque(maxlen=width)
    previous = None
    for index, (label, segment) in enumerate(zip(prediction, segments)):
        if segment != previous:
            votes.clear()
            previous = segment
        votes.append(str(label))
        output[index] = Counter(votes).most_common(1)[0][0]
    return output


def model_factory(name: str):
    if name == "tree":
        return DecisionTreeClassifier(max_depth=12, min_samples_leaf=20,
                                      class_weight="balanced", random_state=42)
    if name == "forest":
        return RandomForestClassifier(n_estimators=150, max_depth=16,
                                      min_samples_leaf=8, class_weight="balanced",
                                      n_jobs=-1, random_state=42)
    raise ValueError(name)


def evaluate(datasets, model_name: str, labels: list[str]):
    folds = {}
    for held_out, (x_test, y_test, segments) in datasets.items():
        train = [data for name, data in datasets.items() if name != held_out]
        x_train = np.concatenate([data[0] for data in train])
        y_train = np.concatenate([data[1] for data in train])
        model = model_factory(model_name).fit(x_train, y_train)
        prediction = model.predict(x_test)
        smoothed = smooth(prediction, segments)
        folds[held_out] = {
            "accuracy": accuracy_score(y_test, prediction),
            "macro_f1": f1_score(y_test, prediction, labels=labels,
                                 average="macro", zero_division=0),
            "smoothed_accuracy": accuracy_score(y_test, smoothed),
            "smoothed_macro_f1": f1_score(y_test, smoothed, labels=labels,
                                          average="macro", zero_division=0),
        }
    return folds


def means(folds):
    return {key: float(np.mean([fold[key] for fold in folds.values()]))
            for key in next(iter(folds.values()))}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sessions", nargs="+", type=Path)
    parser.add_argument("--output", type=Path, default=Path("models/gesture_v2"))
    parser.add_argument(
        "--experiment", action="append",
        choices=("baseline_tree_margin50", "rich_tree_margin50",
                 "rich_forest_margin50", "rich_forest_margin100",
                 "rich_forest_margin150"),
        help="run only this experiment; repeat for multiple (default: all)",
    )
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rows_by_session = {}
    cleaning = {}
    for path in args.sessions:
        rows, stats = base.load_clean(path)
        rows_by_session[path.name] = rows
        cleaning[path.name] = stats

    labels = sorted({str(row["label"]) for rows in rows_by_session.values() for row in rows})
    experiments = {}
    configurations = [
        ("baseline_tree_margin50", 50, base.window_features, "tree"),
        ("rich_tree_margin50", 50, rich_features, "tree"),
        ("rich_forest_margin50", 50, rich_features, "forest"),
        ("rich_forest_margin100", 100, rich_features, "forest"),
        ("rich_forest_margin150", 150, rich_features, "forest"),
    ]
    if args.experiment:
        configurations = [item for item in configurations if item[0] in args.experiment]
    cache = {}
    for experiment, margin, extractor, model_name in configurations:
        cache_key = (margin, extractor.__name__)
        if cache_key not in cache:
            cache[cache_key] = {
                name: make_windows(rows, margin, extractor)
                for name, rows in rows_by_session.items()
            }
        datasets = cache[cache_key]
        folds = evaluate(datasets, model_name, labels)
        experiments[experiment] = {
            "margin_ms": margin, "model": model_name,
            "feature_set": extractor.__name__, "folds": folds,
            "means": means(folds),
            "windows": {name: len(data[1]) for name, data in datasets.items()},
        }
        print(experiment, experiments[experiment]["means"])

    best_name = max(experiments, key=lambda name:
                    experiments[name]["means"]["smoothed_macro_f1"])
    best = experiments[best_name]
    extractor = rich_features if best["feature_set"] == "rich_features" else base.window_features
    datasets = cache[(best["margin_ms"], extractor.__name__)]
    x_all = np.concatenate([data[0] for data in datasets.values()])
    y_all = np.concatenate([data[1] for data in datasets.values()])
    final_model = model_factory(best["model"]).fit(x_all, y_all)
    names = rich_feature_names() if extractor is rich_features else base.feature_names()
    joblib.dump({"model": final_model, "feature_names": names, "labels": labels,
                 "window_ms": WINDOW_MS, "stride_ms": STRIDE_MS,
                 "transition_margin_ms": best["margin_ms"],
                 "smoothing_windows": 5}, args.output / "gesture_model.joblib")
    report = {"sessions": [str(path) for path in args.sessions], "cleaning": cleaning,
              "labels": labels, "experiments": experiments,
              "selected_experiment": best_name,
              "note": "Random-forest models are offline references, not direct LSM6DSOX MLC exports."}
    (args.output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("selected", best_name, "->", args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
