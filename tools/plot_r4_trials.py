#!/usr/bin/env python3
"""Plot verified R4 serial trials, including acquisition and a longer PI run."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import statistics

from plot_follower import boot_count, plt, read_samples, save_figure, style_axis
from plot_transients import metrics

NAMES = {'pi': 'Fixed PI', 'linreg': 'Linear regression',
         'adaptive-pi': 'Adaptive PI', 'kalman': 'Kalman', 'pllfll': 'PLL/FLL',
         'pi-steady': 'Fixed PI, longer run'}
SERVO = {'pi': 'PI', 'linreg': 'LINREG', 'adaptive-pi': 'AUTO_PI',
         'kalman': 'KALMAN', 'pllfll': 'PLL_FLL', 'pi-steady': 'PI'}
CPU = re.compile(r'tinyptp\[(\d+\.\d+)\]: CPU servo_last_us (\d+) '
                 r'servo_max_us (\d+) usb_poll_max_us (\d+)')


def analyze(directory, after):
    manifest = json.loads((directory / 'manifest.json').read_text())
    if manifest.get('status') != 'complete':
        raise ValueError('The trial manifest must be complete')
    results, traces = {}, {}
    for trial in manifest['trials']:
        key = trial['label']
        if key not in NAMES or key in results:
            raise ValueError(f'Unknown or duplicate trial: {key}')
        path = directory / (key + '.log')
        if hashlib.sha256(path.read_bytes()).hexdigest() != trial['log_sha256']:
            raise ValueError(f'Log checksum mismatch: {path}')
        text = path.read_text()
        if boot_count(text) != 1 or 'platform UNO_R4_MINIMA ' not in text:
            raise ValueError(f'Expected one R4 boot: {path}')
        if not re.search(r': servo ' + SERVO[key] + r'\b', text):
            raise ValueError(f'Wrong servo in {path}')
        rows = read_samples(path)
        if len(rows) != trial['samples']:
            raise ValueError(f'Sample count mismatch: {path}')
        window = [r for r in rows if r['elapsed_s'] >= after]
        if len(window) < 2:
            raise ValueError(f'Insufficient post-cutoff data: {path}')
        offsets = [r['offset_ns'] for r in window]
        cpu = [dict(uptime_s=float(t), last_us=int(last), max_us=int(peak),
                    poll_max_us=int(poll)) for t, last, peak, poll in CPU.findall(text)]
        steady_cpu = [r for r in cpu if r['uptime_s'] >= rows[0]['uptime_s'] + after]
        if not steady_cpu:
            raise ValueError(f'Missing CPU telemetry: {path}')
        counters = re.findall(r'FOLLOWER samples (\d+) timeouts (\d+) missing_rx (\d+) discarded (\d+)', text)
        results[key] = {
            'source_log': path.name, 'source_sha256': trial['log_sha256'],
            'example': trial['example'], 'firmware_sha256': manifest['firmware_sha256'][trial['example']],
            **metrics(rows), 'cutoff_s': after, 'window_samples': len(window),
            'window_span_s': round(window[-1]['elapsed_s'] - window[0]['elapsed_s'], 3),
            'offset_mean_ns': statistics.mean(offsets),
            'offset_rms_ns': math.sqrt(statistics.mean(x*x for x in offsets)),
            'offset_stddev_ns': statistics.pstdev(offsets),
            'offset_min_ns': min(offsets), 'offset_max_ns': max(offsets),
            'path_mean_ns': statistics.mean(r['path_delay_ns'] for r in window),
            'frequency_mean_ppb': statistics.mean(r['frequency_ppb'] for r in window),
            'window_state_counts': {str(s): sum(r['servo_state'] == s for r in window)
                                    for s in sorted({r['servo_state'] for r in window})},
            'steady_servo_last_median_us': statistics.median(r['last_us'] for r in steady_cpu),
            'steady_servo_peak_us': max(r['max_us'] for r in steady_cpu),
            'steady_poll_peak_us': max(r['poll_max_us'] for r in steady_cpu),
            'last_periodic_counters': dict(zip(['samples', 'timeouts', 'missing_rx', 'discarded'],
                                             map(int, counters[-1]))) if counters else None,
            'path_rejections': len(re.findall('PTP rejected path_ns=', text)),
            'initialization_failures': text.count('initialization failed; retrying'),
        }
        traces[key] = (rows, window, cpu)
    if set(results) != set(NAMES):
        raise ValueError('Expected all five servo trials and longer PI run')
    return results, traces


def plots(output, results, traces):
    plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 10,
                         'svg.hashsalt': 'tinyptp-r4-trials'})
    keys = list(NAMES)[:5]
    colors = ['#2563eb', '#c2410c', '#0f766e', '#7c3aed', '#be185d']
    fig, axes = plt.subplots(5, 2, figsize=(13, 14), sharex=True)
    fig.subplots_adjust(left=.10, right=.97, top=.90, bottom=.07, hspace=.43, wspace=.24)
    fig.suptitle('UNO R4 Minima: startup of all five servos', x=.10, y=.98,
                 ha='left', fontsize=20, fontweight='bold')
    fig.text(.10, .945, 'First 120 s of accepted samples, including coarse steps and saturation\n'
             'Sequential trials with differing initial offsets; internal PTP estimates, not PPS measurements.', va='top')
    limit = max(abs(r['offset_ns']) for key in keys for r in traces[key][0] if r['elapsed_s'] <= 120)
    for i, key in enumerate(keys):
        data = [r for r in traces[key][0] if r['elapsed_s'] <= 120]
        x = [r['elapsed_s'] for r in data]
        for ax, values, label in [(axes[i, 0], [r['offset_ns'] for r in data], 'Offset (ns)'),
                                  (axes[i, 1], [r['frequency_ppb']/1000 for r in data], 'Correction (ppm)')]:
            ax.plot(x, values, color=colors[i], lw=1, marker='.', ms=2)
            style_axis(ax, label)
            ax.set_title(NAMES[key], loc='left')
        axes[i, 0].set_yscale('symlog', linthresh=1000)
        axes[i, 0].set_ylim(-limit*1.3, limit*1.3)
        axes[i, 1].set_ylim(-525, 525)
        for band in [-1000, 1000]:
            axes[i, 0].axhline(band, color='#64748b', ls='--', lw=.6)
        first = results[key]['first_s3_s']
        if first is not None and first <= 120:
            for ax in axes[i]:
                ax.axvline(first, color=colors[i], ls=':', lw=.8)
        axes[i, 0].text(.98, .92, f'First s3: {first:.1f} s' if first is not None else 'No s3 observed',
                        transform=axes[i, 0].transAxes, ha='right', va='top', fontsize=9)
    for ax in axes[-1]:
        ax.set_xlabel('Seconds after first accepted sample')
        ax.set_xlim(0, 120)
    save_figure(fig, output, 'r4-startup-transients')

    fig, axes = plt.subplots(3, 1, figsize=(12, 11))
    fig.subplots_adjust(left=.10, right=.97, top=.86, bottom=.07, hspace=.42)
    fig.suptitle('UNO R4 Minima: five-servo steady windows', x=.10, y=.98,
                 ha='left', fontsize=20, fontweight='bold')
    fig.text(.10, .94, 'Same 120 s acquisition cutoff for each 300 s capture; every accepted state retained\n'
             'CPU bars use periodically sampled last-update durations; maxima shown separately in the results table.\n'
             'Sequential conditions prevent a controlled ranking or a causal comparison with UNO R3.', va='top')
    for key, color in zip(keys, colors):
        _, window, _ = traces[key]
        x = [r['elapsed_s'] for r in window]
        axes[0].plot(x, [r['offset_ns'] for r in window], label=NAMES[key], color=color, lw=.8)
        axes[1].plot(x, [r['path_delay_ns'] for r in window], color=color, lw=.8)
    style_axis(axes[0], 'Offset (ns)')
    style_axis(axes[1], 'Mean path delay (ns)')
    for ax in axes[:2]:
        ax.set_xlabel('Seconds after first accepted sample')
    axes[0].legend(ncol=3, fontsize=9)
    bars = axes[2].bar([NAMES[k] for k in keys],
                       [results[k]['steady_servo_last_median_us'] for k in keys], color=colors)
    axes[2].bar_label(bars, fmt='%g us', padding=3)
    axes[2].set_ylim(0, max(b.get_height() for b in bars)*1.3)
    style_axis(axes[2], 'Median sampled servo update (us)')
    save_figure(fig, output, 'r4-servo-comparison')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--after', type=float, default=120)
    args = parser.parse_args()
    if args.after != 120:
        parser.error('This comparison uses the fixed 120-second cutoff')
    results, traces = analyze(args.directory, args.after)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
    for key, (rows, _, cpu) in traces.items():
        for suffix, data in [('samples', rows), ('cpu', cpu)]:
            with (args.output_dir / f'{key}-{suffix}.csv').open('w', newline='') as f:
                writer = csv.DictWriter(f, fieldnames=list(data[0]), lineterminator='\n')
                writer.writeheader()
                writer.writerows(data)
    plots(args.output_dir, results, traces)
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
