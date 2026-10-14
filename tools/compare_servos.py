#!/usr/bin/env python3
"""Compare two single-boot follower captures at a fixed acquisition cutoff.

Requires matplotlib. Keeps every accepted sample, including non-s3 states.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

from plot_follower import plt, read_samples, save_figure, style_axis
from matplotlib.ticker import ScalarFormatter


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pi", type=Path)
    parser.add_argument("linreg", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--after", type=float, default=120)
    args = parser.parse_args()
    if args.after < 0:
        parser.error("--after must be nonnegative")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    runs, summary = {}, {}
    for name, path in [("PI", args.pi), ("LINREG", args.linreg)]:
        if path.read_text().count("tinyptp[0.000]: boot role") != 1:
            parser.error(f"{path}: expected exactly one boot")
        rows = read_samples(path)
        window = [r for r in rows if r["elapsed_s"] >= args.after]
        if len(window) < 2:
            parser.error(f"{path}: insufficient samples after cutoff")
        runs[name] = (rows, window)
        offsets = [r["offset_ns"] for r in window]
        locked = [r["elapsed_s"] for r in rows if r["servo_state"] == 3]
        summary[name] = {
            "source_log": path.name,
            "source_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "total_samples": len(rows), "cutoff_s": args.after,
            "first_s3_elapsed_s": min(locked) if locked else None,
            "window_samples": len(window),
            "window_span_s": round(window[-1]["elapsed_s"] - window[0]["elapsed_s"], 3),
            "offset_mean_ns": statistics.mean(offsets),
            "offset_rms_ns": math.sqrt(statistics.mean(v*v for v in offsets)),
            "offset_stddev_ns": statistics.pstdev(offsets),
            "offset_min_ns": min(offsets), "offset_max_ns": max(offsets),
            "path_mean_ns": statistics.mean(r["path_delay_ns"] for r in window),
            "frequency_min_ppb": min(r["frequency_ppb"] for r in window),
            "frequency_max_ppb": max(r["frequency_ppb"] for r in window),
            "state_counts": {str(s): sum(r["servo_state"] == s for r in window)
                             for s in sorted({r["servo_state"] for r in window})},
            "sample_number_gaps": sum(b["sample"]-a["sample"]-1
                                      for a, b in zip(rows, rows[1:])),
        }
        with (args.output_dir / (name.lower() + "-samples.csv")).open("w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=list(rows[0]), lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)
    (args.output_dir / "comparison.json").write_text(json.dumps(summary, indent=2) + "\n")
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10,
                         "svg.hashsalt": "tinyptp-servo-comparison"})
    colors = {"PI": "#2563eb", "LINREG": "#c2410c"}
    for steady in [False, True]:
        fig, axes = plt.subplots(3, 1, figsize=(10, 8), sharex=True)
        fig.subplots_adjust(left=0.15, right=0.97, top=0.84, bottom=0.11, hspace=0.28)
        fig.suptitle("PI and linear-regression follower servos", x=0.15, y=0.97,
                     ha="left", fontsize=19, fontweight="bold")
        subtitle = (f"All samples after {args.after:g} s | " + " | ".join(
            f"{name} RMS {summary[name]['offset_rms_ns']:.2f} ns" for name in runs)
            if steady else "Separate runs on the same UNO and TimeStick pair | acquisition included")
        fig.text(0.15, 0.91, subtitle, fontsize=11)
        for name, (rows, window) in runs.items():
            data = window if steady else rows
            for ax, key, scale, label in zip(axes,
                    ["offset_ns", "frequency_ppb", "path_delay_ns"],
                    [1 if steady else 1e6, 1, 1],
                    ["Offset (ns)" if steady else "Offset (ms)", "Correction (ppb)", "Path delay (ns)"]):
                ax.plot([r["elapsed_s"] for r in data], [r[key]/scale for r in data],
                        color=colors[name], label=name, linewidth=0.85, marker=".", markersize=2)
                style_axis(ax, label)
                ax.yaxis.set_major_formatter(ScalarFormatter(useOffset=False))
        axes[0].legend(loc="best", frameon=False)
        axes[0].axhline(0, color="#64748b", linewidth=0.6, linestyle="--")
        axes[-1].set_xlabel("Elapsed time from each run's first accepted sample (s)")
        fig.text(0.15, 0.025, "Internal PTP estimates. Sequential trials; no smoothing or outlier removal.",
                 fontsize=9, color="#475569")
        save_figure(fig, args.output_dir, "servo-steady-state" if steady else "servo-acquisition")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
