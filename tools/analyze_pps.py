#!/usr/bin/env python3
"""Measure CH1-minus-CH0 PPS edges from a Saleae digital CSV export.

Uses decimal timestamps before subtraction. Does not remove phase outliers, detrend,
correct cable/channel skew, or infer grandmaster/follower channel assignments.
"""
import argparse
from bisect import bisect_left
import csv
from decimal import Decimal
import hashlib
import json
import math
from pathlib import Path
import statistics

NS = Decimal(1_000_000_000)


def read_edges(path):
    edges = {0: {'rising': [], 'falling': [], 'width_ns': [], 'pulses': []},
             1: {'rising': [], 'falling': [], 'width_ns': [], 'pulses': []}}
    previous, last_time, rise = None, None, {0: None, 1: None}
    with Path(path).open(newline='') as stream:
        reader = csv.DictReader(stream)
        required = {'Time [s]', 'Channel 0', 'Channel 1'}
        if not required <= set(reader.fieldnames or []):
            raise ValueError('Expected Saleae columns: Time [s], Channel 0, Channel 1')
        for row in reader:
            t = Decimal(row['Time [s]'])
            if not t.is_finite() or (last_time is not None and t <= last_time):
                raise ValueError('Timestamps must be finite and strictly increasing')
            states = [int(row[f'Channel {ch}']) for ch in (0, 1)]
            if any(s not in (0, 1) for s in states):
                raise ValueError('Digital states must be 0 or 1')
            if previous is not None:
                for ch in (0, 1):
                    if states[ch] != previous[ch]:
                        direction = 'rising' if states[ch] else 'falling'
                        edges[ch][direction].append(t)
                        if states[ch]:
                            rise[ch] = t
                        elif rise[ch] is not None:
                            edges[ch]['width_ns'].append((t-rise[ch])*NS)
                            edges[ch]['pulses'].append((rise[ch], t))
                            rise[ch] = None
            previous, last_time = states, t
    if previous is None:
        raise ValueError('Empty digital capture')
    return edges


def pair_edges(a, b, max_separation=Decimal('0.1')):
    """Pair ordered edges uniquely within a window below half a PPS period."""
    if not Decimal(0) < max_separation < Decimal('0.5'):
        raise ValueError('Pairing window must be between zero and 0.5 seconds')
    for times in (a, b):
        if any(y-x <= 2*max_separation for x,y in zip(times,times[1:])):
            raise ValueError('Edges too close for unambiguous PPS pairing; inspect glitches')
    pairs, unmatched_a, unmatched_b = [], [], []
    i = j = 0
    while i < len(a) and j < len(b):
        delta = b[j]-a[i]
        if abs(delta) <= max_separation:
            pairs.append((a[i], b[j], delta*NS)); i += 1; j += 1
        elif delta > 0:
            unmatched_a.append(a[i]); i += 1
        else:
            unmatched_b.append(b[j]); j += 1
    return pairs, unmatched_a+a[i:], unmatched_b+b[j:]


def stats(values):
    x = list(map(float, values))
    if not x:
        return {'count': 0}
    return {'count': len(x), 'mean_ns': statistics.mean(x),
            'median_ns': statistics.median(x),
            'stddev_population_ns': statistics.pstdev(x),
            'rms_about_zero_ns': math.sqrt(statistics.mean(v*v for v in x)),
            'min_ns': min(x), 'max_ns': max(x), 'peak_to_peak_ns': max(x)-min(x)}


def qualify_pulses(edges):
    """Keep complete 490--510 ms highs preceded by 490--510 ms lows.

    This explicit, optional quality rule is for the configured 500 ms PPS gate.
    All rejected transitions remain in the raw capture and are enumerated here.
    Capture-boundary partial pulses cannot qualify.
    """
    qualified, report = {}, {}
    for ch, raw in edges.items():
        def preceding_low(a):
            i = bisect_left(raw['falling'], a)-1
            return a-raw['falling'][i] if i>=0 else None
        accepted = [(a,b) for a,b in raw['pulses']
                    if Decimal('.49') <= b-a <= Decimal('.51')
                    and preceding_low(a) is not None
                    and Decimal('.49') <= preceding_low(a) <= Decimal('.51')]
        rising = [a for a,b in accepted]
        falling = [b for a,b in accepted]
        qualified[ch] = {'rising': rising, 'falling': falling,
                         'width_ns': [(b-a)*NS for a,b in accepted], 'pulses': accepted}
        ar, af = set(rising), set(falling)
        report[str(ch)] = {
            'raw_rising_count': len(raw['rising']), 'raw_falling_count': len(raw['falling']),
            'accepted_complete_pulses': len(accepted),
            'rejected_complete_pulses': [{'rise_s':str(a), 'fall_s':str(b), 'width_ns':str((b-a)*NS),
                                         'preceding_low_ns':str(preceding_low(a)*NS) if preceding_low(a) is not None else None}
                                        for a,b in raw['pulses'] if a not in ar],
            'excluded_rising_s': [str(t) for t in raw['rising'] if t not in ar],
            'excluded_falling_s': [str(t) for t in raw['falling'] if t not in af]}
    return qualified, report


def analyze(path, rate, qualify_500ms=False):
    if rate <= 0:
        raise ValueError('A positive actual sample rate is required')
    edges = read_edges(path)
    quality = None
    if qualify_500ms:
        edges, quality = qualify_pulses(edges)
    summary = {'source_csv_sha256': hashlib.sha256(Path(path).read_bytes()).hexdigest(),
               'sign_convention': 'CH1 edge time minus CH0 edge time',
               'sample_rate_hz': rate, 'sample_interval_ns': 1e9/rate,
               'pairing_window_seconds': 0.1,
               'calibration': 'uncalibrated: no channel/cable skew correction or timebase calibration',
               'quantization_note': 'Two sampled edge times can differ from the true interval by up to one sample period, excluding other errors.',
               'channel_roles': 'unassigned', 'channels': {}, 'edges': {}}
    if quality is not None:
        summary['pulse_qualification'] = {
            'rule': 'Complete continuous high and preceding low durations each 490 to 510 ms inclusive; no phase-based rejection.',
            'selection_note': 'Introduced after extra transitions were detected; this is not a predeclared glitch-free result.',
            'boundary_note': 'Partial high pulses and pulses without an observed preceding falling edge are excluded.',
            'channels': quality}
    pairs_by_direction = {}
    for ch in (0,1):
        periods = [(b-a)*NS for a,b in zip(edges[ch]['rising'], edges[ch]['rising'][1:])]
        summary['channels'][str(ch)] = {
            'rising_count': len(edges[ch]['rising']), 'falling_count': len(edges[ch]['falling']),
            'rising_period': stats(periods), 'high_pulse_width': stats(edges[ch]['width_ns']),
            'periods_outside_0p99_to_1p01_seconds': sum(abs(p-NS)>Decimal(10_000_000) for p in periods)}
    for direction in ('rising','falling'):
        pairs, ua, ub = pair_edges(edges[0][direction],edges[1][direction])
        pairs_by_direction[direction] = pairs
        summary['edges'][direction] = {**stats(p[2] for p in pairs),
            'unmatched_ch0_seconds': list(map(str,ua)), 'unmatched_ch1_seconds': list(map(str,ub))}
    if len(pairs_by_direction['rising']) < 2:
        raise ValueError('Fewer than two matched rising edges; no PPS stability result')
    return summary, pairs_by_direction


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--sample-rate', type=int, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--qualify-500ms', action='store_true', help='Require complete high and preceding low durations of 490--510 ms')
    args=parser.parse_args()
    summary,pairs=analyze(args.csv,args.sample_rate,args.qualify_500ms)
    args.output_dir.mkdir(parents=True,exist_ok=True)
    (args.output_dir/'pps-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    with (args.output_dir/'pps-pairs.csv').open('w',newline='') as f:
        w=csv.writer(f,lineterminator='\n');w.writerow(['edge','ch0_time_s','ch1_time_s','ch1_minus_ch0_ns'])
        for direction,items in pairs.items():
            for item in items:w.writerow([direction,*map(str,item)])
    print(json.dumps(summary,indent=2))


if __name__ == '__main__':
    main()
