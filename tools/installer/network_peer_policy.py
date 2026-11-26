"""Lease only WePlay private UDP peers; persistent public-network blocks stay put."""
import ctypes as C
from ctypes import wintypes as W
import ipaddress
import os
from pathlib import Path
import re
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
        self.lease_expires=self._publish()
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
        except BaseException as primary:
            try:
                self.close()
            except Exception:
                primary.add_note('windows_network_cleanup_failed')
            raise

    @staticmethod
    def _read_previous_lease(path):
        with content.plain(path).open('rb') as stream:
            raw = stream.read(4097)
        if len(raw) > 4096:
            raise ValueError('Ungültiger alter Netzbereich.')
        entries = raw.decode('ascii').splitlines()
        fields = dict(line.split('=', 1) for line in entries)
        if (len(fields) != len(entries) or fields.get('format') != 'weplay-wfp-peers-v1'
                or not re.fullmatch('[0-9a-f]{32}', fields.get('nonce', ''))
                or not 0 < int(fields.get('parent', '0')) <= 0xffffffff
                or int(fields.get('parent_created', '0')) <= 0
                or int(fields.get('expires_tick_ms', '-1')) < 0):
            raise ValueError('Ein unbekannter Netzbereich bleibt unverändert.')
        return fields

    def _require_inactive(self, fields):
        if int(fields['expires_tick_ms']) <= self.api.k.GetTickCount64():
            return
        C.set_last_error(0)
        handle = self.api.k.OpenProcess(0x1000, False, int(fields['parent']))
        if not handle:
            if C.get_last_error() == 87:  # ERROR_INVALID_PARAMETER: the PID is gone.
                return
            raise RuntimeError('Diese Kopie hat bereits einen aktiven WePlay-Netzbereich.')
        try:
            created, ended, kernel, user = (W.FILETIME() for _ in range(4))
            self.api.check(self.api.k.GetProcessTimes(handle, C.byref(created), C.byref(ended),
                                                       C.byref(kernel), C.byref(user)))
            live = (created.dwLowDateTime | (created.dwHighDateTime << 32)) == int(fields['parent_created'])
        finally:
            self.api.k.CloseHandle(handle)
        if live:
            raise RuntimeError('Diese Kopie hat bereits einen aktiven WePlay-Netzbereich.')

    def _read_ready_marker(self):
        try:
            with content.plain(self.ready).open('rb') as stream:
                marker = stream.read(34)
        except FileNotFoundError:
            return None
        if not re.fullmatch(rb'[0-9a-f]{32}\n', marker):
            raise ValueError('Eine unbekannte Netzbereichsbestätigung bleibt unverändert.')
        return marker

    def _recover(self):
        marker = self._read_ready_marker()
        if not self.path.exists():
            if marker is None:
                return
            # Older recoveries could archive the lease before removing its
            # readiness marker. Use only a matching, inactive retained lease.
            matched = None
            for path in self.folder.glob('wfp-peers-recovered-*.ini'):
                if not re.fullmatch(r'wfp-peers-recovered-[0-9a-f]{32}\.ini', path.name):
                    continue
                try:
                    fields = self._read_previous_lease(path)
                except (OSError, ValueError):
                    continue
                if marker == (fields['nonce'] + '\n').encode('ascii'):
                    if matched is not None:
                        raise ValueError('Eine unbekannte Netzbereichsbestätigung bleibt unverändert.')
                    matched = fields
            if matched is None:
                raise ValueError('Eine unbekannte Netzbereichsbestätigung bleibt unverändert.')
            self._require_inactive(matched)
            self.ready.unlink(missing_ok=True)
            return
        fields = self._read_previous_lease(self.path)
        self._require_inactive(fields)
        if marker is not None:
            if marker != (fields['nonce'] + '\n').encode('ascii'):
                raise ValueError('Eine unbekannte Netzbereichsbestätigung bleibt unverändert.')
            self.ready.unlink(missing_ok=True)
        backup = content.plain(self.folder / ('wfp-peers-recovered-' + uuid.uuid4().hex + '.ini'))
        self.path.rename(backup)

    def _publish(self,*,expired=False,renew_before=None):
        now=self.api.k.GetTickCount64()
        if renew_before is not None and now>=renew_before:
            raise TimeoutError('Der begrenzte Windows-Netzbereich ist abgelaufen.')
        if self.path.exists():
            stored=self.path.read_bytes()
            if len(stored)>4096 or ('nonce='+self.nonce+'\n').encode('ascii') not in stored:
                raise ValueError('Der Netzbereich wurde von einem anderen Starter verändert.')
        expires=0 if expired else now+5000
        fields={'format':'weplay-wfp-peers-v1','nonce':self.nonce,'parent':str(os.getpid()),
                'parent_created':str(self.created),'expires_tick_ms':str(expires),
                'local':self.value['local'],'coordinator':self.value['coordinator'],
                'mode':'group' if self.value['group'] else 'pair'}
        if not self.value['group']:fields['peer']=self.value['peer']
        raw=''.join(key+'='+value+'\n' for key,value in fields.items()).encode('ascii')
        temporary=None
        try:
            with tempfile.NamedTemporaryFile(dir=self.folder,prefix='wfp-lease-',suffix='.tmp',delete=False) as target:
                temporary=Path(target.name);target.write(raw);target.flush();os.fsync(target.fileno())
            deadline = time.monotonic() + .25
            while True:
                if renew_before is not None and self.api.k.GetTickCount64()>=renew_before:
                    raise TimeoutError('Der begrenzte Windows-Netzbereich ist abgelaufen.')
                try:
                    os.replace(temporary, self.path)
                    break
                except PermissionError:
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(.02)
        finally:
            if temporary is not None and temporary.exists():temporary.unlink()
        return expires

    def _heartbeat(self):
        while not self.stop.wait(1):
            try:
                if self.process is not None and self.process.poll() is not None:
                    raise RuntimeError('Der begrenzte Windows-Netzbereich wurde vorzeitig geschlossen.')
                self.lease_expires=self._publish(renew_before=self.lease_expires)
            except BaseException as error:
                # Windows may report an exclusive file handle as a plain
                # PermissionError. Retry only inside the last published lease.
                if isinstance(error,PermissionError) and self.api.k.GetTickCount64()<self.lease_expires:
                    continue
                self.error=error;self.stop.set()

    def close(self):
        self.stop.set()
        if self.thread:self.thread.join(timeout=3)
        expire_error = None
        try:
            if self.path.exists() and ('nonce='+self.nonce+'\n').encode('ascii') in self.path.read_bytes():
                self._publish(expired=True)
        except (OSError, ValueError) as error:
            # A failed write leaves the existing short expiry in force.
            expire_error = error
        code = 0
        if self.process:
            code = self.process.wait(timeout=10)
            self.process.close()
            self.process = None
        if self.ready.exists() and self.ready.read_bytes()==(self.nonce+'\n').encode('ascii'):
            self.ready.unlink()
        if self.path.exists() and ('nonce='+self.nonce+'\n').encode('ascii') in self.path.read_bytes():
            self.path.unlink()
        if code:
            raise RuntimeError('Der Windows-Netzhelfer meldet einen Fehler beim Schließen.')
        if expire_error is not None:
            raise expire_error
