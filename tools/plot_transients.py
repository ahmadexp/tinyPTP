#!/usr/bin/env python3
"""Reproduce startup transient figures and metrics from archived comparisons.

No device access. Every run in each supplied comparison.json is included and
its original log SHA-256 is verified before analysis. Requires matplotlib.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path

from plot_follower import boot_count, plt, read_samples, save_figure, style_axis

TYPES = {"PI": "Fixed PI", "LINREG": "Linear regression",
         "AUTO_PI": "Adaptive PI", "KALMAN": "Kalman", "PLL_FLL": "PLL/FLL"}


def settling_time(rows, band_ns, minimum_tail_s=30):
    """First sample in the final uninterrupted in-band suffix, if long enough.

    Missing sample numbers or gaps above 1.5 s break the observed suffix.
    Returning None means not demonstrated within this finite capture.
    """
    start = len(rows)
    for i in range(len(rows) - 1, -1, -1):
        if abs(rows[i]["offset_ns"]) > band_ns:
            break
        start = i
        if i and (rows[i]["sample"] != rows[i-1]["sample"] + 1 or
                  rows[i]["elapsed_s"] - rows[i-1]["elapsed_s"] > 1.5):
            break
    if start == len(rows):
        return None
    if rows[-1]["elapsed_s"] - rows[start]["elapsed_s"] < minimum_tail_s:
        return None
    return rows[start]["elapsed_s"]


def metrics(rows):
    early = [r for r in rows if r["elapsed_s"] < 120]
    initial = rows[0]["offset_ns"]
    opposite = [r for r in early if r["offset_ns"] * initial < 0]
    peak = max(opposite, key=lambda r: abs(r["offset_ns"])) if opposite else None
    locked = next((r for r in rows if r["servo_state"] == 3), None)
    saturated = [r for r in early if abs(r["frequency_ppb"]) >= 500000]
    return {
        "samples": len(rows), "first_sample_uptime_s": rows[0]["uptime_s"],
        "capture_span_s": rows[-1]["elapsed_s"], "initial_offset_ns": initial,
        "first_s3_s": locked["elapsed_s"] if locked else None,
        "settle_1000ns_s": settling_time(rows, 1000),
        "settle_100ns_s": settling_time(rows, 100),
        "opposite_peak_0_120s_ns": peak["offset_ns"] if peak else (0 if initial else None),
        "opposite_peak_time_s": peak["elapsed_s"] if peak else None,
        "saturated_samples_0_120s": len(saturated),
        "last_saturated_sample_0_120s_s": saturated[-1]["elapsed_s"] if saturated else None,
        "s3_exits": sum(a["servo_state"] == 3 and b["servo_state"] != 3
                        for a, b in zip(rows, rows[1:])),
        "sample_number_gaps": sum(b["sample"] - a["sample"] - 1 for a, b in zip(rows, rows[1:])),
        "coarse_step_samples": sum(r["servo_state"] == 1 for r in rows),
    }


def load_runs(comparisons, root):
    runs = []
    for path in comparisons:
        source = json.loads(path.read_text())
        for key, previous in source.items():
            kind = next(k for k in TYPES if key == k or key.startswith(k + "_"))
            log = path.parent.parent / previous["source_log"]
            digest = hashlib.sha256(log.read_bytes()).hexdigest()
            if digest != previous["source_sha256"]:
                raise ValueError(f"Source hash mismatch: {log}")
            text = log.read_text()
            if boot_count(text) != 1:
                raise ValueError(f"Expected exactly one boot: {log}")
            rows = read_samples(log)
            result = metrics(rows)
            if result["samples"] != previous["total_samples"] or result["first_s3_s"] != previous["first_s3_elapsed_s"]:
                raise ValueError(f"Archive summary mismatch: {log}")
            runs.append({"id": f"{path.parent.parent.name}/{key}", "servo": kind,
                         "source_log": log.resolve().relative_to(root).as_posix(),
                         "source_sha256": digest, **result, "rows": rows})
    return runs


def plot_runs(runs, output):
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10,
                         "svg.hashsalt": "tinyptp-transients"})
    palette = ["#2563eb", "#c2410c", "#0f766e", "#7c3aed", "#be185d",
               "#854d0e", "#475569", "#0891b2"]
    overview, grid = plt.subplots(5, 2, figsize=(13, 15), sharex=True)
    overview.subplots_adjust(left=.10, right=.97, top=.89, bottom=.07, hspace=.38, wspace=.25)
    overview.suptitle("Startup transients: all five follower servos", x=.10, y=.98,
                      ha="left", fontsize=20, fontweight="bold")
    overview.text(.10, .94, "13 archived runs, including every PI reference | first 120 s of accepted samples\n"
                  "Common axes; differing initial offsets and sequential conditions prevent a speed ranking.\n"
                  "Offset axes use a signed log scale outside +/-1,000 ns.", fontsize=11, va="top")
    for index, (kind, name) in enumerate(TYPES.items()):
        selected = [r for r in runs if r["servo"] == kind]
        fig, axes = plt.subplots(3, 1, figsize=(11, 10), sharex=True)
        fig.subplots_adjust(left=.13, right=.97, top=.74, bottom=.10, hspace=.30)
        fig.suptitle(name + ": startup and settling", x=.13, y=.98,
                     ha="left", fontsize=20, fontweight="bold")
        fig.text(.13, .925, "Raw samples, first 120 s | dots mark samples; diamonds mark first s3\n"
                 "Dashed lines: +/-1,000 ns; dotted lines: +/-100 ns", fontsize=11, va="top")
        for j, run in enumerate(selected):
            data = [r for r in run["rows"] if r["elapsed_s"] < 120]
            x = [r["elapsed_s"] for r in data]
            offset = [r["offset_ns"] for r in data]
            freq = [r["frequency_ppb"] / 1000 for r in data]
            color = palette[j % len(palette)]
            for ax, values in [(axes[0], offset), (axes[1], offset), (axes[2], freq),
                               (grid[index, 0], offset), (grid[index, 1], freq)]:
                ax.plot(x, values, color=color, lw=.9, marker=".", ms=2,
                        label=run["id"])
            locked = next((r for r in data if r["servo_state"] == 3), None)
            if locked:
                for ax in [axes[0], axes[1], grid[index, 0]]:
                    ax.plot(locked["elapsed_s"], locked["offset_ns"], "D", color=color, ms=4)
        for ax in [axes[0], grid[index, 0]]:
            ax.set_yscale("symlog", linthresh=1000)
            ax.set_ylim(-5e7, 5e7)
            ax.set_yticks([-1e7, -1e4, 0, 1e4, 1e7], labels=["-10M", "-10k", "0", "10k", "10M"])
        for ax in [axes[0], axes[1], grid[index, 0]]:
            style_axis(ax, "Offset (ns)")
            for limit in [-1000, 1000]:
                ax.axhline(limit, color="#64748b", ls="--", lw=.7)
            ax.axhline(0, color="#94a3b8", lw=.5)
        axes[1].set_ylim(-1500, 1500)
        axes[1].set_title("Linear near-zero detail: offsets outside +/-1,500 ns are outside this panel", fontsize=10)
        for limit in [-100, 100]:
            axes[1].axhline(limit, color="#64748b", ls=":", lw=.7)
        axes[0].set_title("Signed logarithmic scale outside +/-1,000 ns; linear inside", fontsize=10)
        for ax in [axes[2], grid[index, 1]]:
            style_axis(ax, "Correction (ppm)")
            ax.set_ylim(-550, 550)
        for ax in list(axes) + list(grid[index]):
            ax.set_xlim(0, 120)
        grid[index, 0].set_title(f"{name}: {len(selected)} run(s)", loc="left", fontsize=11, fontweight="bold")
        axes[2].set_xlabel("Seconds from each run's first accepted sample")
        handles, labels = axes[0].get_legend_handles_labels()
        fig.legend(handles, labels, loc="upper left", bbox_to_anchor=(.12, .865),
                   ncol=2, frameon=False, fontsize=9)
        fig.text(.13, .025, "Internal PTP telemetry, not independent PPS timing. Original logs linked in the report.\n"
                 "Command is logged after the offset measurement; 1 ppm = 1,000 ppb.", fontsize=9)
        save_figure(fig, output, kind.lower() + "-transient")
    for ax in grid[-1]:
        ax.set_xlabel("Seconds from first accepted sample")
    overview.text(.10, .025, "Colors identify separate runs within each row; see per-servo figures for legends. "
                  "No smoothing or normalization.\nDiamonds: first s3. "
                  "These are startup responses, not controlled phase-step or frequency-step experiments.", fontsize=10)
    save_figure(overview, output, "all-servo-transients")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("comparisons", nargs="+", type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    runs = load_runs(args.comparisons, root)
    if {r["servo"] for r in runs} != set(TYPES):
        parser.error("Supply comparisons covering all five servo types")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    summary = [{k: v for k, v in r.items() if k != "rows"} for r in runs]
    (args.output_dir / "metrics.json").write_text(json.dumps({
        "acquisition_window_s": [0, 120], "minimum_settled_tail_s": 30,
        "settling_scope": "all remaining accepted samples, finite capture only",
        "runs": summary}, indent=2) + "\n")
    with (args.output_dir / "metrics.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(summary[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(summary)
    with (args.output_dir / "samples.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["run", "servo"] + list(runs[0]["rows"][0]), lineterminator="\n")
        writer.writeheader()
        for run in runs:
            writer.writerows(dict(run=run["id"], servo=run["servo"], **row) for row in run["rows"])
    plot_runs(runs, args.output_dir)
    print(f"Verified and plotted {len(runs)} runs, {sum(r['samples'] for r in runs)} accepted samples.")


if __name__ == "__main__":
    main()
