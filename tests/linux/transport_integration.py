from pathlib import Path
import json,os,sys,shutil,subprocess,uuid,threading,selectors,time
ROOT=Path(__file__).resolve().parents[2]
import argparse
parser=argparse.ArgumentParser(description='Real Linux transport/worker test in two isolated namespaces; no game or Internet route.')
parser.add_argument('--runtime',type=Path,required=True)
parser.add_argument('--work',type=Path,required=True,help='Existing directory strictly below repository work/')
parser.add_argument('--uid',type=int,required=True,help='Ordinary local user owning this test copy')
args=parser.parse_args()
STAGE=args.work.absolute()
assert STAGE.is_relative_to(ROOT/'work') and STAGE!=ROOT/'work' and STAGE.is_dir()
os.environ['PATH']='/usr/sbin:/usr/bin:/sbin:/bin'
sys.path[:0]=[str(ROOT/'tools/linux'),str(ROOT/'tools/installer')]
import install,compatibility
STAGE=compatibility.plain(STAGE)
from internet_room import Room
assert os.geteuid()==0
uid=args.uid
assert uid>0
base=STAGE/('transport-'+uuid.uuid4().hex[:8]);base.mkdir();os.chown(base,uid,uid)
runtime=compatibility.plain(args.runtime)
manifest=dict(linux_native=True,internet_easytier=True,automatic_lan=True,graphics_choice=True,display_selection=True,files={})
for p in (ROOT/'tools').rglob('*.py'):manifest['files']['tools/'+p.relative_to(ROOT/'tools').as_posix()]='fixture'
manifest['files']['tools/installer/easytier-linux-runtime.json']='fixture'
launcher=base/'Launcher';launcher.mkdir()
for source,target in install.launcher_sources(manifest):shutil.copyfile(ROOT/'tools'/source,launcher/target)
(base/'Runtime').mkdir();shutil.copytree(runtime,base/'Runtime/easytier')
ns=['wpt'+uuid.uuid4().hex[:8] for _ in range(2)];children=[];threads=[];stops=[];created=[]
def command(*args,**kw):return subprocess.run(list(args),check=True,capture_output=True,timeout=20,**kw)
def inside(i,*args):return ['ip','netns','exec',ns[i],*map(str,args)]
def start_worker(i,host,room):
    env=dict(os.environ,PKEXEC_UID=str(uid),PYTHONDONTWRITEBYTECODE='1')
    child=subprocess.Popen(inside(i,'/usr/bin/python3','-I','-B',launcher/'linux_elevation.py','--worker'),stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env,bufsize=0)
    children.append(child)
    child.stdin.write(json.dumps(dict(format='weplay-linux-tunnel-v1',root=str(base),room=room.token(),host=host)).encode()+b'\n');child.stdin.flush()
    stopped=threading.Event();stops.append(stopped)
    def heartbeat():
        while not stopped.wait(.8):
            try:child.stdin.write(b'ping\n');child.stdin.flush()
            except OSError:return
    thread=threading.Thread(target=heartbeat,daemon=True);thread.start();threads.append(thread)
    selector=selectors.DefaultSelector();selector.register(child.stdout,selectors.EVENT_READ);pending=b'';deadline=time.monotonic()+70
    try:
        while time.monotonic()<deadline:
            if child.poll() is not None:raise AssertionError(('worker stopped',child.returncode,child.stderr.read().decode()[-1800:]))
            for _,_ in selector.select(.3):
                raw=os.read(child.stdout.fileno(),16384)
                if not raw:continue
                pending+=raw
                while b'\n' in pending:
                    line,pending=pending.split(b'\n',1);value=json.loads(line)
                    assert value.get('phase')=='ready',value
                    return child,stopped,value
        raise TimeoutError('worker startup')
    finally:selector.close()
try:
    for name in ns:command('ip','netns','add',name);created.append(name)
    left='wv'+uuid.uuid4().hex[:6];right='wv'+uuid.uuid4().hex[:6]
    command('ip','link','add',left,'type','veth','peer','name',right)
    for name,dev,address in zip(ns,(left,right),('192.168.252.1/24','192.168.252.2/24')):
        command('ip','link','set',dev,'netns',name)
        command('ip','-n',name,'address','add',address,'dev',dev)
        command('ip','-n',name,'link','set',dev,'up');command('ip','-n',name,'link','set','lo','up')
    room=Room(uuid.uuid4().hex,os.urandom(32).hex(),'10.249.248.0/24',mode='direct',bootstrap=('udp://192.168.252.1:11010',))
    host,hoststop,h=start_worker(0,True,room);print('PASS real privileged host worker ready',flush=True)
    guest,gueststop,g=start_worker(1,False,room);print('PASS real privileged guest worker ready',flush=True)
    server_code='''import socket,selectors,time,sys
sel=selectors.DefaultSelector();sockets=[]
for port in (3074,6200,9103,9111,9112,9999):
 s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind((sys.argv[1],port));sel.register(s,selectors.EVENT_READ,'udp');sockets.append(s)
for port in (9110,2222):
 s=socket.socket();s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);s.bind((sys.argv[1],port));s.listen();sel.register(s,selectors.EVENT_READ,'tcp');sockets.append(s)
print('ready',flush=True)
while True:
 for key,_ in sel.select(.2):
  s=key.fileobj
  if key.data=='udp':data,peer=s.recvfrom(128);s.sendto(b'fixture-ok',peer)
  else:c,_=s.accept();c.sendall(b'fixture-ok');c.close()
'''
    echo=subprocess.Popen(inside(0,'/usr/bin/python3','-I','-B','-c',server_code,h['address']),stdout=subprocess.PIPE,stderr=subprocess.PIPE);children.append(echo)
    assert echo.stdout.readline()==b'ready\n'
    probe_code='''import socket,sys,json
address=sys.argv[1];passed=[]
for port in (3074,6200,9103,9111,9112,9999):
 with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
  s.settimeout(2);s.sendto(b'fixture', (address,port))
  try:got=s.recv(128)==b'fixture-ok'
  except socket.timeout:got=False
  assert got==(port!=9999),(port,got);passed.append(port)
for port in (9110,2222):
 try:
  with socket.create_connection((address,port),timeout=2) as s:got=s.recv(128)==b'fixture-ok'
 except OSError:got=False
 assert got==(port==9110),(port,got);passed.append(port)
print(json.dumps({'ports_checked':len(passed),'allowed':6,'denied':2}))
'''
    proof=command(*inside(1,'/usr/bin/python3','-I','-B','-c',probe_code,h['address']));print(proof.stdout.decode(),flush=True)
    echo.terminate();echo.wait(timeout=10)
    gueststop.set();threads[1].join();guest.stdin.close();assert guest.wait(timeout=20)==0
    print('PASS guest EOF cleanup',flush=True)
    hoststop.set();threads[0].join();assert host.wait(timeout=23)==2
    print('PASS host heartbeat expiry cleanup',flush=True)
    for i,value in enumerate((h,g)):
        devices=json.loads(command('ip','-n',ns[i],'-j','link').stdout)
        assert value['device'] not in {d['ifname'] for d in devices}
    assert not list(base.glob('LocalData/easytier-*'))
    (base/'result.json').write_text(json.dumps(dict(passed=True,isolated_namespaces=2,allowed_services=6,denied_services=2,eof_cleanup=True,heartbeat_cleanup=True,no_tun_left=True),indent=2))
finally:
    for stop in stops:stop.set()
    for thread in threads:thread.join(timeout=2)
    for child in reversed(children):
        if child.poll() is None:child.terminate()
        try:child.wait(timeout=12)
        except subprocess.TimeoutExpired:raise RuntimeError('owned test process did not terminate')
    for name in reversed(created):command('ip','netns','delete',name)
print('PASS isolated native transport and actual privileged-worker lifecycle',flush=True)
