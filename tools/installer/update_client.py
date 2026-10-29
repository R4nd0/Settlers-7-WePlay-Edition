"""Signed, fixed-repository full-installer updates. No credentials or game data."""
import base64
from contextlib import contextmanager
import ctypes
from ctypes import wintypes
import hashlib
import http.client
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from urllib.parse import urlsplit,urljoin,quote
import uuid

import compatibility

REPOSITORY='R4nd0/Settlers-7-WePlay-Edition'
RELEASES='https://api.github.com/repos/'+REPOSITORY+'/releases?per_page=20'
MANIFEST='weplay-update.json'
MAX_SETUP=768*1024*1024
MAX_MANIFEST=49152
KEY=Path(__file__).with_name('update-key.json')
HEX=re.compile('[0-9a-f]{64}')
TAG=re.compile('[A-Za-z0-9][A-Za-z0-9._-]{0,79}')
VERSION=re.compile(r'[0-9]{1,4}\.[0-9]{1,4}\.[0-9]{1,4}(?:-[A-Za-z0-9][A-Za-z0-9.-]{0,47})?')

class UpdateError(ValueError):
    def __init__(self,code):self.code=code;super().__init__(code)

def unique(pairs):
    value={}
    for key,item in pairs:
        if key in value:raise UpdateError('update_invalid')
        value[key]=item
    return value

def decode(raw,limit=MAX_MANIFEST):
    if not isinstance(raw,bytes) or len(raw)>limit:raise UpdateError('update_invalid')
    try:return json.loads(raw.decode('utf-8'),object_pairs_hook=unique)
    except (UnicodeError,ValueError):raise UpdateError('update_invalid') from None

def canonical(value):return json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=True).encode('ascii')

def b64(value):
    if not isinstance(value,str) or not re.fullmatch('[A-Za-z0-9_-]+',value):raise UpdateError('update_signature_invalid')
    try:raw=base64.urlsafe_b64decode(value+'='*(-len(value)%4))
    except ValueError:raise UpdateError('update_signature_invalid') from None
    if base64.urlsafe_b64encode(raw).decode().rstrip('=')!=value:raise UpdateError('update_signature_invalid')
    return raw

def public_key(path=KEY):
    value=decode(compatibility.plain(path).read_bytes(),4096)
    if (not isinstance(value,dict) or set(value)!={'format','key_id','n','e'} or value['format']!='weplay-update-key-v1'
            or not isinstance(value['n'],str) or not re.fullmatch('[0-9a-f]{768}',value['n'])
            or type(value['e']) is not int or value['e']!=65537):raise UpdateError('update_signature_invalid')
    modulus=bytes.fromhex(value['n'])
    if modulus[0]<128 or not modulus[-1]&1 or value['key_id']!=hashlib.sha256(modulus).hexdigest():raise UpdateError('update_signature_invalid')
    return value

def verify(raw,*,key=None):
    envelope=decode(raw);key=key or public_key()
    if (not isinstance(envelope,dict) or set(envelope)!={'format','key_id','payload','signature'}
            or envelope['format']!='weplay-signed-update-v1' or envelope['key_id']!=key['key_id']):raise UpdateError('update_signature_invalid')
    payload=b64(envelope['payload']);signature=b64(envelope['signature'])
    if len(payload)>32768 or len(signature)!=384 or int.from_bytes(signature,'big')>=int(key['n'],16):raise UpdateError('update_signature_invalid')
    actual=pow(int.from_bytes(signature,'big'),key['e'],int(key['n'],16)).to_bytes(384,'big')
    digest=bytes.fromhex('3031300d060960864801650304020105000420')+hashlib.sha256(payload).digest()
    expected=b'\x00\x01'+b'\xff'*(384-len(digest)-3)+b'\x00'+digest
    if actual!=expected:raise UpdateError('update_signature_invalid')
    value=decode(payload,32768)
    if canonical(value)!=payload:raise UpdateError('update_invalid')
    validate(value)
    return value

def validate(value):
    if (not isinstance(value,dict) or set(value)!={'format','repository','channel','sequence','version','tag','notes','setup'}
            or value['format']!='weplay-update-v1' or value['repository']!=REPOSITORY or value['channel']!='preview'
            or type(value['sequence']) is not int or not 1<=value['sequence']<=2147483647
            or not isinstance(value['version'],str) or not VERSION.fullmatch(value['version'])
            or not isinstance(value['tag'],str) or not TAG.fullmatch(value['tag'])
            or not isinstance(value['notes'],str) or len(value['notes'])>4000
            or any(ord(c)<32 and c not in '\n\r\t' for c in value['notes'])):raise UpdateError('update_invalid')
    setup=value['setup']
    expected='https://github.com/'+REPOSITORY+'/releases/download/'+value['tag']+'/Setup.exe'
    if (not isinstance(setup,dict) or set(setup)!={'name','url','size','sha256'} or setup['name']!='Setup.exe'
            or setup['url']!=expected or type(setup['size']) is not int or not 1<=setup['size']<=MAX_SETUP
            or not isinstance(setup['sha256'],str) or not HEX.fullmatch(setup['sha256'])):raise UpdateError('update_invalid')
    return value

def allowed_url(url):
    try:value=urlsplit(url);port=value.port
    except ValueError:raise UpdateError('update_download_failed') from None
    if (len(url)>8192 or any(ord(c)<32 for c in url) or value.scheme!='https' or value.username is not None or value.password is not None
            or value.fragment or port not in (None,443)
            or value.hostname not in ('api.github.com','github.com','objects.githubusercontent.com','release-assets.githubusercontent.com')):
        raise UpdateError('update_download_failed')
    return value

def transfer(url,limit,*,destination=None,expected_size=None,progress=None,connection_factory=http.client.HTTPSConnection):
    deadline=time.monotonic()+(1800 if destination else 90)
    for redirect in range(6):
        target=allowed_url(url);connection=connection_factory(target.hostname,timeout=10)
        try:
            connection.request('GET',(target.path or '/')+('?' +target.query if target.query else ''),headers={
                'User-Agent':'Settlers7WePlay-Updater/1.0','Accept':'application/vnd.github+json' if target.hostname=='api.github.com' else 'application/octet-stream'})
            response=connection.getresponse()
            if response.status in (301,302,303,307,308):
                location=response.getheader('Location')
                if not location or redirect==5:raise UpdateError('update_download_failed')
                url=urljoin(url,location);allowed_url(url);continue
            if response.status!=200:raise UpdateError('update_download_failed')
            size=response.getheader('Content-Length')
            if size and (not size.isdigit() or int(size)>limit or (expected_size is not None and int(size)!=expected_size)):
                raise UpdateError('update_download_failed')
            chunks=[];received=0;hasher=hashlib.sha256()
            output=compatibility.plain(destination).open('xb') if destination else None
            try:
                while True:
                    if time.monotonic()>=deadline:raise UpdateError('update_download_failed')
                    if connection.sock:connection.sock.settimeout(min(10,max(.1,deadline-time.monotonic())))
                    chunk=response.read(min(65536,limit-received+1))
                    if not chunk:break
                    received+=len(chunk)
                    if received>limit:raise UpdateError('update_download_failed')
                    hasher.update(chunk)
                    if output:output.write(chunk)
                    else:chunks.append(chunk)
                    if progress:progress('update_download',received,expected_size or limit)
                if expected_size is not None and received!=expected_size:raise UpdateError('update_download_failed')
                if output:output.flush();os.fsync(output.fileno())
                return (received,hasher.hexdigest()) if output else b''.join(chunks)
            finally:
                if output:output.close()
        except (OSError,TimeoutError,http.client.HTTPException):raise UpdateError('update_download_failed') from None
        finally:connection.close()
    raise UpdateError('update_download_failed')

def atomic(path,value):
    path=compatibility.plain(path);temporary=path.with_name(path.name+'.'+uuid.uuid4().hex+'.tmp')
    try:
        with temporary.open('xb') as stream:stream.write(canonical(value)+b'\n');stream.flush();os.fsync(stream.fileno())
        os.replace(temporary,path)
    finally:temporary.unlink(missing_ok=True)

def paths(app):
    app=compatibility.plain(app);state=compatibility.plain(app/'State');state.mkdir(exist_ok=True)
    updates=compatibility.plain(state/'Updates');updates.mkdir(exist_ok=True)
    return app,updates

def current(app):
    try:value=decode(compatibility.plain(app/'desktop-release.json').read_bytes(),65536)
    except (OSError,ValueError):raise UpdateError('update_unavailable') from None
    if (not isinstance(value,dict) or not isinstance(value.get('version'),str) or not VERSION.fullmatch(value['version'])
            or type(value.get('update_sequence')) is not int or value['update_sequence']<1 or value.get('update_channel')!='preview'):
        raise UpdateError('update_unavailable')
    return value

def highwater(updates):
    path=compatibility.plain(updates/'highest.json')
    if not path.exists():return {'sequence':0,'payload_sha256':None}
    value=decode(path.read_bytes(),4096)
    if (not isinstance(value,dict) or set(value)!={'sequence','payload_sha256'} or type(value['sequence']) is not int
            or value['sequence']<1 or not isinstance(value['payload_sha256'],str) or not HEX.fullmatch(value['payload_sha256'])):raise UpdateError('update_invalid')
    return value

@contextmanager
def cache_lock(updates):
    path=compatibility.plain(updates/'cache.lock')
    with path.open('a+b') as stream:
        if stream.tell()==0:stream.write(b'0');stream.flush()
        stream.seek(0)
        if os.name=='nt':
            import msvcrt
            try:msvcrt.locking(stream.fileno(),msvcrt.LK_NBLCK,1)
            except OSError:raise UpdateError('update_busy') from None
            try:yield
            finally:stream.seek(0);msvcrt.locking(stream.fileno(),msvcrt.LK_UNLCK,1)
        else:
            import fcntl
            try:fcntl.flock(stream,fcntl.LOCK_EX|fcntl.LOCK_NB)
            except OSError:raise UpdateError('update_busy') from None
            try:yield
            finally:fcntl.flock(stream,fcntl.LOCK_UN)

def check(app,progress=None,*,fetch=transfer):
    app,updates=paths(app);local=current(app)
    base={'available':False,'current_version':local['version'],'version':None,'sequence':None,'release_url':None,'notes':'','reason':'no_signed_release'}
    if progress:progress('update_check',0,0)
    releases=decode(fetch(RELEASES,1024*1024),1024*1024)
    if not isinstance(releases,list) or len(releases)>20:raise UpdateError('update_invalid')
    candidates=[]
    for release in releases:
        if not isinstance(release,dict) or release.get('draft') is not False:continue
        tag=release.get('tag_name');assets=release.get('assets')
        if not isinstance(tag,str) or not TAG.fullmatch(tag) or not isinstance(assets,list) or len(assets)>50:continue
        selected=[a for a in assets if isinstance(a,dict) and a.get('name')==MANIFEST]
        if len(selected)!=1:continue
        expected='https://github.com/'+REPOSITORY+'/releases/download/'+tag+'/'+MANIFEST
        if selected[0].get('browser_download_url')!=expected:raise UpdateError('update_invalid')
        candidates.append((tag,expected))
    # A repository can retain months of signed releases; inspect the newest
    # bounded subset rather than disabling updates once eight exist.
    candidates=candidates[:8]
    newest=None;raw_newest=None
    for tag,url in candidates:
        raw=fetch(url,MAX_MANIFEST);value=verify(raw)
        if value['tag']!=tag:raise UpdateError('update_invalid')
        if newest is None or value['sequence']>newest['sequence']:newest,raw_newest=value,raw
        elif value['sequence']==newest['sequence'] and canonical(value)!=canonical(newest):raise UpdateError('update_invalid')
    if newest is None:return base
    with cache_lock(updates):
        seen=highwater(updates);fingerprint=hashlib.sha256(canonical(newest)).hexdigest()
        if newest['sequence']<seen['sequence'] or (newest['sequence']==seen['sequence'] and fingerprint!=seen['payload_sha256']):raise UpdateError('update_downgrade')
        atomic(updates/'highest.json',{'sequence':newest['sequence'],'payload_sha256':fingerprint})
        if newest['sequence']<=local['update_sequence']:return {**base,'reason':'current'}
        # Commit a signed envelope only after signature, scope and replay checks.
        atomic(updates/'verified-envelope.json',decode(raw_newest))
    return {**base,'available':True,'version':newest['version'],'sequence':newest['sequence'],
            'release_url':'https://github.com/'+REPOSITORY+'/releases/tag/'+newest['tag'],'notes':newest['notes'],'reason':'available'}

def process_info(pid):
    if os.name!='nt' or type(pid) is not int or not 1<=pid<=0xffffffff:raise UpdateError('update_parent_invalid')
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD];kernel.OpenProcess.restype=wintypes.HANDLE
    kernel.QueryFullProcessImageNameW.argtypes=[wintypes.HANDLE,wintypes.DWORD,wintypes.LPWSTR,ctypes.POINTER(wintypes.DWORD)];kernel.QueryFullProcessImageNameW.restype=wintypes.BOOL
    kernel.GetProcessTimes.argtypes=[wintypes.HANDLE,*([ctypes.POINTER(wintypes.FILETIME)]*4)];kernel.GetProcessTimes.restype=wintypes.BOOL
    kernel.CloseHandle.argtypes=[wintypes.HANDLE];kernel.CloseHandle.restype=wintypes.BOOL
    handle=kernel.OpenProcess(0x1000,False,pid)
    if not handle:raise UpdateError('update_parent_invalid')
    try:
        buffer=ctypes.create_unicode_buffer(32768);length=wintypes.DWORD(len(buffer))
        times=[wintypes.FILETIME() for _ in range(4)]
        if not kernel.QueryFullProcessImageNameW(handle,0,buffer,ctypes.byref(length)) or not kernel.GetProcessTimes(handle,*[ctypes.byref(x) for x in times]):raise UpdateError('update_parent_invalid')
        creation=(times[0].dwHighDateTime<<32)|times[0].dwLowDateTime
        return {'pid':pid,'path':str(compatibility.plain(buffer.value)),'started_ticks':creation+504911232000000000}
    finally:kernel.CloseHandle(handle)

def prepare(app,parent_pid,parent_started_ticks,progress=None,*,expected_sequence=None,fetch=transfer,process_reader=process_info):
    app,updates=paths(app);local=current(app)
    if parent_pid!=os.getppid() or type(parent_started_ticks) is not int:raise UpdateError('update_parent_invalid')
    parent=process_reader(parent_pid)
    if parent['path'].casefold()!=str(app/'S7Launcher.exe').casefold() or parent['started_ticks']!=parent_started_ticks:raise UpdateError('update_parent_invalid')
    backend=process_reader(os.getpid())
    if backend['path'].casefold()!=str(app/'Package/Runtime/python/python.exe').casefold():raise UpdateError('update_parent_invalid')
    with cache_lock(updates):
        envelope=compatibility.plain(updates/'verified-envelope.json').read_bytes();value=verify(envelope)
        if expected_sequence is not None and value['sequence']!=expected_sequence:raise UpdateError('update_changed')
        seen=highwater(updates)
        if value['sequence']<=local['update_sequence'] or value['sequence']!=seen['sequence'] or hashlib.sha256(canonical(value)).hexdigest()!=seen['payload_sha256']:raise UpdateError('update_downgrade')
    if shutil.disk_usage(updates).free<value['setup']['size']+256*1024*1024:raise UpdateError('update_disk_full')
    job=compatibility.plain(updates/uuid.uuid4().hex);job.mkdir()
    target=job/'Setup.exe';partial=job/'Setup.partial'
    try:
        size,digest=fetch(value['setup']['url'],value['setup']['size'],destination=partial,expected_size=value['setup']['size'],progress=progress)
        if size!=value['setup']['size'] or digest!=value['setup']['sha256'] or compatibility.digest(partial)!=digest:raise UpdateError('update_hash_invalid')
        os.replace(partial,target)
        for name in ('update_handoff.ps1','update-key.json'):shutil.copyfile(compatibility.plain(Path(__file__).with_name(name)),job/name)
        atomic(job/'weplay-update.json',decode(envelope))
        record={'format':'weplay-update-handoff-v1','app':str(app),'parent':parent,'backend':backend,'version':value['version'],'sequence':value['sequence']}
        atomic(job/'handoff.json',record)
        return {'version':value['version'],'sequence':value['sequence'],'job':str(job)}
    except BaseException:
        # Only this newly-created partial download is ever reclaimed here.
        compatibility.plain(partial).unlink(missing_ok=True)
        raise

def start_handoff(app,job):
    app,updates=paths(app);job=compatibility.plain(job)
    if job.parent!=updates or not re.fullmatch('[0-9a-f]{32}',job.name):raise UpdateError('update_invalid')
    record=decode(compatibility.plain(job/'handoff.json').read_bytes(),4096)
    if record.get('app')!=str(app) or record.get('format')!='weplay-update-handoff-v1':raise UpdateError('update_invalid')
    if record.get('backend',{}).get('pid')!=os.getpid() or record.get('parent',{}).get('pid')!=os.getppid():raise UpdateError('update_parent_invalid')
    if compatibility.plain(job/'ready.json').exists():raise UpdateError('update_invalid')
    for name in ('update_handoff.ps1','update-key.json'):
        if compatibility.digest(compatibility.plain(job/name))!=compatibility.digest(compatibility.plain(Path(__file__).with_name(name))):raise UpdateError('update_invalid')
    # No browser URL or update-provided argument is executable input.
    command=[str(Path(os.environ.get('SystemRoot',r'C:\Windows'))/'System32/WindowsPowerShell/v1.0/powershell.exe'),
             '-NoProfile','-NonInteractive','-File',str(job/'update_handoff.ps1'),'-Job',str(job/'handoff.json')]
    try:child=subprocess.Popen(command,cwd=job,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0),close_fds=True)
    except OSError:raise UpdateError('update_handoff_failed') from None
    deadline=time.monotonic()+90
    while time.monotonic()<deadline:
        if child.poll() is not None:raise UpdateError('update_handoff_failed')
        ready=compatibility.plain(job/'ready.json')
        if ready.exists():
            value=decode(ready.read_bytes(),4096)
            if value!={'format':'weplay-update-ready-v1','helper_pid':child.pid,'version':record['version'],'sequence':record['sequence']}:
                raise UpdateError('update_handoff_failed')
            return {'helper_pid':child.pid,'version':record['version']}
        time.sleep(.1)
    child.terminate()
    raise UpdateError('update_handoff_failed')
