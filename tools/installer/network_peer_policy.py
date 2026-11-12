"""Lease only WePlay private UDP peers; persistent public-network blocks stay put."""
import ctypes as C
from ctypes import wintypes as W
import ipaddress
import os
from pathlib import Path
import tempfile
import threading
import time
import uuid

import compatibility as content
import isolated_process
import network_elevation


def configuration(value):
    if not isinstance(value,dict) or type(value.get('group')) is not bool:
        raise ValueError('Ungültiger WePlay-Netzbereich.')
    keys={'local','coordinator','group'}|({'peer'} if not value['group'] else set())
    if set(value)!=keys:raise ValueError('Ungültiger WePlay-Netzbereich.')
    networks=tuple(map(ipaddress.IPv4Network,('10.0.0.0/8','172.16.0.0/12','192.168.0.0/16')))
    for key in keys-{'group'}:
        address=ipaddress.IPv4Address(value[key])
        if str(address)!=value[key] or address.packed[-1] in (0,255) or not any(address in network for network in networks):
            raise ValueError('WePlay-Netzfreigaben sind auf private Mitspieler begrenzt.')
    return dict(value)


class Session:
    def __init__(self,root,value):
        self.root=content.plain(root)
        self.value=configuration(value)
        self.folder=content.plain(self.root/'LocalData')
        self.folder.mkdir(exist_ok=True)
        self.path=content.plain(self.folder/'wfp-peers.ini')
        self.ready=content.plain(self.folder/'wfp-peers.ready')
        self.nonce=uuid.uuid4().hex
        self.api=isolated_process.Windows()
        self.api.k.GetTickCount64.argtypes=[];self.api.k.GetTickCount64.restype=C.c_ulonglong
        self.api.k.GetProcessTimes.argtypes=[W.HANDLE,*([C.POINTER(W.FILETIME)]*4)]
        self.api.k.GetProcessTimes.restype=W.BOOL
        created,ended,kernel,user=(W.FILETIME() for _ in range(4))
        self.api.check(self.api.k.GetProcessTimes(C.c_void_p(-1),C.byref(created),C.byref(ended),C.byref(kernel),C.byref(user)))
        self.created=created.dwLowDateTime|(created.dwHighDateTime<<32)
        self.stop=threading.Event();self.thread=None;self.process=None;self.error=None
        self._recover()
        self._publish()
        self.thread=threading.Thread(target=self._heartbeat,name='weplay-network-lease',daemon=True)
        self.thread.start()
        try:
            self.process=network_elevation.start(self.root/'Launcher/weplay_network_policy.exe',
                self.root/'Base/Bin/Settlers7R.exe','serve-lan')
            deadline=time.monotonic()+20
            while time.monotonic()<deadline:
                if self.error:raise self.error
                if self.process.poll() is not None:
                    raise RuntimeError('Windows hat den begrenzten WePlay-Netzbereich abgelehnt.')
                if self.ready.exists() and self.ready.read_bytes()==(self.nonce+'\n').encode('ascii'):
                    break
                time.sleep(.05)
            else:raise TimeoutError('Der WePlay-Netzbereich wurde nicht rechtzeitig bestätigt.')
        except BaseException:
            self.close()
            raise

    def _recover(self):
        if not self.path.exists():
            if self.ready.exists():raise ValueError('Eine unbekannte Netzbereichsbestätigung bleibt unverändert.')
            return
        if self.path.stat().st_size>4096:raise ValueError('Ungültiger alter Netzbereich.')
        raw=self.path.read_text(encoding='ascii')
        fields=dict(line.split('=',1) for line in raw.splitlines())
        if fields.get('format')!='weplay-wfp-peers-v1' or len(fields.get('nonce',''))!=32:
            raise ValueError('Ein unbekannter Netzbereich bleibt unverändert.')
        pid=int(fields['parent']);live=False
        handle=self.api.k.OpenProcess(0x1000,False,pid)
        if handle:
            try:
                created,ended,kernel,user=(W.FILETIME() for _ in range(4))
                self.api.check(self.api.k.GetProcessTimes(handle,C.byref(created),C.byref(ended),C.byref(kernel),C.byref(user)))
                live=(created.dwLowDateTime|(created.dwHighDateTime<<32))==int(fields['parent_created'])
            finally:self.api.k.CloseHandle(handle)
        if live and int(fields['expires_tick_ms'])>self.api.k.GetTickCount64():
            raise RuntimeError('Diese Kopie hat bereits einen aktiven WePlay-Netzbereich.')
        backup=content.plain(self.folder/('wfp-peers-recovered-'+uuid.uuid4().hex+'.ini'))
        self.path.rename(backup)
        if self.ready.exists():
            if self.ready.read_bytes()!=(fields['nonce']+'\n').encode('ascii'):
                raise ValueError('Eine unbekannte Netzbereichsbestätigung bleibt unverändert.')
            self.ready.unlink()

    def _publish(self,*,expired=False):
        if self.path.exists():
            stored=self.path.read_bytes()
            if len(stored)>4096 or ('nonce='+self.nonce+'\n').encode('ascii') not in stored:
                raise ValueError('Der Netzbereich wurde von einem anderen Starter verändert.')
        fields={'format':'weplay-wfp-peers-v1','nonce':self.nonce,'parent':str(os.getpid()),
                'parent_created':str(self.created),'expires_tick_ms':str(0 if expired else self.api.k.GetTickCount64()+5000),
                'local':self.value['local'],'coordinator':self.value['coordinator'],
                'mode':'group' if self.value['group'] else 'pair'}
        if not self.value['group']:fields['peer']=self.value['peer']
        raw=''.join(key+'='+value+'\n' for key,value in fields.items()).encode('ascii')
        temporary=None
        try:
            with tempfile.NamedTemporaryFile(dir=self.folder,prefix='wfp-lease-',suffix='.tmp',delete=False) as target:
                temporary=Path(target.name);target.write(raw);target.flush();os.fsync(target.fileno())
            os.replace(temporary,self.path)
        finally:
            if temporary is not None and temporary.exists():temporary.unlink()

    def _heartbeat(self):
        while not self.stop.wait(1):
            try:
                if self.process is not None and self.process.poll() is not None:
                    raise RuntimeError('Der begrenzte Windows-Netzbereich wurde vorzeitig geschlossen.')
                self._publish()
            except BaseException as error:
                self.error=error;self.stop.set()

    def close(self):
        self.stop.set()
        if self.thread:self.thread.join(timeout=3)
        if self.path.exists() and ('nonce='+self.nonce+'\n').encode('ascii') in self.path.read_bytes():
            self._publish(expired=True)
        if self.process:
            try:
                code=self.process.wait(timeout=10)
                if code:raise RuntimeError('Der Windows-Netzhelfer meldet einen Fehler beim Schließen.')
            finally:self.process.close()
            self.process=None
        if self.ready.exists() and self.ready.read_bytes()==(self.nonce+'\n').encode('ascii'):
            self.ready.unlink()
        if self.path.exists() and ('nonce='+self.nonce+'\n').encode('ascii') in self.path.read_bytes():
            self.path.unlink()
