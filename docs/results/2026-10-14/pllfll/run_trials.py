import json,subprocess,time,re,signal
from pathlib import Path
from datetime import datetime,timezone
root=Path('/home/user/tinyPTP')
out=root/'test-results/pllfll-20261014'
ports={'master':'/dev/serial/by-id/usb-Arduino__www.arduino.cc__0043_1453230343235190E071-if00','follower':'/dev/serial/by-id/usb-Arduino__www.arduino.cc__0043_14533303731351B0C090-if00'}
cli=['/home/user/.local/share/tinyptp/bin/arduino-cli','--config-file','/home/user/.local/share/tinyptp/arduino-cli.yaml']
current='pi'
status={'started_utc':datetime.now(timezone.utc).isoformat(),'runs':[]}
def save():
 (out/'trial-status.json').write_text(json.dumps(status,indent=2)+'\n')
def upload(servo):
 global current
 # Set unknown before upload so a failure also triggers restoration.
 current='unknown'
 subprocess.run(cli+['upload','--fqbn','arduino:avr:uno','--port',ports['follower'],'--input-dir',str(root/('build/servo-'+servo)),str(root/'build/pllfll-source/TimeStickProbe')],check=True)
 current=servo;status['installed_follower']=servo;save()
def capture(name,servo):
 print('START '+name,flush=True)
 paths={role:out/(name+'-'+role+'.log') for role in ports}
 if any(p.exists() for p in paths.values()):raise RuntimeError('Refusing to overwrite '+name)
 jobs=[]
 try:
  for role in ports:
   jobs.append(subprocess.Popen(['python3',str(root/'tools/capture_serial.py'),ports[role],str(paths[role]),'--seconds','300','--reset'],stdout=subprocess.DEVNULL))
  start=time.monotonic();checked=False
  while any(p.poll() is None for p in jobs):
   time.sleep(1)
   if time.monotonic()-start>50 and not checked:
    checked=True
    text=paths['follower'].read_text()
    if 'master offset ' not in text:raise RuntimeError(name+' has no accepted exchanges after 50 seconds')
    print('RUNNING '+name+' accepted exchanges present',flush=True)
  if any(p.returncode for p in jobs):raise RuntimeError(name+' reader failure')
 finally:
  for p in jobs:
   if p.poll() is None:p.terminate()
  for p in jobs:p.wait()
 result={'name':name,'servo':servo,'capture_seconds':300,'roles':{}}
 for role,p in paths.items():
  text=p.read_text();counts={k:max(map(int,re.findall(k+r' (\d+)',text)),default=0) for k in ['timeouts','missing_rx','discarded']}
  if text.count('boot role')!=1 or any(counts.values()) or any(k in text for k in ['initialization failed','link down','USB disconnected']):raise RuntimeError(name+' invalid '+role+' capture')
  if 'enabled (register readback)' not in text:raise RuntimeError(name+' no PPS readback')
  result['roles'][role]={'counters':counts,'samples':text.count('master offset ')}
  if role=='follower':
   if result['roles'][role]['samples']<280:raise RuntimeError(name+' too few samples')
   expected='servo PI kp' if servo=='pi' else 'servo PLL_FLL window 8'
   if expected not in text:raise RuntimeError(name+' wrong servo')
 status['runs'].append(result);save();print('DONE '+name,flush=True)
def stop(signum,frame):raise KeyboardInterrupt()
signal.signal(signal.SIGTERM,stop)
try:
 capture('warmup-02','pi')
 for name,servo in [('pi-01','pi'),('pllfll-01','pllfll'),('pi-02','pi'),('pllfll-02','pllfll'),('pi-03','pi')]:
  if current!=servo:upload(servo)
  capture(name,servo)
 status['complete']=True
except BaseException as e:
 status['complete']=False;status['error']=str(e);print('FAILED '+repr(e),flush=True)
 raise
finally:
 if current!='pi':upload('pi')
 status['installed_follower']=current;status['ended_utc']=datetime.now(timezone.utc).isoformat();save()
