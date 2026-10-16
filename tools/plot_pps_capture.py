#!/usr/bin/env python3
"""Verify, analyze, and plot physical PPS capture with simultaneous serial logs.

Run on a complete capture_pps_saleae.py output directory. Output includes all
pairs and separate full-record/post-cutoff statistics. Optional pulse-duration
qualification is explicitly reported; no phase-based outlier removal is used.
"""
import argparse
import csv
from datetime import datetime, timezone
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re
import struct
from zipfile import ZipFile

from analyze_pps import analyze, read_edges, stats


def verify_capture(directory):
    directory=Path(directory)
    manifest=json.loads((directory/'capture-manifest.json').read_text())
    if manifest['status']!='complete' or manifest['serial_errors']:
        raise ValueError('Capture must be complete with no serial-reader errors')
    if manifest['device']['is_simulation']:
        raise ValueError('Simulated devices cannot supply a physical PPS measurement')
    for name,expected in manifest['sha256'].items():
        if Path(name).name != name:
            raise ValueError('Invalid manifest filename')
        if hashlib.sha256((directory/name).read_bytes()).hexdigest()!=expected:
            raise ValueError('Capture checksum mismatch: '+name)
    with ZipFile(directory/'pps.sal') as z:
        meta=json.loads(z.read('meta.json'))['data']
    if meta['legacyDevice']['isSimulation']:
        raise ValueError('Native capture contains simulated data')
    if int(meta['legacyDevice']['deviceId']) != int(manifest['device']['device_id'],16):
        raise ValueError('Native capture and manifest device IDs differ')
    settings=meta['legacySettings']
    if settings['sampleRate']['digital']!=manifest['digital_sample_rate_hz']:
        raise ValueError('Native capture and manifest sample rates differ')
    if settings['enabledChannels'] != [{'type':'Digital','index':0},{'type':'Digital','index':1}]:
        raise ValueError('Expected only digital channels 0 and 1')
    if settings['glitchFilter']['enabled']:
        raise ValueError('Glitch filtering is not supported for this measurement')
    csv_edges=read_edges(directory/'digital.csv')
    binary_info={}
    for ch in (0,1):
        b=(directory/f'digital_{ch}.bin').read_bytes()
        if len(b)<44:raise ValueError('Truncated binary export')
        magic,version,kind,initial,begin,end,count=struct.unpack('<8siiIddQ',b[:44])
        if (magic,version,kind)!=(b'<SALEAE>',0,0) or initial not in (0,1):
            raise ValueError('Expected version-0 digital binary export')
        if len(b)!=44+8*count:raise ValueError('Binary export length mismatch')
        transitions=struct.unpack('<'+'d'*count,b[44:])
        edge_lists={'rising':[],'falling':[]};state=initial
        for t in transitions:
            state=1-state
            edge_lists['rising' if state else 'falling'].append(t)
        for kind in edge_lists:
            a=edge_lists[kind];c=csv_edges[ch][kind]
            if len(a)!=len(c) or any(abs(x-float(y))>0.6e-9 for x,y in zip(a,c)):
                raise ValueError('CSV and binary edge exports differ')
        binary_info[str(ch)]={'initial_state':initial,'begin_s':begin,'end_s':end,'transitions':count}
    return manifest,meta,binary_info


SAMPLE=re.compile(rb'tinyptp\[(\d+\.\d+)\]: master offset (-?\d+) s([0-3]) freq ([+-]?\d+) path delay (-?\d+) sample (\d+) seq (\d+)')


def serial_samples(directory, meta):
    stamps=[json.loads(s) for s in (directory/'serial-receive-times.jsonl').read_text().splitlines()]
    start=meta['captureStartTime']
    start_s=Decimal(start['unixTimeMilliseconds'])/1000+Decimal(str(start.get('fractionalMilliseconds',0)))/1000
    samples=[];events=[]
    for path in sorted(directory.glob('serial-*.log')):
        index=int(path.stem.split('-')[1]);raw=path.read_bytes()
        chunks=[s for s in stamps if s['port_index']==index]
        for match in SAMPLE.finditer(raw):
            chunk=next((c for c in chunks if c['file_offset']<=match.end()-1<c['file_offset']+c['bytes']),None)
            if chunk is None:raise ValueError('Serial sample has no host receipt timestamp')
            dt=datetime.fromisoformat(chunk['received_utc']).astimezone(timezone.utc)
            # Use timestamp only for display alignment, not physical edge timing.
            receipt_s=Decimal(str(dt.timestamp()))
            samples.append({'port_index':index,'host_receive_elapsed_s':float(receipt_s-start_s),
                            'uptime_s':float(match[1]),'offset_ns':int(match[2]),'state':int(match[3]),
                            'frequency_ppb':int(match[4]),'path_ns':int(match[5]),'sample':int(match[6])})
        text=raw.decode(errors='replace')
        for needle in ('boot role','clock stepped','initialization failed','PTP rejected','USB disconnected','link down'):
            lines=[line for line in text.splitlines() if needle in line]
            if lines:events.append({'port_index':index,'kind':needle,'lines':lines})
    if len({s['port_index'] for s in samples})>1:
        raise ValueError('Multiple followers found; select one explicitly')
    return samples,events


def save_figure(fig, stem):
    for ext in ('png', 'svg'):
        path = stem.with_suffix('.' + ext)
        fig.savefig(path, dpi=180, bbox_inches='tight')
        if ext == 'svg':
            path.write_text('\n'.join(line.rstrip() for line in path.read_text().splitlines()) + '\n')


def plot_same_source(args, manifest, meta, binary_info, result, pairs):
    """Archive an empirical measurement-path check, without correcting PTP data."""
    result['purpose']='Same PPS source on both inputs; empirical channel/lead delay check, not a PTP measurement or traceable calibration'
    result['wiring_evidence']=manifest['mapping_evidence']
    result['verified_native_settings']=meta['legacySettings']
    result['verified_binary_exports']=binary_info
    result['capture_end_s']=min(v['end_s'] for v in binary_info.values())
    result['correction_applied_to_baseline']=False
    args.output_dir.mkdir(parents=True,exist_ok=True)
    (args.output_dir/'pps-summary.json').write_text(json.dumps(result,indent=2)+'\n')
    with (args.output_dir/'pps-pairs.csv').open('w',newline='') as f:
        w=csv.writer(f,lineterminator='\n');w.writerow(['edge','ch0_s','ch1_s','ch1_minus_ch0_ns'])
        for direction,items in pairs.items():
            for item in items:w.writerow([direction,*map(str,item)])
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,ax=plt.subplots(figsize=(10,4),layout='constrained')
    for direction,color in [('rising','#2563eb'),('falling','#c2410c')]:
        ax.plot([float(p[0]) for p in pairs[direction]],[float(p[2]) for p in pairs[direction]],'.',color=color,label=direction.title())
    ax.set_xlabel('Time from capture start (s)');ax.set_ylabel('CH1 minus CH0 (ns)')
    ax.set_title('Same PPS source on both Saleae inputs\nMeasurement paths unchanged except CH0 moved to the CH1 source')
    ax.grid(alpha=.2);ax.legend()
    fig.text(.5,-.05,'Uncorrected sampled edge differences. No correction applied to the original PTP result; not a traceable calibration.',ha='center',fontsize=9)
    save_figure(fig,args.output_dir/'same-source-check')
    plt.close(fig)
    print(json.dumps(result['edges'],indent=2))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture_dir',type=Path)
    parser.add_argument('--output-dir',type=Path,required=True)
    parser.add_argument('--after',type=float,default=120)
    parser.add_argument('--qualify-500ms',action='store_true')
    parser.add_argument('--same-source',action='store_true',help='Both inputs share one PPS source; report CH1 minus CH0 without PTP role interpretation')
    args=parser.parse_args()
    if args.after<0:parser.error('Cutoff cannot be negative')
    manifest,meta,binary_info=verify_capture(args.capture_dir)
    result,pairs=analyze(args.capture_dir/'digital.csv',manifest['digital_sample_rate_hz'],args.qualify_500ms)
    if args.same_source:
        plot_same_source(args,manifest,meta,binary_info,result,pairs)
        return
    roles=manifest['channel_roles']
    if not isinstance(roles,dict) or set(roles.values())!={'follower','grandmaster'}:
        raise ValueError('Establish follower/grandmaster channel mapping before plotting')
    follower=int(next(ch for ch,role in roles.items() if role=='follower'))
    sign=1 if follower==1 else -1
    result['channel_roles']=roles
    result['mapping_evidence']=manifest['mapping_evidence']
    result['verified_native_settings']=meta['legacySettings']
    result['verified_binary_exports']=binary_info
    result['physical_sign']='follower edge minus grandmaster edge; positive means follower edge is later'
    result['after_s']=args.after
    result['physical_pps']={}
    for direction,items in pairs.items():
        post=[p for p in items if p[0]>=Decimal(str(args.after))]
        result['physical_pps'][direction]={'full':stats(sign*p[2] for p in items),
                                        'post_cutoff':stats(sign*p[2] for p in post)}
        if len(post)<2:raise ValueError('Insufficient post-cutoff PPS data')
    serial_all,events=serial_samples(args.capture_dir,meta)
    capture_end=min(v['end_s'] for v in binary_info.values())
    serial=[s for s in serial_all if 0 <= s['host_receive_elapsed_s'] <= capture_end]
    postserial=[s for s in serial if s['host_receive_elapsed_s']>=args.after]
    result['serial']={'samples':len(serial),'outside_capture_interval_samples':len(serial_all)-len(serial),'post_cutoff':stats(s['offset_ns'] for s in postserial),
                      'states_post_cutoff':{str(k):sum(s['state']==k for s in postserial) for k in range(4)},
                      'sample_number_gaps':sum(max(0,b['sample']-a['sample']-1) for a,b in zip(serial,serial[1:])),
                      'nonincreasing_sample_ids':sum(b['sample']<=a['sample'] for a,b in zip(serial,serial[1:])),
                      'events':events,
                      'uptime_interval_max_s':max((b['uptime_s']-a['uptime_s'] for a,b in zip(serial,serial[1:])),default=None),
                      'time_alignment':'Serial host receive times are approximate; samples are not paired with individual PPS edges.'}
    result['capture_end_s']=capture_end
    counters=[]
    for path in args.capture_dir.glob('serial-*.log'):
        for a,b,c in re.findall(r'timeouts (\d+) missing_rx (\d+) discarded (\d+)',path.read_text()):
            counters.append([int(a),int(b),int(c)])
    result['reported_counter_maxima']=dict(zip(['timeouts','missing_rx','discarded'],map(max,zip(*counters)))) if counters else None
    args.output_dir.mkdir(parents=True,exist_ok=True)
    (args.output_dir/'pps-summary.json').write_text(json.dumps(result,indent=2)+'\n')
    with (args.output_dir/'pps-pairs.csv').open('w',newline='') as f:
        w=csv.writer(f,lineterminator='\n');w.writerow(['edge','ch0_s','ch1_s','ch1_minus_ch0_ns','follower_minus_grandmaster_ns','post_cutoff'])
        for direction,items in pairs.items():
            for a,b,d in items:w.writerow([direction,str(a),str(b),str(d),str(sign*d),int(a>=Decimal(str(args.after)))])
    with (args.output_dir/'serial-samples.csv').open('w',newline='') as f:
        if serial:
            w=csv.DictWriter(f,fieldnames=list(serial[0]),lineterminator='\n');w.writeheader();w.writerows(serial)
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    plt.rcParams.update({'font.family':'DejaVu Sans','font.size':10,'svg.hashsalt':'tinyptp-pps'})
    fig,axes=plt.subplots(3,1,figsize=(11,9),sharex=True,layout='constrained')
    for direction,color in [('rising','#2563eb'),('falling','#c2410c')]:
        items=pairs[direction]
        axes[0].plot([float(p[0]) for p in items],[float(sign*p[2]) for p in items],'.',ms=2,color=color,label=direction.title())
    axes[0].set_ylabel('PPS edge separation (ns)');axes[0].legend(ncol=2)
    axes[1].plot([s['host_receive_elapsed_s'] for s in serial],[s['offset_ns'] for s in serial],'.-',color='#0f766e',ms=2,lw=.6)
    axes[1].set_ylabel('Internal PTP offset (ns)')
    axes[2].plot([s['host_receive_elapsed_s'] for s in serial],[s['frequency_ppb'] for s in serial],color='#7c3aed',lw=.8)
    axes[2].set_ylabel('Frequency command (ppb)');axes[2].set_xlabel('Time from Saleae capture start (s)')
    for ax in axes:
        ax.grid(alpha=.2);ax.axvspan(0,args.after,color='gray',alpha=.12)
        if args.qualify_500ms:
            for channel in result['pulse_qualification']['channels'].values():
                for rejected in channel['rejected_complete_pulses']:
                    a,b=float(rejected['rise_s']),float(rejected['fall_s'])
                    if b-a<.001:
                        ax.axvline(a,color='red',alpha=.12,lw=.8)
                    else:
                        ax.axvspan(a,b,color='red',alpha=.12)
    fig.suptitle('Physical PPS timing and simultaneous PTP telemetry\nPPS: follower edge minus grandmaster edge; serial offset has the opposite phase sign',fontsize=13)
    qualifier='High and preceding low must each span 490-510 ms; red marks rejected segments. ' if args.qualify_500ms else ''
    fig.text(.5,-.055,f"{manifest['digital_sample_rate_hz']/1e6:g} MS/s ({result['sample_interval_ns']:g} ns nominal sample interval). No skew correction. Gray: first {args.after:g} s. Serial times: host receipt.\n"+qualifier,ha='center',fontsize=9)
    save_figure(fig,args.output_dir/'pps-and-serial')
    # Preserve the full trace and add a view where the acquisition step cannot
    # hide nanosecond-scale post-cutoff behavior. No samples are removed from
    # the archive or the full-record statistics.
    post_values = [float(sign*p[2]) for direction in pairs for p in pairs[direction]
                   if float(p[0])>=args.after]
    for ax,values in zip(axes,[post_values,[s['offset_ns'] for s in postserial],
                               [s['frequency_ppb'] for s in postserial]]):
        ax.set_xlim(args.after,capture_end)
        if values:
            low,high=min(values),max(values);padding=max(1,(high-low)*.08)
            ax.set_ylim(low-padding,high+padding)
    fig.suptitle('Post-cutoff physical PPS timing and simultaneous PTP telemetry\nPPS: follower edge minus grandmaster edge; serial offset has the opposite phase sign',fontsize=13)
    save_figure(fig,args.output_dir/'pps-steady-and-serial')
    plt.close(fig)
    # Deterministic representative: pair closest to the median post-cutoff rising separation.
    rising=[p for p in pairs['rising'] if p[0]>=Decimal(str(args.after))]
    median=result['physical_pps']['rising']['post_cutoff']['median_ns']
    chosen=min(rising,key=lambda p:abs(float(sign*p[2])-median))
    physical=float(sign*chosen[2]);pad=max(30,abs(physical)*.5)
    lo=min(0,physical)-pad;hi=max(0,physical)+pad
    fig,ax=plt.subplots(figsize=(9,3),layout='constrained')
    for edge,base,label,color in [(0,1.5,'Grandmaster','#c2410c'),(physical,0,'Follower','#2563eb')]:
        ax.step([lo,edge,hi],[base,base+1,base+1],where='post',lw=2,label=label,color=color)
    ax.set_yticks([.5,2],['Follower','Grandmaster']);ax.set_xlabel('Time relative to grandmaster rising edge (ns)')
    ax.set_title(f'Recorded digital transitions: follower minus grandmaster = {physical:g} ns')
    ax.grid(axis='x',alpha=.2);ax.set_xlim(lo,hi)
    fig.text(.5,-.025,f'Pair selected nearest the post-cutoff median; CH0 timestamp {chosen[0]} s. Digital levels, not analog edge shape.',ha='center',fontsize=8)
    save_figure(fig,args.output_dir/'pps-edge-detail')
    plt.close(fig)
    if args.qualify_500ms:
        # Show detected short high segments directly from the unmodified export.
        candidates=sorted(Decimal(p['rise_s']) for channel in result['pulse_qualification']['channels'].values()
                          for p in channel['rejected_complete_pulses'] if Decimal(p['width_ns'])<1000)
        centers=[]
        for t in candidates:
            if not centers or t-centers[-1]>Decimal('.000001'):
                centers.append(t)
        if centers:
            with (args.capture_dir/'digital.csv').open() as f:
                raw=list(csv.DictReader(f))
            shown=centers[:6]
            fig,axs=plt.subplots(len(shown),1,figsize=(10,3*len(shown)),squeeze=False,layout='constrained')
            for ax,center in zip(axs[:,0],shown):
                lo=center-Decimal('.000000020');hi=center+Decimal('.000000080')
                for ch,base,color in [(0,0,'#2563eb'),(1,1.5,'#c2410c')]:
                    previous=next(int(row[f'Channel {ch}']) for row in reversed(raw) if Decimal(row['Time [s]'])<lo)
                    x=[-20.];y=[base+previous]
                    for row in raw:
                        t=Decimal(row['Time [s]'])
                        if lo<=t<=hi:
                            x.append(float((t-center)*Decimal(1_000_000_000)))
                            y.append(base+int(row[f'Channel {ch}']))
                    ax.step(x+[80.],y+[y[-1]],where='post',color=color,lw=1.5)
                ax.set_yticks([.5,2],['CH0 follower','CH1 grandmaster'])
                ax.set_xlabel(f'Time relative to {center} s (ns)');ax.grid(axis='x',alpha=.2)
            fig.suptitle(f'Extra recorded transitions: first {len(shown)} of {len(centers)} short-segment clusters\nRaw digital samples; cause undetermined. All exclusions remain in the summary.',fontsize=13)
            save_figure(fig,args.output_dir/'extra-transitions')
            plt.close(fig)
    print(json.dumps({'pps':result['physical_pps'],'serial':result['serial']},indent=2))


if __name__=='__main__':main()
