"""Bounded LAN admission and expiring native peer files, no game-memory access."""
import ctypes
import http.client
import math
import os
from pathlib import Path
import tempfile
import threading
import time
import socket

import compatibility
from lobby_directory import (CONTROL_FORMAT, HTTP_PORT, LEASE_SECONDS, MAX_MEMBERS,
    content_id, encode, json_record, private_address)


def request(host, local, path, *, expected=None, timeout=3):
    host, local = private_address(host), private_address(local)
    if type(timeout) not in (int, float) or not math.isfinite(timeout) or not 0 < timeout <= 10:
        raise ValueError('Ungueltige Lobby-Anfragedauer.')
    if path not in ('status', 'games', 'admit', 'peers', 'leave'):
        raise ValueError('Unbekannte Lobby-Anfrage.')
    body = None if path in ('status', 'games', 'peers') else encode({
        'format': CONTROL_FORMAT, 'content_id': content_id(expected)})
    connection = http.client.HTTPConnection(host, HTTP_PORT, timeout=timeout, source_address=(local, 0))
    deadline = time.monotonic() + timeout
    expired = threading.Event()
    timer = None
    response = None
    try:
        # Numeric direct connection already has a bounded connect timeout. The
        # timer then bounds the entire transaction, including slow-drip reads.
        connection.connect()
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError('LAN-Anfrage hat zu lange gedauert.')
        owned_socket = connection.sock
        def interrupt():
            expired.set()
            try:
                owned_socket.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        timer = threading.Timer(remaining, interrupt)
        timer.daemon = True
        timer.start()
        connection.request('GET' if body is None else 'POST', '/public/lobby/v1/' + path,
            body=body, headers={} if body is None else {'Content-Type': 'application/json'})
        response = connection.getresponse()
        data = response.read(4097)
        if expired.is_set() or time.monotonic() >= deadline:
            raise TimeoutError('LAN-Anfrage hat zu lange gedauert.')
        if response.status != 200 or len(data) > 4096:
            raise ValueError('LAN-Lobby nicht erreichbar, voll oder mit anderem Dateistand.')
        if path == 'leave':
            if data:
                raise ValueError('Unerwartete Abmeldeantwort.')
            return None
        value = json_record(data, 4096)
        if value.get('format') != ('s7-lobby-games-v1' if path == 'games' else CONTROL_FORMAT) or (expected is not None and value.get('content_id') != content_id(expected)):
            raise ValueError('Spiel- oder Patchstand der LAN-Lobby stimmt nicht ueberein.')
        return value
    finally:
        if timer is not None:
            timer.cancel()
            if timer.ident is not None:
                timer.join()
        try:
            if response is not None:
                response.close()
        finally:
            connection.close()


def validate_peers(value, local, coordinator, expected):
    local, coordinator = private_address(local), private_address(coordinator)
    if (not isinstance(value, dict) or set(value) != {'format', 'content_id', 'revision', 'lease_seconds', 'peers'}
            or value.get('format') != CONTROL_FORMAT or value.get('content_id') != content_id(expected)
            or type(value.get('revision')) is not int or not 1 <= value['revision'] <= 0xffffffff
            or type(value.get('lease_seconds')) is not int or value['lease_seconds'] != LEASE_SECONDS):
        raise ValueError('Unbekannte Teilnehmerantwort.')
    peers = value.get('peers')
    if not isinstance(peers, list) or not 1 <= len(peers) <= MAX_MEMBERS:
        raise ValueError('Ungueltige Teilnehmerzahl.')
    peers = tuple(private_address(peer) for peer in peers)
    if len(set(peers)) != len(peers) or local not in peers or coordinator not in peers:
        raise ValueError('Die Teilnehmerantwort gehoert nicht zu diesem Spielstart.')
    return tuple(sorted(peer for peer in peers if peer != local))


def tick_ms():
    # Same boot-relative clock as the native GetTickCount64 validator.
    function = ctypes.WinDLL('kernel32', use_last_error=True).GetTickCount64
    function.argtypes, function.restype = [], ctypes.c_ulonglong
    return int(function())


def write_peers(root, local, peers, *, clock=tick_ms):
    root = compatibility.plain(root)
    local = private_address(local)
    if not isinstance(peers, tuple) or len(peers) > MAX_MEMBERS - 1:
        raise ValueError('Ungueltige Peer-Liste.')
    if len(set(peers)) != len(peers) or local in peers:
        raise ValueError('Mehrdeutige Peer-Liste.')
    for peer in peers:
        private_address(peer)
    destination = compatibility.plain(root / 'LocalData/lan-peers.ini')
    destination.parent.mkdir(exist_ok=True)
    if destination.exists():
        with destination.open('rb') as source:
            previous = source.read(4097)
        if len(previous) > 4096 or not previous.startswith(b'format=s7-lan-peers-v1\n'):
            raise ValueError('Unbekannte Teilnehmerdatei wird nicht ersetzt.')
    content = f'format=s7-lan-peers-v1\nlocal={local}\nexpires_tick_ms={clock() + LEASE_SECONDS * 1000}\n'
    content += ''.join(f'peer_{index}={peer}\n' for index, peer in enumerate(peers, 1))
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, prefix='peers-', suffix='.tmp', delete=False) as output:
            temporary = Path(output.name)
            output.write(content.encode('ascii')); output.flush(); os.fsync(output.fileno())
        os.replace(temporary, destination)
    finally:
        if temporary and temporary.exists():
            temporary.unlink()
    return destination


class Membership:
    """Single owner thread renews admission. Failed renewals never extend peers."""
    def __init__(self, root, local, coordinator, expected, *, transport=request, clock=tick_ms):
        self.root, self.local, self.coordinator = Path(root), private_address(local), private_address(coordinator)
        self.expected, self.transport, self.clock = content_id(expected), transport, clock
        self.stop_event = threading.Event()
        self.publish_lock = threading.Lock()
        self.thread = self.path = None
        self.admitted = False
        self.last_success_ms = None

    def renew(self):
        if self.stop_event.is_set():
            return
        value = self.transport(self.coordinator, self.local, 'admit', expected=self.expected)
        self.admitted = True
        peers = validate_peers(value, self.local, self.coordinator, self.expected)
        with self.publish_lock:
            if self.stop_event.is_set():
                return
            self.path = write_peers(self.root, self.local, peers, clock=self.clock)
            self.last_success_ms = self.clock()

    def start(self):
        self.renew()  # Must finish before the game can bind or send.
        self.thread = threading.Thread(target=self._run, name='s7-lan-membership', daemon=True)
        self.thread.start()
        return self

    def _run(self):
        while not self.stop_event.wait(3):
            try:
                self.renew()
            except (OSError, ValueError, http.client.HTTPException):
                # Native expiry closes dynamic routes after 15 seconds. The
                # original simulation/connection error remains authoritative.
                pass

    def close(self):
        self.stop_event.set()
        if self.thread:
            self.thread.join(timeout=5)
        try:
            if self.admitted:
                self.transport(self.coordinator, self.local, 'leave', expected=self.expected)
        except (OSError, ValueError, http.client.HTTPException):
            pass
        finally:
            with self.publish_lock:
                if self.path and self.path.exists():
                    compatibility.plain(self.path).unlink()
        if self.thread and self.thread.is_alive():
            raise RuntimeError('Die LAN-Abmeldung konnte nicht abgeschlossen werden.')
