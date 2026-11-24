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
from internet_room import AdapterNotReady, InternetError, parse, select_interface
from lan_interfaces import enumerate_interfaces

FORMAT = 'weplay-linux-tunnel-v1'
REPORTED_FAILURES = (
    'internet_adapter_invalid', 'internet_address_conflict', 'internet_connect_timeout',
    'internet_direct_unavailable', 'internet_disconnected', 'internet_network_overlap',
    'internet_relay_unavailable', 'internet_runtime_invalid',
)


def failure_code(value):
    code = value.get('code')
    return code if code in REPORTED_FAILURES else 'internet_connect_failed'


class StaleTunnelState(InternetError):
    def __init__(self):
        super().__init__('internet_disconnected')


class ElevatedEasyTier:
    def __init__(self, root, room, *, host, cancel_check=None, progress=None):
        self.root, self.room, self.host = compatibility.plain(root), room, host
        self.cancel_check, self.progress = cancel_check, progress
        self.child = self.thread = None
        self.stop = threading.Event(); self.lock = threading.Lock()
        self.latest = None

    def start(self):
        if os.geteuid() == 0:
            raise InternetError('internet_game_must_not_be_root')
        from easytier_runtime import verify_runtime
        verify_runtime(self.root)
        if self.cancel_check: self.cancel_check()
        script = compatibility.plain(self.root / 'Launcher/linux_elevation.py')
        payload = json.dumps(dict(format=FORMAT,root=str(self.root),room=self.room.token(),host=self.host),
                             separators=(',',':')).encode('ascii') + b'\n'
        if len(payload) > 8192:
            raise InternetError('internet_request_invalid')
        try:
            self.child = subprocess.Popen(['/usr/bin/pkexec', '--disable-internal-agent', '/usr/bin/python3', '-I', '-B', str(script), '--worker'],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0)
        except FileNotFoundError:
            raise InternetError('internet_elevation_failed') from None
        try:
            try:
                self.child.stdin.write(payload)
                self.child.stdin.flush()
            except BrokenPipeError:
                raise InternetError('internet_elevation_cancelled' if self.child.poll() is not None
                                    else 'internet_connect_failed') from None
            thread = threading.Thread(target=self._receive, name='weplay-linux-tunnel', daemon=True)
            thread.start()
            self.thread = thread
            deadline = time.monotonic() + 70
            while time.monotonic() < deadline:
                if self.cancel_check: self.cancel_check()
                reading = self.thread.is_alive()
                with self.lock: value = self.latest
                if value is not None:
                    if value.get('phase') == 'error':
                        raise InternetError(failure_code(value))
                    try:
                        interface, peers = self.snapshot()
                    except (AdapterNotReady, StaleTunnelState):
                        pass
                    else:
                        if time.monotonic() >= deadline:
                            break
                        if self.host or self.room.coordinator in peers:
                            if self.cancel_check: self.cancel_check()
                            if self.progress: self.progress('internet_connected')
                            return interface
                if not reading:
                    code = 'internet_elevation_cancelled' if self.child.poll() is not None else 'internet_connect_failed'
                    raise InternetError(code)
                time.sleep(.1)
            raise InternetError('internet_connect_timeout')
        except BaseException as primary:
            try:
                self.close()
            except Exception as cleanup:
                raise primary from cleanup
            raise

    def _receive(self):
        child = self.child
        pending = b''
        heartbeat = True
        selector = selectors.DefaultSelector()
        try:
            selector.register(child.stdout, selectors.EVENT_READ)
            # The pipe can still hold a final diagnostic after the worker exits.
            while not self.stop.is_set():
                for _, _ in selector.select(.4):
                    raw = os.read(child.stdout.fileno(), 16385)
                    if not raw: return
                    pending += raw
                    if len(pending) > 32768: return
                    while b'\n' in pending:
                        line, pending = pending.split(b'\n',1)
                        value = json.loads(line)
                        if not isinstance(value,dict) or value.get('format') != FORMAT: return
                        with self.lock: self.latest = value
                if heartbeat and child.poll() is None:
                    try:
                        child.stdin.write(b'ping\n')
                        child.stdin.flush()
                    except BrokenPipeError:
                        heartbeat = False
        except (OSError,ValueError): pass
        finally:
            selector.close()
            # EOF is also the fail-closed root-worker lifetime boundary.
            try: child.stdin.close()
            except OSError: pass

    def alive(self):
        child = self.child
        return child is not None and child.poll() is None

    def snapshot(self):
        with self.lock: value = self.latest
        child = self.child
        if child is None:
            raise InternetError('internet_disconnected')
        if isinstance(value, dict) and value.get('phase') == 'error':
            raise InternetError(failure_code(value))
        if child.poll() is not None:
            raise InternetError('internet_disconnected')
        if not isinstance(value,dict) or value.get('phase') != 'ready': raise InternetError('internet_disconnected')
        try:
            if set(value) != {'format','phase','address','device','peers','written_monotonic_ns'}: raise ValueError()
            written = value['written_monotonic_ns']
            if type(written) is not int or written < 0:
                raise ValueError()
            if not 0 <= time.monotonic_ns() - written <= 10_000_000_000:
                raise StaleTunnelState()
            interface = select_interface(self.room, value['address'], value['device'],
                                         enumerate_interfaces(allow_empty=True, include_all=True))
            peers = value['peers']
            if not isinstance(peers,dict) or len(peers)>255: raise ValueError()
            for address, cost in peers.items():
                if not interface.contains(address) or address==interface.address or cost not in ('p2p','relay'): raise ValueError()
                if self.room.mode=='direct' and cost!='p2p': raise ValueError()
            if self.child is not child or child.poll() is not None:
                raise InternetError('internet_disconnected')
            if time.monotonic_ns() - written > 10_000_000_000:
                raise StaleTunnelState()
            return interface,peers
        except InternetError:
            raise
        except (ValueError,KeyError,TypeError):
            raise InternetError('internet_status_invalid') from None

    def close(self):
        self.stop.set()
        if self.thread is not None: self.thread.join(timeout=3)
        if self.child is not None:
            try:
                if self.child.stdin and not self.child.stdin.closed: self.child.stdin.close()
                code = self.child.wait(timeout=20)
            except subprocess.TimeoutExpired: raise InternetError('internet_cleanup_failed') from None
            finally:
                if self.child.stdout: self.child.stdout.close()
            self.child = None
            if code != 0:
                raise InternetError('internet_cleanup_failed')


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
    from easytier_runtime import EasyTier, refresh_snapshot
    parent = Path(tempfile.mkdtemp(prefix='weplay-',dir='/run'))
    overlay = None
    selector = selectors.DefaultSelector()
    pending = b''
    last = time.monotonic()

    class ParentClosed(Exception):
        pass

    def send(record):
        print(json.dumps(dict(format=FORMAT,**record),separators=(',',':')),flush=True)

    def check_parent(timeout=0):
        nonlocal pending, last
        for _, _ in selector.select(timeout):
            raw = os.read(sys.stdin.fileno(), 1024)
            if not raw:
                raise ParentClosed()
            pending += raw
            if len(pending) > 4096:
                raise InternetError('internet_request_invalid')
            while b'\n' in pending:
                message, pending = pending.split(b'\n', 1)
                if message != b'ping':
                    raise InternetError('internet_request_invalid')
                last = time.monotonic()
        if time.monotonic() - last >= 15:
            raise InternetError('internet_disconnected')

    try:
        selector.register(sys.stdin.buffer, selectors.EVENT_READ)
        overlay = EasyTier(root, room, host=value['host'], private_parent=parent,
                           cancel_check=check_parent)
        overlay.start()
        while True:
            check_parent(.5)
            interface,peers = refresh_snapshot(overlay, check_parent)
            send(dict(phase='ready',address=interface.address,device=interface.name,peers=peers,
                      written_monotonic_ns=time.monotonic_ns()))
    except ParentClosed:
        return 0
    except (OSError,ValueError,RuntimeError,TimeoutError,subprocess.SubprocessError) as error:
        try:
            status = {'phase': 'error'}
            if isinstance(error, InternetError) and error.code in REPORTED_FAILURES:
                status['code'] = error.code
            send(status)
        except BrokenPipeError: pass
        return 2
    finally:
        selector.close()
        try:
            if overlay is not None:
                overlay.close()
        finally: shutil.rmtree(parent)


if __name__ == '__main__':
    if sys.argv[1:] != ['--worker']: raise SystemExit(2)
    try: raise SystemExit(worker())
    except (OSError,ValueError,RuntimeError,KeyError,TypeError,subprocess.SubprocessError): raise SystemExit(2)
