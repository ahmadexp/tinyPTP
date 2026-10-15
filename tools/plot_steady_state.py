#!/usr/bin/env python3
"""Plot a fixed post-acquisition window, retaining all samples in that window.

Requires matplotlib. The cutoff is elapsed seconds after the first accepted
exchange, not a filter on servo state, offset magnitude, or apparent quality.
"""

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

from plot_follower import boot_count, plt, read_samples, save_figure, style_axis
from matplotlib.ticker import ScalarFormatter


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--after", type=float, default=120,
                        help="Exclude this many seconds after first accepted exchange")
    args = parser.parse_args()
    if args.after < 0:
        parser.error("--after must be nonnegative")
    text = args.log.read_text()
    if boot_count(text) != 1:
        parser.error("Long-run analysis requires exactly one boot in the capture")
    rows = read_samples(args.log)
    window = [r for r in rows if r["elapsed_s"] >= args.after]
    if len(window) < 2:
        parser.error("Fewer than two samples remain after the acquisition cutoff")
    offsets = [r["offset_ns"] for r in window]
    paths = [r["path_delay_ns"] for r in window]
    frequencies = [r["frequency_ppb"] for r in window]
    gaps = [b["uptime_s"] - a["uptime_s"] for a, b in zip(window, window[1:])]
    summary = {
        "source_log": args.log.name,
        "source_sha256": hashlib.sha256(args.log.read_bytes()).hexdigest(),
        "selection": "All accepted exchanges at or after the fixed elapsed-time cutoff.",
        "cutoff_after_first_sample_s": args.after,
        "total_samples": len(rows), "excluded_acquisition_samples": len(rows) - len(window),
        "window_samples": len(window),
        "window_first_sample": window[0]["sample"], "window_last_sample": window[-1]["sample"],
        "window_first_elapsed_s": window[0]["elapsed_s"],
        "window_last_elapsed_s": window[-1]["elapsed_s"],
        "window_span_s": round(window[-1]["elapsed_s"] - window[0]["elapsed_s"], 3),
        "offset_mean_ns": statistics.mean(offsets),
        "offset_rms_ns": math.sqrt(statistics.mean(v * v for v in offsets)),
        "offset_population_stddev_ns": statistics.pstdev(offsets),
        "offset_min_ns": min(offsets), "offset_max_ns": max(offsets),
        "path_mean_ns": statistics.mean(paths), "path_min_ns": min(paths), "path_max_ns": max(paths),
        "frequency_first_ppb": frequencies[0], "frequency_last_ppb": frequencies[-1],
        "frequency_min_ppb": min(frequencies), "frequency_max_ppb": max(frequencies),
        "servo_state_counts": {str(state): sum(r["servo_state"] == state for r in window)
                               for state in sorted({r["servo_state"] for r in window})},
        "largest_sample_interval_s": round(max(gaps), 3),
        "sample_number_gaps": sum(b["sample"] - a["sample"] - 1 for a, b in zip(window, window[1:])),
        "measurement_limit": "Internal PTP estimates; not calibrated PPS or UTC measurements.",
    }
    output = args.output_dir
    output.mkdir(parents=True, exist_ok=True)
    with (output / "steady-state-samples.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(window[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(window)
    (output / "steady-state-summary.json").write_text(json.dumps(summary, indent=2) + "\n")

    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 11,
                         "axes.labelcolor": "#334155", "text.color": "#0f172a",
                         "svg.hashsalt": "tinyptp-steady-state"})
    fig, axes = plt.subplots(3, 1, figsize=(10, 8), sharex=True)
    fig.subplots_adjust(left=0.15, right=0.97, top=0.82, bottom=0.11, hspace=0.29)
    fig.suptitle("Follower steady-state measurements", x=0.15, y=0.975,
                 ha="left", fontsize=19, fontweight="bold")
    fig.text(0.15, 0.92, f"{len(window)} samples over {summary['window_span_s'] / 60:.2f} min | "
             f"first {args.after:g} s excluded", fontsize=11)
    fig.text(0.15, 0.875, f"Offset RMS {summary['offset_rms_ns']:.2f} ns | "
             f"mean {summary['offset_mean_ns']:.2f} ns | "
             f"range {min(offsets):+d} to {max(offsets):+d} ns", fontsize=11)
    times = [r["elapsed_s"] / 60 for r in window]
    for ax, values, label, color in zip(
            axes, [offsets, frequencies, paths],
            ["Offset (ns)", "Correction (ppb)", "Path delay (ns)"],
            ["#2563eb", "#7c3aed", "#0f766e"]):
        ax.plot(times, values, color=color, linewidth=0.75, marker=".", markersize=2.5)
        style_axis(ax, label)
        ax.yaxis.set_major_formatter(ScalarFormatter(useOffset=False))
    axes[0].axhline(0, color="#64748b", linewidth=0.8, linestyle="--")
    axes[-1].set_xlabel("Elapsed time from first accepted sample (min)")
    fig.text(0.15, 0.026, "All samples after the cutoff; no smoothing or outlier removal. "
             "Internal PTP estimates.", fontsize=9, color="#475569")
    save_figure(fig, output, "follower-steady-state")
    names = ["follower-steady-state.png", "follower-steady-state.svg",
             "steady-state-samples.csv", "steady-state-summary.json"]
    (output / "SHA256SUMS").write_text("".join(
        hashlib.sha256((output / name).read_bytes()).hexdigest() + "  " + name + "\n"
        for name in names))
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
