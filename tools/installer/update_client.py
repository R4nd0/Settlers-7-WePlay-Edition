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
import threading
import time
from urllib.parse import urlsplit,urljoin,quote
import uuid

import compatibility

REPOSITORY='R4nd0/Settlers-7-WePlay-Edition'
RELEASES='https://api.github.com/repos/'+REPOSITORY+'/releases?per_page=20'
MANIFEST='weplay-update.json'
ISOLATED_MANIFEST='weplay-update-isolated.json'
MAX_SETUP=768*1024*1024
MAX_MANIFEST=49152
JOB_FORMAT='weplay-update-job-v1'
PREPARING_FORMAT='weplay-update-preparing-v1'
DELETING_FORMAT='weplay-update-deleting-v1'
DOWNLOAD_BLOCK=1024*1024
MAX_JOB_RECEIPT=2*1024*1024
CHECK_BUDGET=120
MAX_JOB_AGE=7*24*60*60
MAX_CACHED_JOBS=2
MAX_CACHED_BYTES=256*1024*1024
JOB_FILES={'Setup.exe','Setup.partial','WePlay.UpdateHelper.exe','update_handoff.ps1',
           'update-key.json','weplay-update.json','handoff.json','handoff-active.json'}
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
    keys={'format','repository','channel','sequence','version','tag','notes','setup'}
    if (not isinstance(value,dict) or set(value) not in (keys,keys|{'platform_isolation'})
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
    if 'platform_isolation' in value:
        import release_policy
        if not release_policy.valid(value['platform_isolation']):raise UpdateError('update_isolation_required')
    return value


def preserve_isolation(app,value):
    import release_policy
    try:release_policy.check_update(app,value)
    except (OSError,ValueError):raise UpdateError('update_isolation_required') from None

def allowed_url(url):
    try:value=urlsplit(url);port=value.port
    except ValueError:raise UpdateError('update_download_failed') from None
    if (len(url)>8192 or any(ord(c)<32 for c in url) or value.scheme!='https' or value.username is not None or value.password is not None
            or value.fragment or port not in (None,443)
            or value.hostname not in ('api.github.com','github.com','objects.githubusercontent.com','release-assets.githubusercontent.com')):
        raise UpdateError('update_download_failed')
    return value

def transfer(url,limit,*,destination=None,expected_size=None,progress=None,connection_factory=http.client.HTTPSConnection,
             deadline=None,cancelled=None,clock=time.monotonic,before_write=None):
    deadline=min(deadline if deadline is not None else float('inf'),clock()+(1800 if destination else 90))
    def checkpoint():
        if cancelled and cancelled():raise UpdateError('cancelled')
        remaining=deadline-clock()
        if remaining<=0:raise UpdateError('update_download_failed')
        return min(10,remaining)
    for redirect in range(6):
        target=allowed_url(url);connection=connection_factory(target.hostname,timeout=checkpoint())
        response=None
        def transport_socket():
            return connection.sock or getattr(getattr(getattr(response,'fp',None),'raw',None),'_sock',None)
        def expire_connection():
            # Socket timeouts alone reset for every received header byte. Stop
            # this exact connection at the total deadline even during headers.
            transport=transport_socket()
            if transport:
                try:transport.shutdown(2)
                except OSError:pass
        transport_done=threading.Event();transport_cancelled=threading.Event()
        def watch_transport():
            while not transport_done.wait(min(.1,max(.001,deadline-clock()))):
                if cancelled and cancelled():
                    transport_cancelled.set();expire_connection();return
                if clock()>=deadline:expire_connection();return
        transport_watchdog=threading.Thread(target=watch_transport,name='weplay-update-transfer',daemon=True)
        transport_watchdog.start()
        try:
            connection.request('GET',(target.path or '/')+('?' +target.query if target.query else ''),headers={
                'User-Agent':'Settlers7WePlay-Updater/1.0','Accept':'application/vnd.github+json' if target.hostname=='api.github.com' else 'application/octet-stream'})
            if connection.sock:connection.sock.settimeout(checkpoint())
            response=connection.getresponse();checkpoint()
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
            pending=bytearray();written=0;written_hash=hashlib.sha256()
            def flush_pending():
                nonlocal written
                if not pending:return
                block=bytes(pending)
                # Persist write intent first. The last block may be only partly
                # present after process loss; all earlier bytes are durable.
                before_write(written,written_hash.hexdigest(),block)
                checkpoint();output.write(block);output.flush();os.fsync(output.fileno())
                written_hash.update(block);written+=len(block);pending.clear()
            try:
                while True:
                    timeout=checkpoint()
                    # For Connection: close responses http.client detaches its
                    # socket, but the response's buffered stream still owns it.
                    transport=transport_socket()
                    if transport:transport.settimeout(timeout)
                    # read1 returns after one buffered/socket read; read(n) can
                    # wait indefinitely for n bytes from a slow continuous peer.
                    chunk=response.read1(min(65536,limit-received+1,DOWNLOAD_BLOCK-len(pending) if output and before_write else 65536));checkpoint()
                    if not chunk:break
                    received+=len(chunk)
                    if received>limit:raise UpdateError('update_download_failed')
                    hasher.update(chunk)
                    if output:
                        if before_write:
                            pending.extend(chunk)
                            if len(pending)>=DOWNLOAD_BLOCK:flush_pending()
                        else:output.write(chunk)
                    else:chunks.append(chunk)
                    if progress:progress('update_download',received,expected_size or limit)
                if expected_size is not None and received!=expected_size:raise UpdateError('update_download_failed')
                if output and before_write:flush_pending()
                if output:output.flush();os.fsync(output.fileno())
                return (received,hasher.hexdigest()) if output else b''.join(chunks)
            finally:
                if output:output.close()
        except (OSError,TimeoutError,http.client.HTTPException):
            if transport_cancelled.is_set():raise UpdateError('cancelled') from None
            checkpoint()
            raise UpdateError('update_download_failed') from None
        finally:
            transport_done.set()
            # Join before rebinding connection/response for a redirect; an
            # already-running expiry callback must never touch the next hop.
            transport_watchdog.join()
            connection.close()
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

def process_running(pid):
    """Conservative liveness check for retention only; never terminates a PID."""
    if type(pid) is not int or not 1<=pid<=0xffffffff:return True
    if os.name!='nt':
        try:os.kill(pid,0);return True
        except ProcessLookupError:return False
        except OSError:return True
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD];kernel.OpenProcess.restype=wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes=[wintypes.HANDLE,wintypes.DWORD];kernel.WaitForSingleObject.restype=wintypes.DWORD
    kernel.CloseHandle.argtypes=[wintypes.HANDLE];kernel.CloseHandle.restype=wintypes.BOOL
    handle=kernel.OpenProcess(0x100000,False,pid)
    if not handle:return ctypes.get_last_error() not in (87,1168)
    try:return kernel.WaitForSingleObject(handle,0)!=0
    finally:kernel.CloseHandle(handle)


def job_inventory(job,*,include_ready=False):
    """Only flat, known regular files are eligible; unknown contents are preserved."""
    job=compatibility.plain(job)
    entries=list(job.iterdir())
    if any(p.name not in JOB_FILES|{'job-owner.json','ready.json'} for p in entries):return None
    result={}
    for path in entries:
        path=compatibility.plain(path)
        if not path.is_file():return None
        if path.name!='job-owner.json' and (include_ready or path.name!='ready.json'):
            result[path.name]=compatibility.digest(path)
    return result


def owned_job(app,job,*,require_inventory=True):
    """A local ownership receipt is not execution authority or a signed manifest."""
    app,updates=paths(app);job=compatibility.plain(job)
    if job.parent!=updates or not re.fullmatch('[0-9a-f]{32}',job.name):return None
    marker=compatibility.plain(job/'job-owner.json')
    if not marker.is_file():return None
    if marker.stat().st_size>MAX_JOB_RECEIPT:return None
    with marker.open('rb') as stream:value=decode(stream.read(MAX_JOB_RECEIPT+1),MAX_JOB_RECEIPT)
    preparing=isinstance(value,dict) and value.get('format')==PREPARING_FORMAT
    if (not isinstance(value,dict) or set(value)!=({'format','app','job','created','creator_pid','files'}|({'download'} if preparing else set()))
            or value['format'] not in (JOB_FORMAT,PREPARING_FORMAT,DELETING_FORMAT) or value['app']!=hashlib.sha256(str(app).casefold().encode()).hexdigest()
            or value['job']!=job.name or type(value['created']) is not int or value['created']<1
            or type(value['creator_pid']) is not int or not 1<=value['creator_pid']<=0xffffffff
            or not isinstance(value['files'],dict) or not set(value['files'])<=(JOB_FILES|({'ready.json'} if value['format']==DELETING_FORMAT else set()))
            or any(not isinstance(x,str) or not HEX.fullmatch(x) for x in value['files'].values())):return None
    if preparing:
        download=value['download']
        if (not isinstance(download,dict) or set(download)!={'size','sha256','next'}
                or type(download['size']) is not int or not 0<=download['size']<=MAX_SETUP
                or not isinstance(download['sha256'],str) or not HEX.fullmatch(download['sha256'])
                or not isinstance(download['next'],str) or len(download['next'])>4*((DOWNLOAD_BLOCK+2)//3)):
            return None
        try:block=base64.b64decode(download['next'],validate=True)
        except ValueError:return None
        if (len(block)>DOWNLOAD_BLOCK or download['size']+len(block)>MAX_SETUP
                or base64.b64encode(block).decode('ascii')!=download['next']):return None
    if require_inventory:
        if preparing:
            partial=compatibility.plain(job/'Setup.partial')
            if partial.exists() and (not partial.is_file() or not download['size']<=partial.stat().st_size<=download['size']+len(block)):
                return None
        inventory=job_inventory(job,include_ready=value['format']==DELETING_FORMAT)
        if inventory is None:return None
        if preparing:
            if not preparing_inventory(job,value,inventory):return None
        elif value['format']==DELETING_FORMAT:
            if any(value['files'].get(name)!=digest for name,digest in inventory.items()):return None
        elif inventory!=value['files']:return None
    return value


def preparing_inventory(job,value,inventory):
    """Authenticate an interrupted download; never adopt post-crash bytes."""
    if any(name!='Setup.partial' and value['files'].get(name)!=digest for name,digest in inventory.items()):return False
    if 'Setup.partial' not in inventory:return True
    path=compatibility.plain(job/'Setup.partial');download=value['download']
    block=base64.b64decode(download['next'],validate=True);size=path.stat().st_size
    if not download['size']<=size<=download['size']+len(block):return False
    hasher=hashlib.sha256();remaining=download['size']
    with path.open('rb') as stream:
        while remaining:
            chunk=stream.read(min(DOWNLOAD_BLOCK,remaining))
            if not chunk:return False
            hasher.update(chunk);remaining-=len(chunk)
        tail=stream.read(DOWNLOAD_BLOCK+1)
    return hasher.hexdigest()==download['sha256'] and tail==block[:size-download['size']]


def remove_owned_job(app,job):
    value=owned_job(app,job)
    if value is None:return False
    inventory=job_inventory(job,include_ready=True)
    if inventory is None:return False
    if value['format']==PREPARING_FORMAT and not preparing_inventory(job,value,inventory):return False
    if value['format']==JOB_FORMAT and {name:digest for name,digest in inventory.items() if name!='ready.json'}!=value['files']:
        return False
    # Persist the complete validated deletion generation before the first
    # unlink. Retries may accept absent files only under this receipt, and
    # never adopt added/modified contents into the original generation.
    if value['format']!=DELETING_FORMAT:
        value={key:item for key,item in value.items() if key!='download'}
        value.update(format=DELETING_FORMAT,files=inventory)
        atomic(job/'job-owner.json',value)
    if any(value['files'].get(name)!=digest for name,digest in inventory.items()):return False
    for name,digest in inventory.items():
        path=compatibility.plain(job/name)
        if compatibility.digest(path)!=digest:return False
        path.unlink()
    compatibility.plain(job/'job-owner.json').unlink()
    compatibility.plain(job).rmdir()
    return True


def reclaim_jobs(app,*,now=None,alive=process_running,max_age=MAX_JOB_AGE,
                 max_jobs=MAX_CACHED_JOBS,max_bytes=MAX_CACHED_BYTES):
    app,updates=paths(app);now=int(time.time()) if now is None else now
    rows=[]
    with cache_lock(updates):
        for path in updates.iterdir():
            if not re.fullmatch('[0-9a-f]{32}',path.name):continue
            try:
                job=compatibility.plain(path)
                if not job.is_dir():continue
                value=owned_job(app,job,require_inventory=False)
                if value is None or value['created']>now+300 or alive(value['creator_pid']):continue
                active=compatibility.plain(job/'handoff-active.json')
                if active.exists():
                    record=decode(active.read_bytes(),4096)
                    if (not isinstance(record,dict) or set(record)!={'format','pid'}
                            or record['format']!='weplay-update-active-v1' or type(record['pid']) is not int
                            or alive(record['pid'])):continue
                ready=compatibility.plain(job/'ready.json')
                if ready.exists():
                    record=decode(ready.read_bytes(),4096)
                    if (not isinstance(record,dict) or record.get('format')!='weplay-update-ready-v1'
                            or type(record.get('helper_pid')) is not int or alive(record['helper_pid'])):continue
                entries=list(job.iterdir())
                names={p.name for p in entries}
                expected={'job-owner.json',*value['files'],*(['ready.json'] if ready.exists() else [])}
                if value['format']==PREPARING_FORMAT:expected.add('Setup.partial')
                if value['format'] in (DELETING_FORMAT,PREPARING_FORMAT):
                    if not names<=expected:continue
                elif names!=expected:continue
                if any(not compatibility.plain(p).is_file() for p in entries):continue
                size=sum(p.stat().st_size for p in entries if value['format']==PREPARING_FORMAT or p.name in value['files'])
                rows.append((value['created'],job,size,value['format']==DELETING_FORMAT))
            except (OSError,ValueError):continue
        rows.sort(key=lambda row:row[0],reverse=True)
        kept=0;total=0;removed=0
        for created,job,size,deleting in rows:
            if deleting or now-created>max_age or kept>=max_jobs or total+size>max_bytes:
                try:removed+=remove_owned_job(app,job)
                except (OSError,ValueError):pass
            else:kept+=1;total+=size
    return removed


def check(app,progress=None,*,fetch=transfer,cancelled=None,budget_seconds=CHECK_BUDGET,clock=time.monotonic):
    deadline=clock()+budget_seconds
    def cancellation():
        if cancelled and cancelled():raise UpdateError('cancelled')
    def fetch_checked(url,limit):
        cancellation()
        if clock()>=deadline:raise UpdateError('update_download_failed')
        return fetch(url,limit,deadline=deadline,cancelled=cancelled,clock=clock,
                     progress=(lambda *_:progress('update_check',0,0)) if progress else None)
    app,updates=paths(app);local=current(app)
    # This request may be hard-stopped by the desktop. Destructive cache
    # maintenance belongs to prepare(), never this UI/network read path.
    base={'available':False,'current_version':local['version'],'version':None,'sequence':None,'release_url':None,'notes':'','reason':'no_signed_release'}
    if progress:progress('update_check',0,0)
    releases=decode(fetch_checked(RELEASES,1024*1024),1024*1024)
    if not isinstance(releases,list) or len(releases)>20:raise UpdateError('update_invalid')
    candidates=[]
    for release in releases:
        if not isinstance(release,dict) or release.get('draft') is not False:continue
        tag=release.get('tag_name');assets=release.get('assets')
        if not isinstance(tag,str) or not TAG.fullmatch(tag) or not isinstance(assets,list) or len(assets)>50:continue
        name=ISOLATED_MANIFEST if any(isinstance(a,dict) and a.get('name')==ISOLATED_MANIFEST for a in assets) else MANIFEST
        selected=[a for a in assets if isinstance(a,dict) and a.get('name')==name]
        if len(selected)!=1:continue
        expected='https://github.com/'+REPOSITORY+'/releases/download/'+tag+'/'+name
        if selected[0].get('browser_download_url')!=expected:raise UpdateError('update_invalid')
        candidates.append((tag,expected))
    # A repository can retain months of signed releases; inspect the newest
    # bounded subset rather than disabling updates once eight exist.
    candidates=candidates[:8]
    newest=None;raw_newest=None;transport_failures=0;sequences={}
    for index,(tag,url) in enumerate(candidates):
        cancellation()
        if clock()>=deadline:
            transport_failures+=len(candidates)-index;break
        if progress:progress('update_check',index,len(candidates))
        try:raw=fetch_checked(url,MAX_MANIFEST)
        except UpdateError as error:
            if error.code!='update_download_failed':raise
            transport_failures+=1;continue
        value=verify(raw)
        if value['tag']!=tag:raise UpdateError('update_invalid')
        encoded=canonical(value)
        if sequences.setdefault(value['sequence'],encoded)!=encoded:raise UpdateError('update_invalid')
        if newest is None or value['sequence']>newest['sequence']:newest,raw_newest=value,raw
        elif value['sequence']==newest['sequence'] and canonical(value)!=canonical(newest):raise UpdateError('update_invalid')
    cancellation()
    if newest is None:
        if transport_failures:raise UpdateError('update_download_failed')
        return base
    if newest['sequence']>local['update_sequence']:preserve_isolation(app,newest)
    with cache_lock(updates):
        seen=highwater(updates);fingerprint=hashlib.sha256(canonical(newest)).hexdigest()
        if newest['sequence']<seen['sequence'] or (newest['sequence']==seen['sequence'] and fingerprint!=seen['payload_sha256']):raise UpdateError('update_downgrade')
        atomic(updates/'highest.json',{'sequence':newest['sequence'],'payload_sha256':fingerprint})
        if newest['sequence']<=local['update_sequence']:return {**base,'reason':'current',**({'incomplete':True} if transport_failures else {})}
        # Commit a signed envelope only after signature, scope and replay checks.
        atomic(updates/'verified-envelope.json',decode(raw_newest))
    return {**base,'available':True,'version':newest['version'],'sequence':newest['sequence'],
            'release_url':'https://github.com/'+REPOSITORY+'/releases/tag/'+newest['tag'],'notes':newest['notes'],'reason':'available',
            **({'incomplete':True} if transport_failures else {})}

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


def handoff_source(app):
    package=compatibility.plain(app/'Package/package.json')
    if package.is_file():
        manifest=decode(package.read_bytes(),4*1024*1024)
        if manifest.get('windows_helpers',False) is True:
            source=compatibility.plain(app/'Package/Payload/Helpers/WePlay.UpdateHelper.exe')
            if compatibility.digest(source)!=manifest.get('files',{}).get('payload/helpers/weplay.updatehelper.exe'):
                raise UpdateError('update_invalid')
            return source
    # Compatibility for RC3.4 packages; RC3.5 neither ships nor executes this script.
    return compatibility.plain(Path(__file__).with_name('update_handoff.ps1'))

def prepare(app,parent_pid,parent_started_ticks,progress=None,*,expected_sequence=None,fetch=transfer,process_reader=process_info,cancelled=None):
    def checkpoint():
        if cancelled and cancelled():raise UpdateError('cancelled')
    checkpoint()
    app,updates=paths(app);local=current(app)
    reclaim_jobs(app)
    if parent_pid!=os.getppid() or type(parent_started_ticks) is not int:raise UpdateError('update_parent_invalid')
    parent=process_reader(parent_pid)
    if parent['path'].casefold()!=str(app/'S7Launcher.exe').casefold() or parent['started_ticks']!=parent_started_ticks:raise UpdateError('update_parent_invalid')
    backend=process_reader(os.getpid())
    if backend['path'].casefold()!=str(app/'Package/Runtime/python/python.exe').casefold():raise UpdateError('update_parent_invalid')
    with cache_lock(updates):
        envelope=compatibility.plain(updates/'verified-envelope.json').read_bytes();value=verify(envelope)
        preserve_isolation(app,value)
        if expected_sequence is not None and value['sequence']!=expected_sequence:raise UpdateError('update_changed')
        seen=highwater(updates)
        if value['sequence']<=local['update_sequence'] or value['sequence']!=seen['sequence'] or hashlib.sha256(canonical(value)).hexdigest()!=seen['payload_sha256']:raise UpdateError('update_downgrade')
    if shutil.disk_usage(updates).free<value['setup']['size']+256*1024*1024:raise UpdateError('update_disk_full')
    checkpoint()
    job=compatibility.plain(updates/uuid.uuid4().hex);job.mkdir()
    target=job/'Setup.exe';partial=job/'Setup.partial'
    try:
        marker={'format':PREPARING_FORMAT,'app':hashlib.sha256(str(app).casefold().encode()).hexdigest(),
                'job':job.name,'created':int(time.time()),'creator_pid':os.getpid(),'files':{'Setup.exe':value['setup']['sha256']},
                'download':dict(size=0,sha256=hashlib.sha256(b'').hexdigest(),next='')}
        atomic(job/'job-owner.json',marker)
        def before_write(size,digest,block):
            checkpoint()
            if len(block)>DOWNLOAD_BLOCK:raise UpdateError('update_invalid')
            marker['download']=dict(size=size,sha256=digest,next=base64.b64encode(block).decode('ascii'))
            atomic(job/'job-owner.json',marker)
        size,digest=fetch(value['setup']['url'],value['setup']['size'],destination=partial,expected_size=value['setup']['size'],
                          progress=progress,cancelled=cancelled,before_write=before_write)
        checkpoint()
        if size!=value['setup']['size'] or digest!=value['setup']['sha256'] or compatibility.digest(partial)!=digest:raise UpdateError('update_hash_invalid')
        checkpoint()
        os.replace(partial,target)
        def declare(name,digest):
            checkpoint();marker['files'][name]=digest;atomic(job/'job-owner.json',marker)
        def copy_owned(source):
            declare(source.name,compatibility.digest(source));shutil.copyfile(source,job/source.name);checkpoint()
        def write_owned(name,data):
            declare(name,hashlib.sha256(canonical(data)+b'\n').hexdigest());atomic(job/name,data);checkpoint()
        helper=handoff_source(app);copy_owned(helper)
        copy_owned(compatibility.plain(Path(__file__).with_name('update-key.json')))
        write_owned('weplay-update.json',decode(envelope))
        record={'format':'weplay-update-handoff-v1','app':str(app),'parent':parent,'backend':backend,'version':value['version'],'sequence':value['sequence']}
        write_owned('handoff.json',record)
        checkpoint()
        if job_inventory(job)!=marker['files']:raise UpdateError('update_invalid')
        marker={key:item for key,item in marker.items() if key!='download'};marker['format']=JOB_FORMAT
        atomic(job/'job-owner.json',marker)
        checkpoint()
        return {'version':value['version'],'sequence':value['sequence'],'job':str(job)}
    except BaseException:
        # Preserve unexpected/linked contents and the original preparation error.
        try:
            if not remove_owned_job(app,job):compatibility.plain(job).rmdir()
        except (OSError,ValueError):pass
        raise

def start_handoff(app,job):
    app,updates=paths(app);job=compatibility.plain(job)
    if job.parent!=updates or not re.fullmatch('[0-9a-f]{32}',job.name):raise UpdateError('update_invalid')
    record=decode(compatibility.plain(job/'handoff.json').read_bytes(),4096)
    if record.get('app')!=str(app) or record.get('format')!='weplay-update-handoff-v1':raise UpdateError('update_invalid')
    if record.get('backend',{}).get('pid')!=os.getpid() or record.get('parent',{}).get('pid')!=os.getppid():raise UpdateError('update_parent_invalid')
    if compatibility.plain(job/'ready.json').exists():raise UpdateError('update_invalid')
    helper=handoff_source(app)
    if compatibility.digest(compatibility.plain(job/helper.name))!=compatibility.digest(helper):raise UpdateError('update_invalid')
    if compatibility.digest(compatibility.plain(job/'update-key.json'))!=compatibility.digest(compatibility.plain(Path(__file__).with_name('update-key.json'))):raise UpdateError('update_invalid')
    # No browser URL or update-provided argument is executable input.
    command=([str(job/helper.name),'--job',str(job/'handoff.json')] if helper.suffix.lower()=='.exe' else
        [str(Path(os.environ.get('SystemRoot',r'C:\Windows'))/'System32/WindowsPowerShell/v1.0/powershell.exe'),
         '-NoProfile','-NonInteractive','-File',str(job/'update_handoff.ps1'),'-Job',str(job/'handoff.json')])
    try:child=subprocess.Popen(command,cwd=job,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0),close_fds=True)
    except OSError:raise UpdateError('update_handoff_failed') from None
    if child.poll() is not None:raise UpdateError('update_handoff_failed')
    owner=owned_job(app,job)
    if owner is not None:
        atomic(job/'handoff-active.json',{'format':'weplay-update-active-v1','pid':child.pid})
        owner['files']=job_inventory(job)
        atomic(job/'job-owner.json',owner)
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
