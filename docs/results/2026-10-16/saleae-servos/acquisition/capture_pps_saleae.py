#!/usr/bin/env python3
"""Capture real Saleae CH0/CH1 PPS signals with simultaneous serial logs.

Requires a running Logic 2 automation server, logic2-automation, and pyserial.
Never selects a simulated device. Does not reflash, reset, or command the UNOs.
"""
import argparse
import dataclasses
from datetime import datetime, timezone
import hashlib
import importlib.metadata
import json
from pathlib import Path
import platform
import subprocess
import threading
import time


def utc():
    return datetime.now(timezone.utc).isoformat()


def main():
    from saleae import automation
    import serial
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',type=Path,required=True)
    parser.add_argument('--duration',type=float,default=900)
    parser.add_argument('--sample-rate',type=int,default=500_000_000)
    parser.add_argument('--logic-level',type=float,default=3.3)
    parser.add_argument('--serial-port',action='append',default=[])
    parser.add_argument('--preserve-serial-buffer',action='store_true',
                        help='Retain buffered startup output when opening serial ports immediately after upload')
    parser.add_argument('--device-id')
    parser.add_argument('--ch0-role',choices=['unassigned','follower','grandmaster'],default='unassigned')
    parser.add_argument('--ch1-role',choices=['unassigned','follower','grandmaster'],default='unassigned')
    parser.add_argument('--mapping-evidence',default='not established')
    args=parser.parse_args()
    if args.duration<=0 or args.sample_rate<=0:parser.error('Duration and sample rate must be positive')
    out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=False)
    manifest={'status':'starting','created_utc':utc(),'duration_requested_s':args.duration,
              'digital_channels':[0,1],'digital_sample_rate_hz':args.sample_rate,
              'digital_threshold_volts_api':args.logic_level,
              'glitch_filters':[],'channel_roles':{'0':args.ch0_role,'1':args.ch1_role},
              'mapping_evidence':args.mapping_evidence,
              'calibration':'none: no channel/cable skew correction',
              'host_platform':platform.platform(),
              'packages':{p:importlib.metadata.version(p) for p in ['logic2-automation','pyserial']},
              'serial_ports':args.serial_port,'baud':115200,
              'serial_input_buffer_cleared':not args.preserve_serial_buffer,
              'serial_timing':'Host receive timestamps are not hardware edge timestamps.'}
    def save():
        (out/'capture-manifest.json').write_text(json.dumps(manifest,indent=2,default=str)+'\n')
    save();stop=threading.Event();serial_errors=[];ports=[];files=[];reader=None;awake=None
    try:
        if platform.system()=='Darwin':
            awake=subprocess.Popen(['caffeinate','-disu'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        for i,name in enumerate(args.serial_port):
            port=serial.Serial(name,115200,timeout=0,exclusive=True)
            if not args.preserve_serial_buffer:
                port.reset_input_buffer()
            ports.append(port);files.append((out/f'serial-{i}.log').open('xb'))
        def read_serial():
            with (out/'serial-receive-times.jsonl').open('x') as stamps:
                try:
                    while not stop.is_set():
                        for i,(port,f) in enumerate(zip(ports,files)):
                            data=port.read(65536)
                            if data:
                                offset=f.tell();f.write(data);f.flush()
                                stamps.write(json.dumps({'port_index':i,'file_offset':offset,'bytes':len(data),
                                                         'received_utc':utc(),'host_monotonic_ns':time.monotonic_ns()})+'\n')
                                stamps.flush()
                        stop.wait(.02)
                except Exception as e:
                    serial_errors.append(repr(e))
        reader=threading.Thread(target=read_serial);reader.start()
        with automation.Manager.connect(port=10430,connect_timeout_seconds=10) as manager:
            manifest['logic_app']=dataclasses.asdict(manager.get_app_info())
            devices=manager.get_devices(include_simulation_devices=False)
            if args.device_id:devices=[d for d in devices if d.device_id==args.device_id]
            if len(devices)!=1 or devices[0].is_simulation:
                raise RuntimeError(f'Expected exactly one real device, got {devices!r}')
            device=devices[0];manifest['device']=dataclasses.asdict(device)
            config=automation.LogicDeviceConfiguration(enabled_digital_channels=[0,1],
                digital_sample_rate=args.sample_rate,digital_threshold_volts=args.logic_level)
            capture_config=automation.CaptureConfiguration(capture_mode=automation.TimedCaptureMode(duration_seconds=args.duration))
            manifest['capture_requested_utc']=utc();manifest['status']='capturing';save()
            with manager.start_capture(device_id=device.device_id,device_configuration=config,
                                       capture_configuration=capture_config) as capture:
                capture.wait();manifest['capture_completed_utc']=utc();manifest['status']='exporting';save()
                capture.save_capture(filepath=str(out/'pps.sal'))
                capture.export_raw_data_csv(directory=str(out),digital_channels=[0,1])
                capture.export_raw_data_binary(directory=str(out),digital_channels=[0,1])
        manifest['status']='complete'
    except BaseException as e:
        manifest['status']='failed';manifest['error']=repr(e)
        raise
    finally:
        stop.set()
        if reader:reader.join(timeout=5)
        for f in files:f.close()
        for port in ports:port.close()
        if awake:awake.terminate();awake.wait(timeout=5)
        manifest['serial_errors']=serial_errors
        if serial_errors and manifest['status']=='complete':manifest['status']='serial-error'
        manifest['finished_utc']=utc()
        manifest['sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.iterdir()) if p.is_file() and p.name!='capture-manifest.json'}
        save()
    print(json.dumps(manifest,indent=2,default=str))


if __name__=='__main__':main()
