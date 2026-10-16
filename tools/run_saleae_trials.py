#!/usr/bin/env python3
"""Repeat the R4 servo campaign with simultaneous real PPS and serial captures.

Uses prebuilt, hash-verified firmware and the established USB board identities.
Uploads the grandmaster once, then five follower servos and a longer PI run.
Each Saleae capture starts immediately after follower upload; its first 120 s
are the declared acquisition exclusion. Initial clock phases are not controlled.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

import serial
from saleae import automation
from run_r4_trials import capture, port_for, upload, utc

TRIALS = [('pi', 'FollowerPI', 'PI'), ('linreg', 'FollowerLinreg', 'LINREG'),
          ('adaptive-pi', 'FollowerAdaptivePI', 'AUTO_PI'),
          ('kalman', 'FollowerKalman', 'KALMAN'),
          ('pllfll', 'FollowerPllFll', 'PLL_FLL'),
          ('pi-steady', 'FollowerPI', 'PI')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--master-serial', required=True)
    parser.add_argument('--follower-serial', required=True)
    parser.add_argument('--dfu-util', type=Path, required=True)
    parser.add_argument('--firmware-dir', type=Path, required=True)
    parser.add_argument('--expected-manifest', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--device-id', required=True)
    parser.add_argument('--seconds', type=int, default=300)
    parser.add_argument('--steady-seconds', type=int, default=900)
    args = parser.parse_args()
    if args.master_serial == args.follower_serial or min(args.seconds, args.steady_seconds) < 150:
        parser.error('Use distinct board identities and durations >= 150 seconds')
    expected = json.loads(args.expected_manifest.read_text())['firmware_sha256']
    hashes = {}
    for name in {'Grandmaster', *(t[1] for t in TRIALS)}:
        hashes[name] = hashlib.sha256((args.firmware_dir/(name+'.ino.bin')).read_bytes()).hexdigest()
        if hashes[name] != expected[name]:
            raise ValueError('Firmware differs from expected campaign: '+name)
    for identity in [args.master_serial, args.follower_serial]:
        with serial.Serial(port_for(identity), 115200, timeout=0, exclusive=True):
            pass
    with automation.Manager.connect(port=10430) as manager:
        devices = [d for d in manager.get_devices(include_simulation_devices=False)
                   if d.device_id == args.device_id and not d.is_simulation]
        if len(devices) != 1:
            raise RuntimeError('Required real Saleae device is unavailable')
    args.output_dir.mkdir(parents=True, exist_ok=False)
    manifest = {'status':'running', 'started_utc':utc(),
                'master_serial':args.master_serial, 'follower_serial':args.follower_serial,
                'firmware_sha256':hashes, 'device_id':args.device_id,
                'channel_roles':{'0':'follower','1':'grandmaster'},
                'sample_rate_hz':500000000, 'threshold_preset_volts':3.3,
                'analysis_cutoff_s':120, 'cutoff_origin':'Saleae capture start',
                'pulse_qualification':'Complete high and preceding low each 490 to 510 ms; preserve and enumerate all excluded transitions',
                'order':[t[0] for t in TRIALS], 'randomized':False, 'replicates_per_condition':1,
                'phase_control':'Upload startup, not an imposed equal phase step; master may reinitialize on link loss',
                'trials':[]}
    def save():
        temporary = args.output_dir/'manifest.json.tmp'
        temporary.write_text(json.dumps(manifest,indent=2)+'\n')
        temporary.replace(args.output_dir/'manifest.json')
    def interrupted(number, frame):
        raise KeyboardInterrupt('Campaign interrupted')
    signal.signal(signal.SIGINT, interrupted)
    signal.signal(signal.SIGTERM, interrupted)
    save()
    awake = subprocess.Popen(['/usr/bin/caffeinate','-is','-w',str(os.getpid())]) if sys.platform=='darwin' else None
    active = None
    try:
        master_port = upload(args,args.master_serial,'Grandmaster','grandmaster')
        capture(master_port,args.output_dir/'grandmaster-startup.log',30)
        for label,example,servo in TRIALS:
            duration = args.steady_seconds if label=='pi-steady' else args.seconds
            entry = {'label':label,'example':example,'expected_servo':servo,
                     'duration_s':duration,'started_utc':utc(),'status':'uploading'}
            manifest['trials'].append(entry);save()
            print(utc(), 'uploading', label, flush=True)
            follower_port = upload(args,args.follower_serial,example,label)
            entry['status']='capturing';entry['capture_started_utc']=utc();save()
            command = [sys.executable,str(Path(__file__).with_name('capture_pps_saleae.py')),
                       '--output-dir',str(args.output_dir/label),'--duration',str(duration),
                       '--device-id',args.device_id,'--preserve-serial-buffer',
                       '--serial-port',port_for(args.master_serial),'--serial-port',follower_port,
                       '--ch0-role','follower','--ch1-role','grandmaster',
                       '--mapping-evidence','Unchanged wiring after confirmed restoration: CH0 follower, CH1 grandmaster; startup mapping archived in saleae-pps/bringup/probe-02']
            with (args.output_dir/(label+'-capture.log')).open('x') as output:
                active = subprocess.Popen(command,stdout=output,stderr=subprocess.STDOUT)
                result = active.wait(timeout=duration+120);active=None
            if result:
                raise RuntimeError('Capture failed: '+label)
            record = json.loads((args.output_dir/label/'capture-manifest.json').read_text())
            raw = (args.output_dir/label/'serial-1.log').read_bytes()
            text = raw.decode(errors='replace')
            samples = [int(n) for n in re.findall(r' sample (\d+) seq ',text)]
            entry.update({'ended_utc':utc(),'serial_samples':len(samples),
                          'boots':len(re.findall(r': boot role FOLLOWER',text)),
                          'servo_matches':bool(re.search(r': servo '+servo+r'\b',text)),
                          'serial_sample_gaps':sum(max(0,b-a-1) for a,b in zip(samples,samples[1:])),
                          'capture_status':record['status'],'status':'captured'})
            save()
            if record['status']!='complete' or record['serial_errors'] or entry['boots']!=1 or not entry['servo_matches'] or len(samples)<duration-45:
                raise RuntimeError('Trial did not pass capture/startup checks: '+label)
            print(utc(),'captured',label,len(samples),'serial samples',flush=True)
        manifest['status']='complete'
    except BaseException as error:
        manifest['status']='failed';manifest['error']=repr(error)
        raise
    finally:
        if active and active.poll() is None:
            active.terminate()
            try:active.wait(timeout=15)
            except subprocess.TimeoutExpired:active.kill();active.wait()
        manifest['ended_utc']=utc()
        manifest['sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest()
                            for p in sorted(args.output_dir.iterdir())
                            if p.is_file() and p.name!='manifest.json'}
        save()
        if awake:
            awake.terminate();awake.wait(timeout=5)


if __name__=='__main__':
    main()
