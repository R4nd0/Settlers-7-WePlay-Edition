"""Polkit boundary for an owned native tunnel; never elevate the game or GUI.

Room credentials travel over inherited pipes. The root worker stores its
configuration in a root-only /run directory, closes on parent EOF/heartbeat
expiry, and owns only its direct pinned EasyTier child.
"""
import json
import os
from pathlib import Path
import selectors
import shutil
import subprocess
import sys
import tempfile
import threading
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
if (HERE.parent / 'installer').is_dir(): sys.path.insert(0, str(HERE.parent / 'installer'))
import compatibility
from internet_room import InternetError, parse, select_interface
from lan_interfaces import enumerate_interfaces

FORMAT = 'weplay-linux-tunnel-v1'


class ElevatedEasyTier:
    def __init__(self, root, room, *, host, cancel_check=None, progress=None):
        self.root, self.room, self.host = compatibility.plain(root), room, host
        self.cancel_check, self.progress = cancel_check, progress
        self.child = self.thread = None
        self.stop = threading.Event(); self.lock = threading.Lock()
        self.latest = None; self.updated = 0

    def start(self):
        if os.geteuid() == 0:
            raise InternetError('internet_game_must_not_be_root')
        from easytier_runtime import verify_runtime
        verify_runtime(self.root)
        if self.cancel_check: self.cancel_check()
        script = compatibility.plain(self.root / 'Launcher/linux_elevation.py')
        self.child = subprocess.Popen(['/usr/bin/pkexec', '--disable-internal-agent', '/usr/bin/python3', '-I', '-B', str(script), '--worker'],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0)
        payload = json.dumps(dict(format=FORMAT,root=str(self.root),room=self.room.token(),host=self.host),
                             separators=(',',':')).encode('ascii') + b'\n'
        if len(payload) > 8192:
            self.close(); raise InternetError('internet_request_invalid')
        self.child.stdin.write(payload); self.child.stdin.flush()
        self.thread = threading.Thread(target=self._receive, name='weplay-linux-tunnel', daemon=True)
        self.thread.start()
        deadline = time.monotonic() + 70
        try:
            while time.monotonic() < deadline:
                if self.cancel_check: self.cancel_check()
                with self.lock: value = self.latest
                if value is not None:
                    if value.get('phase') == 'error': raise InternetError('internet_connect_failed')
                    interface, _ = self.snapshot()
                    if self.progress: self.progress('internet_connected')
                    return interface
                if self.child.poll() is not None: raise InternetError('internet_elevation_cancelled')
                time.sleep(.1)
            raise InternetError('internet_connect_timeout')
        except BaseException:
            self.close(); raise

    def _receive(self):
        pending = b''
        selector = selectors.DefaultSelector()
        try:
            selector.register(self.child.stdout, selectors.EVENT_READ)
            while not self.stop.is_set() and self.child.poll() is None:
                for _, _ in selector.select(.4):
                    raw = os.read(self.child.stdout.fileno(), 16385)
                    if not raw: return
                    pending += raw
                    if len(pending) > 32768: return
                    while b'\n' in pending:
                        line, pending = pending.split(b'\n',1)
                        value = json.loads(line)
                        if not isinstance(value,dict) or value.get('format') != FORMAT: return
                        with self.lock: self.latest, self.updated = value, time.monotonic()
                self.child.stdin.write(b'ping\n'); self.child.stdin.flush()
        except (OSError,ValueError): pass
        finally:
            selector.close()
            # EOF is also the fail-closed root-worker lifetime boundary.
            try: self.child.stdin.close()
            except OSError: pass

    def snapshot(self):
        with self.lock: value, updated = self.latest, self.updated
        if self.child is None or self.child.poll() is not None or time.monotonic()-updated > 10:
            raise InternetError('internet_disconnected')
        if not isinstance(value,dict) or value.get('phase') != 'ready': raise InternetError('internet_disconnected')
        try:
            if set(value) != {'format','phase','address','device','peers'}: raise ValueError()
            interface = select_interface(self.room,value['address'],value['device'],enumerate_interfaces())
            peers = value['peers']
            if not isinstance(peers,dict) or len(peers)>255: raise ValueError()
            for address, cost in peers.items():
                if not interface.contains(address) or address==interface.address or cost not in ('p2p','relay'): raise ValueError()
                if self.room.mode=='direct' and cost!='p2p': raise ValueError()
            return interface,peers
        except (ValueError,KeyError,TypeError): raise InternetError('internet_status_invalid') from None

    def close(self):
        self.stop.set()
        if self.thread is not None: self.thread.join(timeout=3)
        if self.child is not None:
            try:
                if self.child.stdin and not self.child.stdin.closed: self.child.stdin.close()
                self.child.wait(timeout=20)
            except subprocess.TimeoutExpired: raise InternetError('internet_cleanup_failed') from None
            finally:
                if self.child.stdout: self.child.stdout.close()
            self.child = None


def worker():
    if os.geteuid() != 0: return 2
    user = os.environ.get('PKEXEC_UID')
    if user is None or not user.isdecimal() or int(user)==0: return 2
    raw = sys.stdin.buffer.readline(8193)
    if len(raw)>8192 or not raw.endswith(b'\n'): return 2
    value = json.loads(raw)
    if (not isinstance(value,dict) or set(value)!={'format','root','room','host'} or value['format']!=FORMAT
            or type(value['host']) is not bool): return 2
    root = compatibility.plain(value['root'])
    if root.stat().st_uid != int(user): return 2
    room = parse(value['room'])
    from easytier_runtime import EasyTier
    parent = Path(tempfile.mkdtemp(prefix='weplay-',dir='/run'))
    overlay = EasyTier(root,room,host=value['host'],private_parent=parent)
    selector = selectors.DefaultSelector()
    def send(record):
        print(json.dumps(dict(format=FORMAT,**record),separators=(',',':')),flush=True)
    try:
        overlay.start()
        selector.register(sys.stdin.buffer,selectors.EVENT_READ)
        last = time.monotonic()
        pending = b''
        while time.monotonic()-last < 15:
            for _,_ in selector.select(.5):
                line = os.read(sys.stdin.fileno(),1024)
                if not line: return 0
                pending += line
                if len(pending)>4096: return 2
                while b'\n' in pending:
                    message,pending = pending.split(b'\n',1)
                    if message != b'ping': return 2
                    last = time.monotonic()
            interface,peers = overlay.snapshot()
            send(dict(phase='ready',address=interface.address,device=interface.name,peers=peers))
        return 2
    except (OSError,ValueError,RuntimeError,TimeoutError,subprocess.SubprocessError):
        try: send(dict(phase='error'))
        except BrokenPipeError: pass
        return 2
    finally:
        selector.close()
        try: overlay.close()
        finally: shutil.rmtree(parent)


if __name__ == '__main__':
    if sys.argv[1:] != ['--worker']: raise SystemExit(2)
    try: raise SystemExit(worker())
    except (OSError,ValueError,RuntimeError,KeyError,TypeError,subprocess.SubprocessError): raise SystemExit(2)
