"""Own one pinned EasyTier child, without a system service or account state."""
import json
import ipaddress
import os
from pathlib import Path
import secrets
import re
import shutil
import socket
import subprocess
import tempfile
import time
import uuid

import compatibility
from internet_room import InternetError, select_interface
from lobby_directory import private_address
from lan_child_job import ServiceJob
from lan_interfaces import enumerate_interfaces

LOCK = Path(__file__).with_name('easytier-runtime.json')


def verify_runtime(root):
    directory = compatibility.plain(Path(root) / 'Runtime/easytier')
    return verify_directory(directory)


def verify_directory(directory):
    directory = compatibility.plain(directory)
    try:
        manifest = json.loads(LOCK.read_text(encoding='utf-8'))
        files = manifest['files']
        if (manifest['format'] != 'weplay-easytier-runtime-v1' or not files or
                any('/' in name or '\\' in name or ':' in name or name in ('.', '..') for name in files)):
            raise ValueError()
        if {p.name for p in directory.iterdir()} != set(files):
            raise ValueError()
        for name, digest in files.items():
            path = compatibility.plain(directory / name)
            if not path.is_file() or compatibility.digest(path) != digest:
                raise ValueError()
        if not {'easytier-core.exe', 'easytier-cli.exe', 'wintun.dll'} <= set(files):
            raise ValueError()
    except (OSError, ValueError, KeyError, TypeError):
        raise InternetError('internet_runtime_invalid') from None
    return directory


def clean_environment():
    # CLI env options otherwise override the protected config. No PYTHONPATH,
    # external DLL search folders or caller-supplied EasyTier configuration.
    environment = {k: v for k, v in os.environ.items()
                   if not k.upper().startswith(('ET_', 'RUST_', 'PYTHON'))}
    system = os.environ.get('SystemRoot', r'C:\Windows')
    environment['PATH'] = str(Path(system) / 'System32')
    return environment


def configuration(room, address, instance_id, device_name):
    # JSON strings are valid TOML basic strings for these strictly validated
    # values. No invitation text is interpreted as TOML or a shell command.
    quote = json.dumps
    lines = [f'instance_id = {quote(instance_id)}', f'instance_name = {quote(device_name)}',
             f'hostname = {quote(device_name)}', f'ipv4 = {quote(address + "/24")}',
             'dhcp = false', f'routes = [{quote(room.subnet)}]',
             'listeners = ["udp://0.0.0.0:11010", "tcp://0.0.0.0:11010"]']
    if room.mode == 'direct':
        # A direct room must not quietly depend on the compiled-in STUN list.
        lines += ['stun_servers = []', 'stun_servers_v6 = []']
    lines += ['[network_identity]', f'network_name = {quote("weplay-" + room.room_id)}',
              f'network_secret = {quote(room.secret)}']
    for peer in room.bootstrap:
        lines += ['[[peer]]', f'uri = {quote(peer)}']
    flags = dict(dev_name=device_name, default_protocol='udp', enable_encryption=True,
                 encryption_algorithm='aes-gcm', enable_exit_node=False, accept_dns=False,
                 no_tun=False, enable_udp_broadcast_relay=False, multi_thread=False,
                 relay_network_whitelist='', private_mode=room.mode == 'direct',
                 p2p_only=room.mode == 'direct', disable_upnp=True,
                 enable_kcp_proxy=False, enable_quic_proxy=False, bind_device=True)
    lines += ['[flags]'] + [f'{k} = {str(v).lower() if isinstance(v, bool) else quote(v)}' for k, v in flags.items()]
    return '\n'.join(lines) + '\n'


def peer_snapshot(raw, room, device_name):
    if not isinstance(raw, bytes) or len(raw) > 131072:
        raise InternetError('internet_status_invalid')
    try:
        rows = json.loads(raw)
        if not isinstance(rows, list) or not 1 <= len(rows) <= 256:
            raise ValueError()
        own = [r for r in rows if isinstance(r, dict) and r.get('cost') == 'Local']
        if len(own) != 1 or own[0].get('hostname') != device_name:
            raise ValueError()
        local = own[0]['ipv4']
        # A Local row's /24 must agree with the controller request as well.
        private_address(local)
        if own[0].get('cidr') != local + '/24' or not room.network.network_address < ipaddress.IPv4Address(local) < room.network.broadcast_address:
            raise ValueError()
        peers = {}
        for row in rows:
            if not isinstance(row, dict):
                raise ValueError()
            if row is own[0] or not row.get('ipv4'):
                continue  # Shared bootstrap nodes have no virtual game address.
            address = row['ipv4']
            private_address(address)
            if ipaddress.IPv4Address(address) not in room.network:
                raise ValueError()
            if address == local or address in peers:
                raise InternetError('internet_address_conflict')
            cost = row.get('cost')
            if (row.get('cidr') != address + '/24' or not isinstance(cost, str) or
                    not (cost == 'p2p' or re.fullmatch(r'relay\([2-9][0-9]?\)', cost))):
                raise ValueError()
            peers[address] = 'p2p' if cost == 'p2p' else 'relay'
        return local, peers
    except InternetError:
        raise
    except (ValueError, TypeError, KeyError, UnicodeError):
        raise InternetError('internet_status_invalid') from None


class EasyTier:
    def __init__(self, root, room, *, host, cancel_check=None, progress=None,
                 job_factory=ServiceJob, interfaces=enumerate_interfaces):
        self.root, self.room, self.host = Path(root), room, host
        self.cancel_check, self.progress = cancel_check, progress
        self.job_factory, self.interfaces = job_factory, interfaces
        self.instance_id = str(uuid.uuid4())
        self.device_name = 'WePlay-' + self.instance_id[:8]
        self.address = str(room.network.network_address + (1 if host else secrets.randbelow(253) + 2))
        self.job = self.child = self.private = self.runtime = None
        self.portal = None

    def checkpoint(self, phase='internet_connect'):
        if self.cancel_check:
            self.cancel_check()
        if self.progress:
            self.progress(phase)

    def start(self):
        self.checkpoint()
        self.runtime = verify_runtime(self.root)
        import ctypes
        if os.name == 'nt' and not ctypes.windll.shell32.IsUserAnAdmin():
            raise InternetError('internet_admin_required')
        try:
            data = compatibility.plain(self.root / 'LocalData')
            data.mkdir(exist_ok=True)
            # CPython 3.14 on Windows gives mode 0700 directories a protected
            # DACL for the current user and administrators. mkdtemp uses 0700.
            self.private = Path(tempfile.mkdtemp(prefix='easytier-', dir=data))
            config = self.private / 'network.toml'
            with config.open('x', encoding='utf-8') as stream:
                stream.write(configuration(self.room, self.address, self.instance_id, self.device_name))
            with socket.socket() as probe:
                probe.bind(('127.0.0.1', 0))
                self.portal = f'127.0.0.1:{probe.getsockname()[1]}'
            self.job = self.job_factory()
            self.child = self.job.start([str(self.runtime / 'easytier-core.exe'), '-c', str(config),
                '--disable-env-parsing', '--rpc-portal', self.portal,
                '--rpc-portal-whitelist', '127.0.0.1/32', '--console-log-level', 'off', '--file-log-level', 'off'],
                cwd=self.private, env=clean_environment(), stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            return self.wait_ready()
        except BaseException:
            self.close()
            raise

    def snapshot(self):
        if self.child is None or self.child.poll() is not None:
            raise InternetError('internet_disconnected')
        # Do not query `node` or `config`: those contain the room secret.
        with tempfile.TemporaryFile(dir=self.private) as output:
            result = subprocess.run([str(self.runtime / 'easytier-cli.exe'), '-p', self.portal,
                '-i', self.instance_id, '-o', 'json', 'peer'], cwd=self.private,
                env=clean_environment(), stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.DEVNULL,
                timeout=3, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            if result.returncode or output.tell() > 131072:
                raise InternetError('internet_status_invalid')
            output.seek(0)
            address, peers = peer_snapshot(output.read(131073), self.room, self.device_name)
        if address != self.address:
            raise InternetError('internet_address_conflict')
        if self.room.mode == 'direct' and any(cost != 'p2p' for cost in peers.values()):
            raise InternetError('internet_direct_unavailable')
        interface = select_interface(self.room, address, self.device_name, self.interfaces())
        return interface, peers

    def wait_ready(self, timeout=45):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.checkpoint()
            try:
                interface, peers = self.snapshot()
                if self.host or self.room.coordinator in peers:
                    self.checkpoint('internet_ready')
                    return interface
            except (InternetError, OSError, subprocess.TimeoutExpired) as error:
                if isinstance(error, InternetError) and error.code in ('internet_disconnected', 'internet_address_conflict'):
                    raise
            time.sleep(.2)
        raise InternetError('internet_direct_unavailable' if self.room.mode == 'direct' else 'internet_connect_timeout') from None

    def close(self):
        had_child=self.child is not None
        cleanup_error=None
        try:
            if self.job:
                self.job.close()
        finally:
            self.job = None
            if self.child is not None:
                try:
                    if self.child.poll() is None:
                        self.child.terminate()
                    self.child.wait(timeout=10)
                finally:
                    self.child = None
            if had_child:
                from windows_system import remove_adapter
                try:remove_adapter(self.root,self.device_name,self.address)
                except (OSError,ValueError,subprocess.TimeoutExpired):cleanup_error=InternetError('internet_cleanup_failed')
            if self.private is not None:
                # Only our new, random, non-reparse directory may be removed.
                directory = compatibility.plain(self.private)
                parent = compatibility.plain(self.root / 'LocalData')
                if directory.parent != parent or not directory.name.startswith('easytier-'):
                    raise InternetError('internet_cleanup_failed')
                shutil.rmtree(directory)
                self.private = None
            if cleanup_error is not None:raise cleanup_error
