#!/usr/bin/env python3
"""Summarize measured PTP offsets from an UNO serial log (no lock inference)."""
import argparse
import math
from pathlib import Path
import re
import statistics


def summarize(text, tail):
    rows = [tuple(map(int, row)) for row in re.findall(
        r"PTP sample=(\d+) seq=(\d+) offset_ns=(-?\d+) path_ns=(-?\d+) freq_ppb=(-?\d+) servo=on", text)]
    if not rows:
        raise ValueError("No servo samples found")
    selected = rows[-tail:]
    offsets = [row[2] for row in selected]
    paths = [row[3] for row in selected]
    return {
        "samples": len(rows), "tail_samples": len(selected),
        "tail_first_sample": selected[0][0],
        "offset_mean_ns": statistics.mean(offsets),
        "offset_rms_ns": math.sqrt(statistics.mean(x*x for x in offsets)),
        "offset_min_ns": min(offsets), "offset_max_ns": max(offsets),
        "offset_max_abs_ns": max(map(abs, offsets)),
        "path_mean_ns": statistics.mean(paths),
        "path_min_ns": min(paths), "path_max_ns": max(paths),
        "last_freq_ppb": rows[-1][4],
        "coarse_steps": text.count("PTP coarse clock step;"),
        "exchange_timeouts": text.count("PTP exchange timeout"),
        "rejected_paths": text.count("PTP rejected path_ns="),
    }


def main():
    import json
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--tail", type=int, default=60)
    args = parser.parse_args()
    if args.tail < 1:
        parser.error("--tail must be positive")
    print(json.dumps(summarize(args.log.read_text(), args.tail), indent=2))


if __name__ == "__main__":
    main()
