#!/usr/bin/env python3
"""Embed archived, unsmoothed R4 samples in the standalone paper source."""
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
DATA = ROOT / 'docs/results/2026-10-14/uno-r4-minima'
NAMES = {'pi': 'PI', 'linreg': 'Regression', 'adaptive-pi': 'Adaptive PI',
         'kalman': 'Kalman', 'pllfll': 'PLL/FLL'}
COLORS = ['blue!80!black', 'orange!90!black', 'teal!80!black', 'violet', 'red!75!black']
summary = json.loads((DATA / 'plots/summary.json').read_text())
rows = {}
for key in [*NAMES, 'pi-steady']:
    raw = (DATA / 'trials' / (key + '.log')).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == summary[key]['source_sha256'], key
    path = DATA / 'plots' / (key + '-samples.csv')
    with path.open() as f:
        rows[key] = list(csv.DictReader(f))
    assert len(rows[key]) == summary[key]['samples'], key
    window = [r for r in rows[key] if float(r['elapsed_s']) >= 120]
    assert len(window) == summary[key]['window_samples'], key
    rms = math.sqrt(sum(int(r['offset_ns'])**2 for r in window) / len(window))
    assert math.isclose(rms, summary[key]['offset_rms_ns'], abs_tol=1e-9), key

def plot(key, color, mode):
    data = rows[key]
    if mode == 'steady':
        data = [r for r in data if float(r['elapsed_s']) >= 120]
    elif mode == 'transient':
        data = [r for r in data if float(r['elapsed_s']) <= 120]
    coords = []
    for r in data:
        y = int(r['offset_ns'])
        if mode == 'transient':
            y = max(1, abs(y))
        coords.append(f"({r['elapsed_s']},{y})")
    return '\\addplot+[no marks,thin,color=' + color + '] coordinates {\n' + '\n'.join(coords) + '\n};\n'

def figure(mode):
    common = r'width=\columnwidth,height=5.0cm,scale only axis=false,tick label style={font=\scriptsize},label style={font=\footnotesize},grid=major,grid style={gray!20},xlabel={Time from first accepted exchange (s)},legend style={font=\scriptsize,draw=none,at={(0.5,1.03)},anchor=south,legend columns=3},legend cell align=left'
    if mode == 'steady':
        options = ',xmin=120,xmax=287,ymin=-220,ymax=160,ylabel={Offset estimate (ns)}'
        body = ''.join(plot(k, c, mode) for k,c in zip(NAMES, COLORS))
        legend = '\\legend{' + ','.join(NAMES.values()) + '}\n'
        caption = 'R4 raw offset estimates after the common 120-second cutoff. Every retained sample is plotted. Traces are separate sequential trials, not simultaneous measurements.'
    elif mode == 'transient':
        options = ',xmin=0,xmax=120,ymode=log,ymin=1,ymax=1e9,ylabel={Absolute offset estimate (ns)}'
        body = ''.join(plot(k, c, mode) for k,c in zip(NAMES, COLORS))
        legend = '\\legend{' + ','.join(NAMES.values()) + '}\n'
        caption = 'R4 startup trajectories, including initial coarse steps. Absolute values are used for the logarithmic axis; exact zero offsets are displayed at 1 ns only in this figure. Initial conditions differ between runs.'
    else:
        options = ',xmin=120,xmax=887,ymin=-100,ymax=100,ylabel={Offset estimate (ns)}'
        body = plot('pi-steady', COLORS[0], 'steady')
        legend = ''
        caption = 'All 767 post-cutoff offset estimates from the longer PI record. RMS is 23.23 ns; no smoothing or downsampling is applied.'
    return '\\begin{figure}[t]\n\\centering\n\\begin{tikzpicture}\n\\begin{axis}[' + common + options + ']\n' + body + legend + '\\end{axis}\n\\end{tikzpicture}\n\\caption{' + caption + '}\n\\label{fig:' + mode + '}\n\\end{figure}'

def pps_figure():
    sys.path.insert(0, str(ROOT / 'tools'))
    from analyze_pps import analyze, stats
    from plot_pps_capture import verify_capture
    data = ROOT / 'docs/results/2026-10-16/saleae-pps'
    verify_capture(data / 'steady-01')
    measured, pairs = analyze(data / 'steady-01/digital.csv', 500000000, qualify_500ms=True)
    saved = json.loads((data / 'plots/pps-summary.json').read_text())
    body = ''
    for kind, color in [('rising', 'blue!80!black'), ('falling', 'orange!90!black')]:
        kept = [(a, -d) for a,b,d in pairs[kind] if a >= 120]
        assert stats(d for a,d in kept) == saved['physical_pps'][kind]['post_cutoff']
        body += '\\addplot+[only marks,mark=*,mark size=.45pt,color=' + color + '] coordinates {\n'
        body += '\n'.join(f'({a},{d})' for a,d in kept) + '\n};\n'
    return (r'''\begin{figure}[t]
\centering
\begin{tikzpicture}
\begin{axis}[width=\columnwidth,height=5.0cm,tick label style={font=\scriptsize},label style={font=\footnotesize},grid=major,grid style={gray!20},xmin=120,xmax=900,ymin=-65,ymax=80,xlabel={Time from capture start (s)},ylabel={Follower arrival minus GM (ns)},legend style={font=\scriptsize,draw=none,at={(0.5,1.03)},anchor=south,legend columns=2}]
''' + body + r'''\legend{Rising,Falling}
\end{axis}
\end{tikzpicture}
\caption{Independent digital PPS timing for fixed PI: all qualified post-cutoff edges, without phase filtering or skew correction. Two disturbed cycles and boundary partial pulses are excluded by the stated duration rule.}
\label{fig:pps}
\end{figure}''')


def pps_servo_table():
    """Recompute table statistics from verified native exports and raw serial."""
    sys.path.insert(0, str(ROOT / 'tools'))
    from analyze_pps import analyze, stats
    from plot_pps_capture import verify_capture, serial_samples
    data = ROOT / 'docs/results/2026-10-16/saleae-servos'
    saved = json.loads((data / 'plots/summary.json').read_text())
    manifest = json.loads((data / 'trials/manifest.json').read_text())
    assert saved['campaign_status'] == manifest['status'] == 'complete'
    assert saved['cutoff_s'] == manifest['analysis_cutoff_s'] == 120
    names = {**NAMES, 'pi-steady': 'PI, 900 s'}
    assert set(saved['trials']) == set(names)
    body = []
    for key, name in names.items():
        path = data / 'trials' / key
        capture, meta, binary = verify_capture(path)
        assert capture['channel_roles'] == {'0': 'follower', '1': 'grandmaster'}
        assert capture['device']['device_id'] == manifest['device_id']
        assert capture['digital_sample_rate_hz'] == 500000000
        assert meta['legacySettings']['digitalThreshold'] == {'description': '3.3+ Volts'}
        _, pairs = analyze(path / 'digital.csv', 500000000, qualify_500ms=True)
        p = stats(-d for a,b,d in pairs['rising'] if a >= 120)
        serial, _ = serial_samples(path, meta)
        end = min(v['end_s'] for v in binary.values())
        q = stats(s['offset_ns'] for s in serial if 120 <= s['host_receive_elapsed_s'] <= end)
        assert p == saved['trials'][key]['physical_pps']['rising']['post_cutoff']
        assert q == saved['trials'][key]['serial']['post_cutoff']
        body.append(f"{name} & {p['count']} & {p['mean_ns']:+.2f} & {p['rms_about_zero_ns']:.2f} & {q['rms_about_zero_ns']:.2f} " + r'\\')
    return (r'''\begin{table}[t]
\centering
\caption{Later simultaneous PPS and serial campaign. Values after 120 s from capture start; PPS uses qualified rising pairs. All timing values in ns.}
\label{tab:pps-servos}
\small
\setlength{\tabcolsep}{3pt}
\begin{tabular}{lrrrr}
\hline
Controller & Pairs & PPS mean & PPS RMS & Serial RMS \\
\hline
''' + '\n'.join(body) + r'''
\hline
\end{tabular}
\end{table}''')


path = HERE / 'tinyptp-ispcs2027.tex'
s = path.read_text()
for marker,mode in [('STEADY','steady'), ('TRANSIENT','transient'), ('LONG','long')]:
    start, end = '% BEGIN_' + marker + '_FIGURE', '% END_' + marker + '_FIGURE'
    value = start + '\n' + figure(mode) + '\n' + end
    if start in s:
        s = re.sub(re.escape(start) + r'.*?' + re.escape(end), lambda _: value, s, flags=re.S)
    else:
        s = s.replace('% ' + marker + '_FIGURE', value)
s = s.replace(r'\hat e/4\unit{s}', r'\hat e/(4\unit{s})')
start, end = '% BEGIN_PPS_FIGURE', '% END_PPS_FIGURE'
value = start + '\n' + pps_figure() + '\n' + end
if start in s:
    s = re.sub(re.escape(start) + r'.*?' + re.escape(end), lambda _: value, s, flags=re.S)
else:
    s = s.replace('% PPS_FIGURE', value)
start, end = '% BEGIN_PPS_SERVO_TABLE', '% END_PPS_SERVO_TABLE'
value = start + '\n' + pps_servo_table() + '\n' + end
if start in s:
    s = re.sub(re.escape(start) + r'.*?' + re.escape(end), lambda _: value, s, flags=re.S)
else:
    assert '% PPS_SERVO_TABLE' in s
    s = s.replace('% PPS_SERVO_TABLE', value)
assert '\u2014' not in s
path.write_text(s)
print('Verified original serial campaign and seven native PPS captures; embedded four figures and the PPS servo table.')
