#!/usr/bin/env python3
"""Bridge supervisor: scoped process identities, graceful stop, private session logs."""
import fcntl,json,mmap,os,signal,socket,struct,subprocess,sys,time
from pathlib import Path
from bridge_config import load_config, digest, state_dir
ROOT=Path(__file__).resolve().parent
try:CONFIG=load_config()
except ValueError as e:raise SystemExit(str(e))
GAME=Path(CONFIG['game_dir']).resolve()
HOST=ROOT/'build/lilith-host'
RUN=Path(os.environ.get('XDG_RUNTIME_DIR',f'/run/user/{os.getuid()}'))/'lilith-linux'
RUN.mkdir(mode=0o700,parents=True,exist_ok=True);os.chmod(RUN,0o700)
STATE=RUN/'state.json';SOCK=RUN/'control.sock'
SHM=Path('/dev/shm/lilith-linux-frame')
def identity(pid):
 try:
  p=Path('/proc')/str(pid);f=(p/'stat').read_text().rsplit(')',1)[1].split()
  if f[0]=='Z':return None
  return {'pid':int(pid),'start':int(f[19]),'name':(p/'comm').read_text().strip(),'cwd':os.readlink(p/'cwd'),'exe':os.readlink(p/'exe').removesuffix(' (deleted)')}
 except (OSError,ValueError):return None
def alive(item):
 p=identity(item['pid']) if item else None
 return p and p['start']==item['start']
def signal_exact(item,sig):
 if alive(item):os.kill(item['pid'],sig)
def find_owned(kind):
 result=[]
 for p in Path('/proc').glob('[0-9]*'):
  a=identity(p.name)
  if a and ((kind=='game' and a['name']=='Lilith.exe' and a['cwd']==str(GAME)) or (kind=='host' and a['exe']==str(HOST))):result.append(a)
 return result
def state():
 try:return json.loads(STATE.read_text())
 except (OSError,ValueError):return {}
cmd=sys.argv[1] if len(sys.argv)>1 else 'start'
if cmd=='status':
 s=state()
 if not alive(s.get('supervisor')):print('Lilith: stopped');sys.exit(0)
 print('Lilith:',s.get('phase'),'game=',s.get('game',{}).get('pid'),'host=',s.get('host',{}).get('pid'),'logs=',s.get('logs'));sys.exit(0)
if cmd=='stop':
 s=state()
 if alive(s.get('supervisor')) and s.get('bridge_root')!=str(ROOT):raise SystemExit('Another bridge installation owns the running session. Stop it using its own launcher first.')
 if not alive(s.get('supervisor')):print('Lilith: already stopped');sys.exit(0)
 c=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM);c.sendto(b'stop',str(SOCK));c.close()
 for _ in range(300):
  if not alive(s.get('supervisor')):print('Lilith: stopped');sys.exit(0)
  time.sleep(.1)
 print('Lilith: stop still in progress; see session logs',file=sys.stderr);sys.exit(1)
if cmd!='start':print('Usage: launch.sh [start|status|stop]',file=sys.stderr);sys.exit(2)
lock=open(RUN/'lock','a+')
try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
except BlockingIOError:print('Lilith: already running (launch.sh status)',file=sys.stderr);sys.exit(2)
existing=find_owned('game')+find_owned('host')
if existing:print('Lilith: unmanaged instance is running; close it first. PIDs:',','.join(str(p['pid']) for p in existing),file=sys.stderr);sys.exit(2)
if digest(GAME/'GameAssembly.dll')!=CONFIG['game_sha256']:raise SystemExit('Game build changed. Stop and re-run install.py to verify compatibility; no hooks were loaded.')
layout=json.loads((ROOT/'build/shm-layout.json').read_text())
if not (HOST.is_file() and (GAME/'Lilith.exe').is_file() and (GAME/'version.dll').is_file()):raise SystemExit('Build or game missing')
logs=state_dir()/'logs'/(time.strftime('%Y%m%d-%H%M%S')+'-'+str(time.time_ns()%1000000000));logs.mkdir(mode=0o700,parents=True,exist_ok=False)
os.umask(0o077)
control=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM)
try:SOCK.unlink()
except FileNotFoundError:pass
control.bind(str(SOCK));control.setblocking(False)
stop=False
signal.signal(signal.SIGTERM,lambda *_:globals().__setitem__('stop',True));signal.signal(signal.SIGINT,lambda *_:globals().__setitem__('stop',True))
records={'bridge_root':str(ROOT),'supervisor':identity(os.getpid()),'phase':'starting','logs':str(logs)}
def save():
 temp=STATE.with_suffix('.tmp');temp.write_text(json.dumps(records));temp.replace(STATE)
def log(message):
 line=time.strftime('%H:%M:%S')+' '+message;print(line,flush=True)
 with open(logs/'supervisor.log','a') as f:f.write(line+'\n')
children=[];game=None;host=None;mapping=None;streams=[];exit_code=0
try:
 fd=os.open(SHM,os.O_RDWR|os.O_CREAT|os.O_NOFOLLOW,0o600)
 with os.fdopen(fd,'r+b') as f:
  if os.fstat(f.fileno()).st_uid!=os.getuid():raise RuntimeError('Shared channel belongs to a different user')
  os.fchmod(f.fileno(),0o600);f.truncate(layout['file_size']);mapping=mmap.mmap(f.fileno(),4096);mapping[:]=bytes(4096)
 env=os.environ.copy();env['QT_WAYLAND_SHELL_INTEGRATION']='layer-shell'
 for name,args in [('host',[str(HOST)]),('proton',[sys.executable,str(ROOT/'run-game.py')]+sys.argv[2:])]:
  output=open(logs/(name+'.log'),'wb');streams.append(output)
  child=subprocess.Popen(args,stdout=output,stderr=subprocess.STDOUT,env=env,start_new_session=True);children.append(child)
  if name=='host':host=identity(child.pid);records['host']=host
 save();deadline=time.monotonic()+60
 while not stop:
  try:
   if control.recv(64)==b'stop':stop=True;break
  except BlockingIOError:pass
  if not alive(host):log('Presenter exited; closing this game');exit_code=1;break
  found=find_owned('game')
  if game is None and found:
   game=found[0];records['game']=game;save()
  if game and not alive(game):log('Game exited; stopping presenter');break
  magic,version=struct.unpack_from('<II',mapping)
  if records['phase']=='starting' and game and magic==layout['magic'] and version==layout['version'] and struct.unpack_from('<I',mapping,8)[0]>0:
   records['phase']='running';save();log('Ready; logs: '+str(logs))
  if records['phase']=='starting' and time.monotonic()>deadline:log('Startup timeout');exit_code=1;break
  time.sleep(.1)
except Exception as e:
 log('Supervisor failure: '+str(e));exit_code=1
finally:
 records['phase']='stopping';save()
 if game is None:
  found=find_owned('game');game=found[0] if found else None
 if alive(game):
  if mapping:struct.pack_into('<I',mapping,layout['shutdown_request'],1)
  end=time.monotonic()+12
  while alive(game) and time.monotonic()<end:time.sleep(.1)
  if alive(game):
   log('Game did not close after WM_CLOSE; sending TERM to exact game PID');signal_exact(game,signal.SIGTERM)
   end=time.monotonic()+4
   while alive(game) and time.monotonic()<end:time.sleep(.1)
  if alive(game):log('Game unresponsive; sending KILL to exact PID');signal_exact(game,signal.SIGKILL)
 signal_exact(host,signal.SIGTERM)
 for child in children:
  try:child.wait(timeout=4)
  except subprocess.TimeoutExpired:
   child.terminate()
   try:child.wait(timeout=3)
   except subprocess.TimeoutExpired:child.kill();child.wait()
 if mapping:mapping.close()
 for stream in streams:stream.close()
 control.close()
 try:SOCK.unlink()
 except FileNotFoundError:pass
 # Clean up only our temporary KWin observer even when the presenter crashed.
 try:subprocess.run(['qdbus6','org.kde.KWin','/Scripting','org.kde.kwin.Scripting.unloadScript','lilith-linux-windows'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=3)
 except (OSError,subprocess.TimeoutExpired):pass
 records['phase']='stopped';save();log('Stopped; no shared Wine/Steam processes were targeted')
sys.exit(exit_code)
