"""Capture DK evaluation UART records or regenerate plots from a saved CSV."""
import argparse
import csv
import json
import time
from pathlib import Path

import numpy as np

AXES = [f"{group}_{axis}" for group in ("gyro", "accel", "corrected_gyro", "linear_accel")
        for axis in "xyz"]
FIELDS = ["kind", "device_ms", "sequence", "mode", "calibrated", *AXES]
MODES = {-1: "fault", 0: "off", 1: "idle", 2: "practice"}
TARGET = {"off": 0, "idle": 12.5, "practice": 416}


def decode(line):
    tag, *fields = line.strip().split(b",")
    if tag not in (b"EVM", b"EVI", b"EVS"):
        return
    values = list(map(int, fields))
    if tag == b"EVM":
        stamp, mode = values
        return ["state", stamp, "", MODES[mode], "", *([""] * 12)]
    if tag == b"EVI":
        stamp, seq = values
        return ["sample", stamp, seq, "idle", "", *([""] * 12)]
    stamp, seq, valid, *axes = values
    if len(axes) != 12 or valid not in (0, 1):
        raise ValueError("invalid axes")
    units = [10]*3 + [1]*3 + [100]*3 + [1]*3
    data = [v / scale for v, scale in zip(axes, units)]
    return ["sample", stamp, seq, "practice", valid, *data[:6],
            *(data[6:] if valid else [""]*6)]


def capture(port, seconds, path):
    import serial
    count = invalid = 0
    with serial.Serial(port, 1000000, timeout=0.2) as uart, path.open("x") as file:
        writer = csv.writer(file)
        writer.writerow(FIELDS)
        uart.reset_input_buffer()
        start = time.monotonic()
        try:
            while time.monotonic() - start < seconds:
                try:
                    row = decode(uart.readline())
                except (ValueError, KeyError):
                    invalid += 1
                    continue
                if row:
                    writer.writerow(row)
                    count += 1
        except KeyboardInterrupt:
            pass
    print(f"Saved {count} records, {invalid} malformed evaluation lines to {path}")


def analyze(path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    with path.open() as file:
        rows = list(csv.DictReader(file))
    if not rows:
        raise SystemExit("No evaluation records. Check firmware, UART port and baud rate.")
    stamp = np.array([int(r["device_ms"]) for r in rows])
    if np.any(np.diff(stamp) < 0):
        raise SystemExit("Device clock restarted or wrapped. Analyze separate runs.")
    t = (stamp - stamp[0]) / 1000
    modes = np.array([r["mode"] for r in rows])
    samples = np.array([r["kind"] == "sample" for r in rows])
    seq = [int(r["sequence"]) for r in rows if r["sequence"]]
    summary = {"duration_s": float(t[-1]), "missing_uart_records":
               sum((b-a-1) % 2**32 for a,b in zip(seq, seq[1:])), "states": []}
    fig, ax = plt.subplots()
    for lo, hi in zip(starts := np.r_[0, np.flatnonzero(modes[1:] != modes[:-1])+1],
                      np.r_[starts[1:],len(rows)]):
        mode = modes[lo]
        end = t[hi] if hi < len(t) else t[-1]
        times = t[lo:hi][samples[lo:hi]]
        edges = np.arange(t[lo]+1, end+1e-8, .1)
        rates = np.searchsorted(times, edges, side="right") - np.searchsorted(times, edges-1, side="right")
        target = TARGET.get(mode)
        result = {"mode": str(mode), "start_s": float(t[lo]), "samples": len(times)}
        ax.axvline(t[lo], color="grey", lw=.6)
        ax.text(t[lo], .98, mode, transform=ax.get_xaxis_transform(), va="top", fontsize=8)
        if target is not None:
            ax.hlines(target,t[lo],end,linestyles="dashed",colors="orange")
        if len(rates):
            result.update(mean_hz=float(rates.mean()), sd_hz=float(rates.std()))
            ax.plot(edges,rates,color="navy")
            if target is not None:
                good = abs(rates-target) <= max(1,.05*target)
                stable = np.where(np.convolve(good.astype(int),np.ones(5),mode="valid")==5)[0] if len(good)>=5 else []
                result["settled_s"] = float(edges[stable[0]]-t[lo]) if len(stable) else None
        if mode == "practice":
            result["gaps_over_1_5_periods"] = int(sum(np.diff(times)>1.5/416))
        summary["states"].append(result)
    ax.set(xlabel="Device time since first record (s)",ylabel="Fresh samples / second",
           title="Measured sampling rate and configured targets (dashed)")
    fig.tight_layout()
    fig.savefig(path.parent/"sampling-rate.png")
    plt.close(fig)
    practice = modes == "practice"
    active = practice & samples
    if active.any():
        data = np.array([[float(r[k]) if r[k] else np.nan for k in AXES] for r in rows])
        valid = np.isfinite(data[:,6])
        fig, axes = plt.subplots(3,2,figsize=(12,9),sharex=True)
        for j,(group,unit) in enumerate([("Gyroscope","deg/s"),("Acceleration","mg")]):
            for col,offset in enumerate([0,6]):
                xyz = data[:,offset+j*3:offset+j*3+3]
                ax = axes[j,col]
                ax.plot(t,xyz,lw=.7)
                ax.legend("XYZ")
                ax.set_ylabel(unit)
                ax.set_title(("Raw " if col==0 else "Corrected ")+group)
                ax = axes[2,j]
                mag = np.linalg.norm(xyz,axis=1)
                ax.plot(t,mag,lw=.8,label="Raw" if col==0 else "Corrected")
                if valid.any():
                    summary[f"{group}_{col}_paired"] = dict(axis_mean=xyz[valid].mean(axis=0).tolist(),
                        axis_sd=xyz[valid].std(axis=0).tolist(),mean_magnitude=float(mag[valid].mean()))
            axes[2,j].set(xlabel="Time (s)",ylabel=unit,title=group+" magnitude")
        for value,label in [(22,"Gentle"),(30,"Balanced"),(40,"Deliberate")]:
            axes[2,0].axhline(value,ls=":",lw=.8,label=label)
        for ax in axes.flat:
            ax.fill_between(t,0,1,where=active & ~valid,transform=ax.get_xaxis_transform(),alpha=.13,color="grey")
        for ax in axes[2]: ax.legend()
        fig.tight_layout()
        fig.savefig(path.parent/"motion.png")
        plt.close(fig)
    (path.parent/"statistics.json").write_text(json.dumps(summary,indent=2)+"\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    options = parser.add_mutually_exclusive_group(required=True)
    options.add_argument("--port")
    options.add_argument("--analyze",type=Path)
    parser.add_argument("--out",type=Path)
    parser.add_argument("--seconds",type=float,default=180)
    args = parser.parse_args()
    if args.port:
        if not args.out or not args.seconds > 0:
            parser.error("Capture needs --out NEW_DIRECTORY and positive --seconds")
        args.out.mkdir(parents=True,exist_ok=False)
        path = args.out/"samples.csv"
        capture(args.port,args.seconds,path)
    else:
        path = args.analyze
    analyze(path)
