#!/usr/bin/env python3
"""Plot follower serial measurements from the final boot in a capture.

Requires matplotlib. Writes PNG/SVG figures, extracted CSV data, and checksums.
Times are relative to the first accepted sample, not UTC or grandmaster time.
"""

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import statistics

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import EngFormatter


SAMPLE = re.compile(
    r"^tinyptp\[(\d+\.\d+)\]: master offset (-?\d+) s([0-3]) "
    r"freq ([+-]?\d+) path delay (-?\d+) sample (\d+) seq (\d+)$"
)


BOOT = re.compile(r"^tinyptp\[\d+\.\d+\]: boot role (?:GRANDMASTER|FOLLOWER|DIAGNOSTIC)[ \t]*$", re.MULTILINE)


def boot_count(text):
    return len(BOOT.findall(text))


def read_samples(path):
    text = path.read_text()
    markers = list(BOOT.finditer(text))
    if not markers:
        raise ValueError("Capture needs a boot marker to delimit buffered output")
    text = text[markers[-1].start():]
    rows = []
    for line in text.splitlines():
        match = SAMPLE.fullmatch(line.strip())
        if not match:
            continue
        uptime, offset, state, frequency, delay, sample, sequence = match.groups()
        if int(state) == 0:
            continue
        rows.append({
            "sample": int(sample), "sequence": int(sequence),
            "uptime_s": float(uptime), "elapsed_s": 0.0,
            "servo_state": int(state), "offset_ns": int(offset),
            "frequency_ppb": int(frequency), "path_delay_ns": int(delay),
        })
    if len(rows) < 2:
        raise ValueError("Need at least two accepted samples after the final boot")
    for previous, current in zip(rows, rows[1:]):
        if current["sample"] <= previous["sample"] or current["uptime_s"] <= previous["uptime_s"]:
            raise ValueError("Sample numbers and uptime must increase within one boot")
    for row in rows:
        row["elapsed_s"] = round(row["uptime_s"] - rows[0]["uptime_s"], 3)
    return rows


def style_axis(ax, label):
    ax.set_ylabel(label)
    ax.grid(True, color="#e2e8f0", linewidth=0.7)
    ax.spines[["top", "right"]].set_visible(False)
    ax.spines[["bottom", "left"]].set_color("#cbd5e1")
    ax.tick_params(colors="#475569")
    ax.margins(x=0.015)


def save_figure(fig, output, stem):
    fig.savefig(output / (stem + ".png"), dpi=160, facecolor="white")
    fig.savefig(output / (stem + ".svg"), metadata={"Date": None}, facecolor="white")
    svg = output / (stem + ".svg")
    svg.write_text("\n".join(line.rstrip() for line in svg.read_text().splitlines()) + "\n")
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--tail", type=int, default=60)
    args = parser.parse_args()
    if args.tail < 2:
        parser.error("--tail must be at least 2")
    rows = read_samples(args.log)
    tail = rows[-args.tail:]
    offsets = [r["offset_ns"] for r in tail]
    rms = math.sqrt(statistics.mean(v * v for v in offsets))
    output = args.output_dir
    output.mkdir(parents=True, exist_ok=True)
    with (output / "follower-samples.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)

    plt.rcParams.update({
        "font.family": "DejaVu Sans", "font.size": 11,
        "axes.labelcolor": "#334155", "text.color": "#0f172a",
        "svg.hashsalt": "tinyptp-follower",
    })
    times = [r["elapsed_s"] for r in rows]
    fig, axes = plt.subplots(3, 1, figsize=(10, 8), sharex=True)
    fig.subplots_adjust(left=0.14, right=0.97, top=0.85, bottom=0.11, hspace=0.26)
    fig.suptitle("Follower acquisition and clock correction", x=0.14, y=0.975,
                 ha="left", fontsize=19, fontweight="bold")
    fig.text(0.14, 0.921, f"{len(rows)} accepted exchanges | "
             f"{times[-1]:.1f} s from first to last sample | final boot only", fontsize=11)
    series = [
        ([r["offset_ns"] / 1_000_000 for r in rows], "Offset (ms)", "#2563eb"),
        ([r["frequency_ppb"] for r in rows], "Correction (ppb)", "#7c3aed"),
        ([r["path_delay_ns"] for r in rows], "Path delay (ns)", "#0f766e"),
    ]
    for ax, (values, label, color) in zip(axes, series):
        ax.plot(times, values, color=color, linewidth=1.5, marker=".", markersize=3)
        style_axis(ax, label)
        ax.axvspan(tail[0]["elapsed_s"], times[-1], color="#cbd5e1", alpha=0.22)
    axes[1].yaxis.set_major_formatter(EngFormatter(sep=""))
    axes[-1].set_xlabel("Elapsed time from first accepted sample (s)")
    fig.text(0.14, 0.025, f"Shading: final {len(tail)} samples. Internal PTP estimates; "
             "not oscilloscope or UTC measurements.", fontsize=9, color="#475569")
    save_figure(fig, output, "follower-acquisition")

    fig, ax = plt.subplots(figsize=(10, 4.5))
    fig.subplots_adjust(left=0.14, right=0.97, top=0.74, bottom=0.20)
    fig.suptitle(f"Follower offset: final {len(tail)} samples", x=0.14, y=0.965,
                 ha="left", fontsize=19, fontweight="bold")
    fig.text(0.14, 0.85, f"RMS {rms:.2f} ns    Mean {statistics.mean(offsets):.1f} ns    "
             f"Range {min(offsets):+d} to {max(offsets):+d} ns", fontsize=12)
    ax.axhline(0, color="#64748b", linewidth=0.8, linestyle="--")
    ax.plot([r["elapsed_s"] for r in tail], offsets, color="#2563eb",
            linewidth=1.2, marker="o", markersize=3.5)
    style_axis(ax, "Offset (ns)")
    ax.set_xlabel("Elapsed time from first accepted sample (s)")
    ax.set_ylim(min(offsets) - 8, max(offsets) + 8)
    fig.text(0.14, 0.035, "Follower minus grandmaster. Original samples without smoothing. "
             "Internal PTP estimates.", fontsize=9, color="#475569")
    save_figure(fig, output, "follower-offset-detail")

    names = ["follower-samples.csv", "follower-acquisition.png", "follower-acquisition.svg",
             "follower-offset-detail.png", "follower-offset-detail.svg"]
    (output / "SHA256SUMS").write_text("".join(
        hashlib.sha256((output / name).read_bytes()).hexdigest() + "  " + name + "\n"
        for name in names))
    print(json.dumps({"samples": len(rows), "tail_samples": len(tail),
                      "offset_rms_ns": rms, "output_dir": str(output)}, indent=2))


if __name__ == "__main__":
    main()
