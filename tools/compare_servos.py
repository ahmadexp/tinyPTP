#!/usr/bin/env python3
"""Compare single-boot follower captures at a fixed acquisition cutoff.

Requires matplotlib. Keeps every accepted sample, including non-s3 states.
"""
import argparse
import csv
import hashlib
import json
import math
import re
from pathlib import Path
import statistics

from plot_follower import plt, read_samples, save_figure, style_axis
from matplotlib.ticker import ScalarFormatter


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pi", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--candidate-label", choices=["LINREG", "AUTO_PI", "KALMAN", "PLL_FLL"], default="LINREG")
    parser.add_argument("--reference-after", type=Path, help="Optional repeated fixed-PI capture after the candidate")
    parser.add_argument("--reference-between", type=Path, help="PI run between two candidate trials")
    parser.add_argument("--candidate-repeat", type=Path, help="Repeated candidate after the middle PI run")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--after", type=float, default=120)
    args = parser.parse_args()
    if args.after < 0:
        parser.error("--after must be nonnegative")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    if bool(args.reference_between) != bool(args.candidate_repeat):
        parser.error("--reference-between and --candidate-repeat must be supplied together")
    if args.candidate_repeat and args.candidate_label == "AUTO_PI":
        parser.error("Repeated adaptive-PI gain plots are not implemented")
    runs, summary = {}, {}
    sources = [("PI", args.pi), (args.candidate_label, args.candidate)]
    if args.candidate_repeat:
        sources.extend([("PI_MIDDLE", args.reference_between),
                        (args.candidate_label + "_REPEAT", args.candidate_repeat)])
    if args.reference_after:
        sources.append(("PI_AFTER", args.reference_after))
    for name, path in sources:
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
    gain_rows = []
    if args.candidate_label == "AUTO_PI":
        first = runs["AUTO_PI"][0][0]["uptime_s"]
        for t, mode, kp, ki in re.findall(
                r"tinyptp\[(\d+\.\d+)\]: servo AUTO_PI mode (\d) kp_milli (\d+) ki_milli (\d+)",
                args.candidate.read_text()):
            gain_rows.append({"elapsed_s": round(float(t) - first, 3), "mode": int(mode),
                              "kp": int(kp) / 1000, "ki": int(ki) / 1000})
        if not gain_rows:
            parser.error("AUTO_PI capture has no gain telemetry")
        with (args.output_dir / "auto-pi-gains.csv").open("w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=list(gain_rows[0]), lineterminator="\n")
            writer.writeheader(); writer.writerows(gain_rows)
        mode_counts = {str(i): 0 for i in range(3)}
        for row in runs["AUTO_PI"][1]:
            mode = [g["mode"] for g in gain_rows if g["elapsed_s"] <= row["elapsed_s"]][-1]
            mode_counts[str(mode)] += 1
        summary["AUTO_PI"]["window_gain_mode_counts"] = mode_counts
    (args.output_dir / "comparison.json").write_text(json.dumps(summary, indent=2) + "\n")
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10,
                         "svg.hashsalt": "tinyptp-servo-comparison"})
    colors = {"PI": "#2563eb", args.candidate_label: "#c2410c", "PI_AFTER": "#0f766e"}
    colors.update({"PI_MIDDLE": "#7c3aed", args.candidate_label + "_REPEAT": "#be185d"})
    labels = {"PI": "PI 1", "PI_MIDDLE": "PI 2", "PI_AFTER": "PI 3",
              args.candidate_label: args.candidate_label.replace("_", "/") + " 1",
              args.candidate_label + "_REPEAT": args.candidate_label.replace("_", "/") + " 2"} if args.candidate_repeat else {}
    for steady in [False, True]:
        fig, axes = plt.subplots(4 if gain_rows else 3, 1, figsize=(10, 10 if gain_rows else 8), sharex=True)
        fig.subplots_adjust(left=0.15, right=0.97, top=0.84, bottom=0.11, hspace=0.28)
        title = {"AUTO_PI": "Fixed and adaptive PI follower servos",
                 "LINREG": "PI and linear-regression follower servos",
                 "KALMAN": "PI and Kalman follower servos",
                 "PLL_FLL": "PI and PLL/FLL follower servos"}[args.candidate_label]
        fig.suptitle(title, x=0.15, y=0.97,
                     ha="left", fontsize=19, fontweight="bold")
        scores = [f"{labels.get(name, name)} {'' if args.candidate_repeat else 'RMS '}{summary[name]['offset_rms_ns']:.2f} ns" for name in runs]
        if steady and args.candidate_repeat:
            subtitle = f"Offset RMS, all samples after {args.after:g} s\n" + " | ".join(scores[:3]) + "\n" + " | ".join(scores[3:])
        else:
            subtitle = (f"All samples after {args.after:g} s | " + " | ".join(scores)
                        if steady else "Separate runs on the same UNO and TimeStick pair | acquisition included")
        fig.text(0.15, 0.91, subtitle, va="top", fontsize=10 if args.reference_after else 11)
        for name, (rows, window) in runs.items():
            data = window if steady else rows
            for ax, key, scale, label in zip(axes,
                    ["offset_ns", "frequency_ppb", "path_delay_ns"],
                    [1 if steady else 1e6, 1, 1],
                    ["Offset (ns)" if steady else "Offset (ms)", "Correction (ppb)", "Path delay (ns)"]):
                ax.plot([r["elapsed_s"] for r in data], [r[key]/scale for r in data],
                        color=colors[name], label=labels.get(name, name), linewidth=0.85, marker=".", markersize=2)
                style_axis(ax, label)
                ax.yaxis.set_major_formatter(ScalarFormatter(useOffset=False))
        axes[0].legend(loc="best", frameon=False, ncol=3 if args.candidate_repeat else 1)
        axes[0].axhline(0, color="#64748b", linewidth=0.6, linestyle="--")
        if gain_rows:
            start = args.after if steady else 0
            end = runs["AUTO_PI"][0][-1]["elapsed_s"]
            carry = [g for g in gain_rows if g["elapsed_s"] <= start][-1]
            gains = [dict(carry, elapsed_s=start)] + [g for g in gain_rows if start < g["elapsed_s"] <= end]
            gains.append(dict(gains[-1], elapsed_s=end))
            axes[3].step([g["elapsed_s"] for g in gains], [g["mode"] for g in gains],
                         where="post", color=colors["AUTO_PI"], linewidth=1.3)
            style_axis(axes[3], "Adaptive gain mode")
            axes[3].set_yticks([0, 1, 2], labels=["Low", "Normal", "Fast"])
            axes[3].set_ylim(-0.2, 2.2)
        axes[-1].set_xlabel("Elapsed time from each run's first accepted sample (s)")
        fig.text(0.15, 0.025, "Internal PTP estimates. Sequential trials; no smoothing or outlier removal.",
                 fontsize=9, color="#475569")
        save_figure(fig, args.output_dir, "servo-steady-state" if steady else "servo-acquisition")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
