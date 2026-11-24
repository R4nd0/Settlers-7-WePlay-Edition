"""Own the ordinary loopback History bootstrap service for one mixed launch.

The caller first verifies its installed game/package. The profile belongs to the
same session lifetime; opaque channel identity and Storm principal are separate.
No persistent solo identity or account is read.
"""
import importlib
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid

import compatibility as content
import portable_lock


def launcher_module():
    folder=Path(__file__).resolve().parent
    if not (folder/'launch_local.py').is_file():folder=folder.parent/'offline-ui'
    previous=sys.path[:]
    try:
        sys.path.insert(0,str(folder))
        return importlib.import_module('launch_local')
    finally:sys.path[:]=previous


class HistorySessionService:
    def __init__(self,root,*,profile_id,name,cancel_check=None):
        if (not isinstance(profile_id,str) or len(profile_id)!=36
                or str(uuid.UUID(profile_id))!=profile_id or uuid.UUID(profile_id).int==0
                or not isinstance(name,str) or not 1<=len(name)<=64
                or any(not 32<=ord(c)<=126 for c in name)):
            raise ValueError('crossplay_configuration_invalid')
        self.root=content.plain(root);self.profile_id=profile_id;self.name=name
        self.cancel_check = cancel_check
        self.process=self.lock=self.output=self.errors=None
        self.runtime=self.owned_runtime=self.previous_runtime=None
        self.previous_times=None;self.closed=False;self.started=False

    def start(self):
        if self.started or self.closed:raise ValueError('crossplay_service_closed')
        self.started=True
        data=content.plain(self.root/'LocalData')
        if not data.is_dir():raise ValueError('crossplay_profile_invalid')
        try:
            if self.cancel_check:
                self.cancel_check()
            self.lock=content.plain(data/'session.lock').open('a+b')
            if self.lock.tell()==0:self.lock.write(b'0');self.lock.flush()
            self.lock.seek(0)
            portable_lock.locking(self.lock.fileno(),portable_lock.LK_NBLCK,1)
            self.runtime=content.plain(data/'s7-local-runtime.ini')
            if self.runtime.exists():
                info=self.runtime.stat()
                if info.st_size>2048:raise ValueError('crossplay_configuration_invalid')
                self.previous_runtime=self.runtime.read_bytes()
                if not self.previous_runtime.startswith(b'format=s7-local-runtime-v1\n'):
                    raise ValueError('crossplay_configuration_invalid')
                self.previous_times=(info.st_atime_ns,info.st_mtime_ns)
            token=uuid.uuid4().hex
            self.log=content.plain(data/('mixed-history-'+token+'-server.jsonl'))
            self.output=self.log.open('xb')
            self.errors=content.plain(data/('mixed-history-'+token+'-errors.txt')).open('xb')
            # Omit --seconds: the game/session owner, rather than a timer, owns
            # this service. The existing native configuration freshness guard
            # remains unchanged and the configuration is written just now.
            self.process=subprocess.Popen([str(content.plain(self.root/'Runtime/dotnet/dotnet.exe')),
                str(content.plain(self.root/'Server/S7Patcher.dll')),'--local-config-server',
                '--local-catalogue-probe','--http-port','0','--udp-port','0'],
                stdout=self.output,stderr=self.errors,
                creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            launcher=launcher_module()
            configuration,port=launcher.service_ready(self.process,self.log,cancel_check=self.cancel_check)
            launcher.write_configuration(data,configuration,port,True,display_name=self.name,profile_id=self.profile_id)
            self.owned_runtime=self.runtime.read_bytes()
            self.pump(0)
            return self
        except BaseException:
            self.close();raise

    def pump(self,timeout=0):
        if self.closed or self.process is None or self.process.poll() is not None:
            raise RuntimeError('crossplay_service_failed')

    def close(self):
        if self.closed:return
        failed=False
        # Keep the lock and original configuration while the owned child may
        # still be using this copy. A later close can finish after it exits.
        if self.process is not None:
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=5)
            self.process = None
        try:
            if self.owned_runtime is not None:
                if not self.runtime.is_file() or self.runtime.read_bytes()!=self.owned_runtime:
                    failed=True
                elif self.previous_runtime is None:self.runtime.unlink()
                else:
                    temporary=content.plain(self.runtime.with_name('restore-'+uuid.uuid4().hex+'.tmp'))
                    try:
                        with temporary.open('xb') as stream:stream.write(self.previous_runtime)
                        os.utime(temporary, ns=self.previous_times)
                        deadline = time.monotonic() + .25
                        while True:
                            try:
                                temporary.replace(self.runtime)
                                break
                            except PermissionError:
                                if time.monotonic() >= deadline:
                                    raise
                                time.sleep(.02)
                    finally:temporary.unlink(missing_ok=True)
        finally:
            for stream in (self.output,self.errors):
                if stream is not None:stream.close()
            if self.lock is not None:
                # A constructor failure can precede successful acquisition;
                # closing the owned handle also releases any acquired lock.
                self.lock.close()
            self.closed=True
        if failed:raise RuntimeError('crossplay_service_cleanup_failed')

    def __enter__(self):return self.start()
    def __exit__(self,*_args):self.close()
