#!/usr/bin/env python3
"""Upload and capture an UNO R4 Minima pair by USB serial identity.

Requires pyserial and Arduino's dfu-util. Flashes both selected boards, then
cycles the follower through five servos and a longer fixed-PI run. Leaves both
roles running. Use a dedicated pair; close other serial monitors first.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import threading
import time

import serial
from serial.tools import list_ports


def utc():
    return datetime.now(timezone.utc).isoformat()


def port_for(identity):
    ports = [p.device for p in list_ports.comports()
             if p.vid == 0x2341 and p.pid == 0x0069 and p.serial_number == identity]
    if len(ports) != 1:
        raise RuntimeError(f"Expected one R4 Minima for serial {identity}, found {ports}")
    return ports[0]


def upload(args, identity, example, label):
    firmware = args.firmware_dir / (example + '.ino.bin')
    with (args.output_dir / (label + '-upload.log')).open('x') as log:
        subprocess.run([str(args.dfu_util), '--device', '2341:0069,2341:0369',
                        '--serial', identity, '-D', str(firmware), '-a0', '-Q'],
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=90)
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        try:
            return port_for(identity)
        except RuntimeError:
            time.sleep(.1)
    raise RuntimeError(f"Serial port did not return for {identity}")


def capture(port, output, seconds, stop=None):
    with output.open('x') as log, serial.Serial(
            port, 115200, timeout=.2, write_timeout=2, exclusive=True) as uart:
        log.write(f'UTC: {utc()}\nPort: {port}; baud: 115200; native USB\n')
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline and not (stop and stop.is_set()):
            data = uart.read(uart.in_waiting or 1)
            if data:
                log.write(data.decode('ascii', errors='backslashreplace'))
                log.flush()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--master-serial', required=True)
    parser.add_argument('--follower-serial', required=True)
    parser.add_argument('--dfu-util', type=Path, required=True)
    parser.add_argument('--firmware-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=300)
    parser.add_argument('--steady-seconds', type=int, default=900)
    parser.add_argument('--master-warmup', type=int, default=30,
                        help='Seconds of master lead time before the first follower upload')
    args = parser.parse_args()

    def interrupted(_number, _frame):
        raise KeyboardInterrupt('Capture interrupted by signal')

    # Background shells may inherit SIGINT as ignored. Restore explicit,
    # graceful shutdown so serial sessions and sleep assertions are released.
    signal.signal(signal.SIGINT, interrupted)
    signal.signal(signal.SIGTERM, interrupted)
    if args.master_serial == args.follower_serial or min(args.seconds, args.steady_seconds) < 150:
        parser.error('Use different board identities and capture durations >= 150 seconds')
    if args.master_warmup < 0:
        parser.error('--master-warmup must be nonnegative')
    trials = [('pi', 'FollowerPI', args.seconds), ('linreg', 'FollowerLinreg', args.seconds),
              ('adaptive-pi', 'FollowerAdaptivePI', args.seconds),
              ('kalman', 'FollowerKalman', args.seconds),
              ('pllfll', 'FollowerPllFll', args.seconds),
              ('pi-steady', 'FollowerPI', args.steady_seconds)]
    for identity in [args.master_serial, args.follower_serial]:
        # Refuse to flash over an active exclusive monitor/capture session.
        with serial.Serial(port_for(identity), 115200, timeout=0, exclusive=True):
            pass
    images = {name: args.firmware_dir / (name + '.ino.bin')
              for name in ['Grandmaster'] + [t[1] for t in trials]}
    hashes = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in images.items()}
    args.output_dir.mkdir(parents=True, exist_ok=False)
    manifest = {'status': 'running', 'started_utc': utc(), 'master_serial': args.master_serial,
                'follower_serial': args.follower_serial, 'master_warmup_s': args.master_warmup,
                'firmware_sha256': hashes, 'trials': []}

    def save():
        (args.output_dir / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')

    save()
    stop = threading.Event()
    failures = []
    master_thread = None
    # Long captures should not be interrupted by the Mac's idle-sleep timer.
    # The assertion expires with this process; no persistent settings change.
    awake = subprocess.Popen(['/usr/bin/caffeinate', '-is', '-w', str(os.getpid())]) \
        if sys.platform == 'darwin' else None
    try:
        master_port = upload(args, args.master_serial, 'Grandmaster', 'grandmaster')

        def master_capture():
            try:
                capture(master_port, args.output_dir / 'grandmaster.log',
                        sum(t[2] for t in trials) + 900, stop)
            except Exception as error:
                failures.append(str(error))

        master_thread = threading.Thread(target=master_capture)
        master_thread.start()
        # Give the initial follower reset the same coarse-step conditions as
        # subsequent trials, when the master clock has already been running.
        time.sleep(args.master_warmup)
        for label, example, seconds in trials:
            print(f'{utc()} starting {label}, {seconds} seconds', flush=True)
            entry = {'label': label, 'example': example, 'seconds': seconds, 'started_utc': utc()}
            manifest['trials'].append(entry)
            save()
            port = upload(args, args.follower_serial, example, label)
            path = args.output_dir / (label + '.log')
            capture(port, path, seconds)
            content = path.read_text()
            entry.update({'ended_utc': utc(), 'log_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                          'samples': len(re.findall(r': master offset ', content)),
                          'boots': len(re.findall(r': boot role FOLLOWER', content))})
            save()
            if failures or entry['boots'] != 1 or entry['samples'] < 20:
                raise RuntimeError(f'Incomplete capture {label}: {entry}; master errors: {failures}')
            print(f'{utc()} completed {label}: {entry["samples"]} samples', flush=True)
        manifest['status'] = 'complete'
    except (Exception, KeyboardInterrupt) as error:
        manifest['status'] = 'failed'
        manifest['error'] = repr(error)
        raise
    finally:
        stop.set()
        if master_thread:
            master_thread.join(timeout=5)
        if failures:
            manifest['status'] = 'failed'
            manifest['master_capture_errors'] = failures
        manifest['ended_utc'] = utc()
        save()
        if awake:
            awake.terminate()
            awake.wait(timeout=5)


if __name__ == '__main__':
    main()
