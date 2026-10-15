#!/usr/bin/env python3
"""Embed archived, unsmoothed R4 samples in the standalone paper source."""
import csv
import hashlib
import json
import math
from pathlib import Path
import re

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
assert '\u2014' not in s
path.write_text(s)
print('Verified six raw-log hashes and all six sample counts/RMS values; embedded three figures.')
