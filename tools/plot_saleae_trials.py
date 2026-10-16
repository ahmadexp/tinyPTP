#!/usr/bin/env python3
"""Verify and compare the six-condition Saleae/serial R4 campaign.

The physical pulse rule is inherited from the preceding instrument checks.
Statistics describe qualified pulses; single sequential trials do not establish
servo superiority. All raw files and rejected transitions remain archived.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import sys

from analyze_pps import stats
from plot_pps_capture import save_figure, verify_capture
from plot_follower import read_samples
from plot_transients import metrics

NAMES = {'pi':'Fixed PI','linreg':'Linear regression','adaptive-pi':'Adaptive PI',
         'kalman':'Kalman','pllfll':'PLL/FLL','pi-steady':'Fixed PI, longer run'}
SERVO = {'pi':'PI','linreg':'LINREG','adaptive-pi':'AUTO_PI','kalman':'KALMAN',
         'pllfll':'PLL_FLL','pi-steady':'PI'}
COLORS = ['#2563eb','#c2410c','#0f766e','#7c3aed','#be185d']


def physical_settling(pairs, band, capture_end, minimum_tail=30):
    """Final in-band suffix, requiring observed continuity through capture end.

    Missing qualified pulses or a gap >1.5 s prevent crossing that gap. Returning
    None is not proof that the servo failed; pulse qualification can break the
    observable record. Times are relative to Saleae capture start.
    """
    if not pairs or capture_end-pairs[-1][0]>1.5:
        return None
    start = len(pairs)
    for i in range(len(pairs)-1,-1,-1):
        if abs(pairs[i][1])>band:
            break
        start=i
        if i and pairs[i][0]-pairs[i-1][0]>1.5:
            break
    if start==len(pairs) or pairs[-1][0]-pairs[start][0]<minimum_tail:
        return None
    return pairs[start][0]


def analyze(directory, output, allow_incomplete):
    manifest=json.loads((directory/'manifest.json').read_text())
    if not allow_incomplete and manifest['status']!='complete':
        raise ValueError('Campaign must be complete')
    for name,digest in manifest.get('sha256',{}).items():
        if Path(name).name!=name or hashlib.sha256((directory/name).read_bytes()).hexdigest()!=digest:
            raise ValueError('Campaign checksum mismatch: '+name)
    results={}; traces={}
    for trial in manifest['trials']:
        if trial['status']!='captured':
            if allow_incomplete:continue
            raise ValueError('Unfinished trial')
        key=trial['label']
        if key not in NAMES or key in results:
            raise ValueError('Unknown or duplicate trial '+key)
        path=directory/key
        verified,meta,binary=verify_capture(path)
        if verified['device']['device_id']!=manifest['device_id']:
            raise ValueError('Unexpected instrument identity')
        if verified['channel_roles']!={'0':'follower','1':'grandmaster'}:
            raise ValueError('Unexpected channel roles')
        if verified['digital_sample_rate_hz']!=manifest['sample_rate_hz']:
            raise ValueError('Sampling rates differ')
        if meta['legacySettings']['digitalThreshold']!={'description':'3.3+ Volts'}:
            raise ValueError('Unexpected native threshold preset')
        if not all(trial['duration_s'] <= v['end_s']-v['begin_s'] < trial['duration_s']+2
                   for v in binary.values()):
            raise ValueError('Capture duration differs from planned condition')
        raw=(path/'serial-1.log').read_text()
        if len(re.findall(r': boot role FOLLOWER',raw))!=1 or not re.search(r': servo '+SERVO[key]+r'\b',raw):
            raise ValueError('Wrong startup/servo in '+key)
        out=output/key
        subprocess.run([sys.executable,str(Path(__file__).with_name('plot_pps_capture.py')),
                        str(path),'--output-dir',str(out),'--after','120','--qualify-500ms'],
                       check=True,stdout=subprocess.DEVNULL)
        summary=json.loads((out/'pps-summary.json').read_text())
        with (out/'pps-pairs.csv').open() as f:
            pairs=list(csv.DictReader(f))
        with (out/'serial-samples.csv').open() as f:
            serial=list(csv.DictReader(f))
        original=read_samples(path/'serial-1.log')
        if len(original)!=trial['serial_samples']:
            raise ValueError('Serial count mismatch: '+key)
        rise=[(float(r['ch0_s']),float(r['follower_minus_grandmaster_ns'])) for r in pairs if r['edge']=='rising']
        post=[d for t,d in rise if t>=120]
        if stats(post)!=summary['physical_pps']['rising']['post_cutoff']:
            raise ValueError('PPS summary mismatch: '+key)
        q=summary['pulse_qualification']['channels']
        entry={'name':NAMES[key],'duration_s':summary['capture_end_s'],
               'firmware_sha256':manifest['firmware_sha256'][trial['example']],
               'physical_pps':summary['physical_pps'],'serial':summary['serial'],
               'serial_startup_since_first_exchange':metrics(original),
               'physical_startup_since_capture':{
                   'first_matched_rising_s':rise[0][0],
                   'first_matched_rising_separation_ns':rise[0][1],
                   'final_contiguous_suffix_1000ns_s':physical_settling(rise,1000,summary['capture_end_s']),
                   'final_contiguous_suffix_100ns_s':physical_settling(rise,100,summary['capture_end_s'])},
               'quality':{ch:{'raw_rising':v['raw_rising_count'],
                             'qualified_complete_pulses':v['accepted_complete_pulses'],
                             'rejected_complete_segments':len(v['rejected_complete_pulses']),
                             'post_cutoff_rejected_complete_segments':sum(float(p['rise_s'])>=120 for p in v['rejected_complete_pulses']),
                             'post_cutoff_short_high_segments':sum(float(p['rise_s'])>=120 and float(p['width_ns'])<1000 for p in v['rejected_complete_pulses']),
                             'excluded_rising':len(v['excluded_rising_s']),
                             'unmatched_qualified_rising':len(summary['edges']['rising']['unmatched_ch'+ch+'_seconds'])}
                          for ch,v in q.items()},
               'reported_counter_maxima':summary['reported_counter_maxima']}
        results[key]=entry;traces[key]=(pairs,serial)
    if not allow_incomplete and set(results)!=set(NAMES):
        raise ValueError('Expected five short servo trials and longer PI run')
    return manifest,results,traces


def plots(output,results,traces):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    plt.rcParams.update({'font.family':'DejaVu Sans','font.size':10,'svg.hashsalt':'tinyptp-saleae-servos'})
    keys=[k for k in NAMES if k!='pi-steady' and k in results]
    if not keys:return
    fig,axes=plt.subplots(2,1,figsize=(12,8),sharex=True,layout='constrained')
    for key,color in zip(keys,COLORS):
        pairs,serial=traces[key]
        p=[r for r in pairs if r['edge']=='rising' and int(r['post_cutoff'])]
        s=[r for r in serial if float(r['host_receive_elapsed_s'])>=120]
        axes[0].plot([float(r['ch0_s']) for r in p],[float(r['follower_minus_grandmaster_ns']) for r in p],'.',ms=3,color=color,label=NAMES[key])
        axes[1].plot([float(r['host_receive_elapsed_s']) for r in s],[int(r['offset_ns']) for r in s],'.',ms=3,color=color)
    axes[0].set_ylabel('Physical rising PPS separation (ns)');axes[0].legend(ncol=3,fontsize=9)
    axes[1].set_ylabel('Internal PTP offset (ns)');axes[1].set_xlabel('Time from each Saleae capture start (s)')
    for ax in axes:ax.grid(alpha=.2)
    fig.suptitle('Five-servo physical PPS and serial comparison\nSingle sequential trials; common 120 s cutoff',fontsize=14)
    fig.text(.5,-.07,'PPS: follower edge minus grandmaster edge. Serial clock phase has the opposite sign.\nQualified pulses only; no skew correction or phase outlier removal. Missing points can reflect pulse exclusions.',ha='center',fontsize=9)
    save_figure(fig,output/'servo-steady-comparison');plt.close(fig)
    fig,axes=plt.subplots(len(keys),2,figsize=(12,2.5*len(keys)),squeeze=False,sharex=True,layout='constrained')
    maximum=max([1e9]+[abs(int(r['offset_ns'])) for k in keys for r in traces[k][1]
                       if 0<=float(r['host_receive_elapsed_s'])<=120])
    ymax=10**math.ceil(math.log10(maximum))
    for row,(key,color) in enumerate(zip(keys,COLORS)):
        pairs,serial=traces[key]
        p=[r for r in pairs if r['edge']=='rising' and float(r['ch0_s'])<=120]
        s=[r for r in serial if 0<=float(r['host_receive_elapsed_s'])<=120]
        axes[row,0].plot([float(r['ch0_s']) for r in p],[max(1,abs(float(r['follower_minus_grandmaster_ns']))) for r in p],'.',ms=3,color=color)
        axes[row,1].plot([float(r['host_receive_elapsed_s']) for r in s],[max(1,abs(int(r['offset_ns']))) for r in s],'.',ms=3,color=color)
        axes[row,0].set_ylabel(NAMES[key]+'\nAbsolute separation (ns)')
        for ax in axes[row]:ax.set_yscale('log');ax.set_ylim(1,ymax);ax.set_xlim(0,120);ax.grid(alpha=.2)
    axes[0,0].set_title('Physical PPS, qualified rising pairs');axes[0,1].set_title('Internal PTP offset, host receipt time')
    for ax in axes[-1]:ax.set_xlabel('Time from each Saleae capture start (s)')
    fig.suptitle('Startup transients after follower uploads\nInitial phases differ; exact zeros displayed at 1 ns only on logarithmic axes',fontsize=14)
    fig.text(.5,-.65/(2.5*len(keys)),'Physical pairing is limited to +/-100 ms and cannot resolve integer-second phase.\nUnmatched edges and duration exclusions are archived; no interpolation over missing observations.',ha='center',fontsize=9)
    save_figure(fig,output/'servo-startup-comparison');plt.close(fig)


def tables(output, results):
    lines=['# Derived Saleae campaign tables', '', 'All timing values are in nanoseconds. Physical values use qualified rising PPS pairs after 120 s from capture start.', '',
           '| Condition | PPS pairs | PPS mean | PPS SD | PPS RMS | PPS min / max | Internal RMS |',
           '| --- | ---: | ---: | ---: | ---: | ---: | ---: |']
    for key,v in results.items():
        p=v['physical_pps']['rising']['post_cutoff'];q=v['serial']['post_cutoff']
        lines.append(f"| {v['name']} | {p['count']} | {p['mean_ns']:+.2f} | {p['stddev_population_ns']:.2f} | {p['rms_about_zero_ns']:.2f} | {p['min_ns']:+.0f} / {p['max_ns']:+.0f} | {q['rms_about_zero_ns']:.2f} |")
    lines += ['', '## Pulse qualification, entire capture', '',
              'These are edge and segment counts, not counts of validated seconds. Boundary partial pulses and startup changes can also be excluded.', '',
              '| Condition | CH0 raw rising | CH1 raw rising | CH0 qualified pulses | CH1 qualified pulses | CH0 / CH1 rejected complete segments | CH0 / CH1 unmatched qualified rises | CH0 / CH1 short highs after cutoff |',
              '| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |']
    for key,v in results.items():
        a,b=v['quality']['0'],v['quality']['1']
        lines.append(f"| {v['name']} | {a['raw_rising']} | {b['raw_rising']} | {a['qualified_complete_pulses']} | {b['qualified_complete_pulses']} | {a['rejected_complete_segments']} / {b['rejected_complete_segments']} | {a['unmatched_qualified_rising']} / {b['unmatched_qualified_rising']} | {a['post_cutoff_short_high_segments']} / {b['post_cutoff_short_high_segments']} |")
    lines += ['', 'Short highs are recorded high segments below 1 microsecond, not a count of every possible disturbance. All rejected segments and edge timestamps are listed in each trial summary.']
    lines += ['', '## Startup observations', '',
              'Serial times below are from the first accepted exchange. Physical times are from Saleae capture start. A physical final suffix requires at least 30 s of in-band observations, no gap greater than 1.5 s, and observations through the capture end. ND means not demonstrated, including because pulse exclusions interrupt the observable record.', '',
              '| Condition | Initial serial offset | Serial first s3 (s) | Physical final +/-1000 ns suffix start (s) | Physical final +/-100 ns suffix start (s) |',
              '| --- | ---: | ---: | ---: | ---: |']
    def fmt(x):return 'ND' if x is None else f'{x:.2f}'
    for key,v in results.items():
        a=v['serial_startup_since_first_exchange'];b=v['physical_startup_since_capture']
        lines.append(f"| {v['name']} | {a['initial_offset_ns']:+d} | {fmt(a['first_s3_s'])} | {fmt(b['final_contiguous_suffix_1000ns_s'])} | {fmt(b['final_contiguous_suffix_100ns_s'])} |")
    (output/'tables.md').write_text('\n'.join(lines)+'\n')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    parser.add_argument('--output-dir',type=Path,required=True)
    parser.add_argument('--allow-incomplete',action='store_true')
    args=parser.parse_args();args.output_dir.mkdir(parents=True,exist_ok=True)
    manifest,results,traces=analyze(args.directory,args.output_dir,args.allow_incomplete)
    result={'campaign_status':manifest['status'],'cutoff_s':120,
            'cutoff_origin':'Saleae capture start; serial aligned approximately by host receipt',
            'qualification_origin':'Rule carried forward from earlier Saleae measurements and recorded in this campaign plan before outcome analysis.',
            'calibration':'No channel/cable correction or traceable calibration',
            'trials':results}
    (args.output_dir/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    plots(args.output_dir,results,traces)
    tables(args.output_dir,results)
    print(json.dumps({k:{'pps_rms_ns':v['physical_pps']['rising']['post_cutoff']['rms_about_zero_ns'],
                         'pps_pairs':v['physical_pps']['rising']['post_cutoff']['count'],
                         'serial_rms_ns':v['serial']['post_cutoff']['rms_about_zero_ns']}
                      for k,v in results.items()},indent=2))


if __name__=='__main__':main()
