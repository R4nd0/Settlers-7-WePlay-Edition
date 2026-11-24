"""Own one LAN directory service; retain it while other games remain admitted."""
import argparse
import errno
import http.client
import json
from pathlib import Path
import subprocess
import sys
import time
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compatibility
import host_runtime
from lan_control import request
from lan_child_job import ServiceJob
from lan_discovery import BeaconBindError, LanBeacon
from lobby_directory import (DISCOVERY_PORT, HTTP_PORT, GAME_SERVICE_PORT, MAX_MEMBERS, LobbyRoom,
    LobbyEndpoint, LanInterface, content_id, public_name)

STARTUP_FAILURES = {
    20: 'lan_discovery_port_busy',
    21: 'lan_discovery_socket_failed',
    22: 'lan_coordinator_start_failed',
    23: 'lan_coordinator_timeout',
    24: 'lan_coordinator_stopped',
}
BEACON_RETRY_SECONDS = 3
BEACON_BIND_FAILURES = ('lan_discovery_port_busy', 'lan_discovery_socket_failed')


class LanHubError(RuntimeError):
    def __init__(self, code, *, winsock_code=None):
        self.code = code
        self.winsock_code = winsock_code
        super().__init__(code)

    def startup_status(self):
        reason = next(status for status, label in STARTUP_FAILURES.items() if label == self.code)
        # Windows preserves a full process exit DWORD, while POSIX preserves eight bits.
        if sys.platform == 'win32' and self.code in BEACON_BIND_FAILURES and self.winsock_code:
            return (reason << 16) | self.winsock_code
        return reason

    @classmethod
    def from_startup_status(cls, status):
        reason, winsock_code = divmod(status, 1 << 16) if status >= 1 << 16 else (status, None)
        code = STARTUP_FAILURES.get(reason, 'lan_coordinator_start_failed')
        return cls(code, winsock_code=(winsock_code or None) if code in BEACON_BIND_FAILURES else None)


def reserve_beacon(room, *, port=DISCOVERY_PORT, clock=time.monotonic, sleep=time.sleep):
    deadline = clock() + BEACON_RETRY_SECONDS
    while True:
        try:
            return LanBeacon(room, port=port)
        except BeaconBindError as error:
            number = error.errno
            winsock_code = number if type(number) is int and 0 < number <= 0xffff else None
            # Winsock can report access denied when an exclusive listener owns
            # the address. Retry the bind only, never widen the socket scope.
            if number not in (errno.EADDRINUSE, 10048, 10013):
                raise LanHubError('lan_discovery_socket_failed', winsock_code=winsock_code) from error
            remaining = deadline - clock()
            if remaining <= 0:
                raise LanHubError('lan_discovery_port_busy', winsock_code=winsock_code) from error
            sleep(min(.2, remaining))
        except (OSError, ValueError) as error:
            raise LanHubError('lan_discovery_socket_failed') from error


def verify_service(root, expected):
    manifest = compatibility.load(compatibility.plain(root / 'compatibility.json'))
    if compatibility.network_id(manifest) != content_id(expected):
        raise ValueError('Der Lobby-Dateistand hat sich geaendert.')
    files = {name: digest for name, digest in manifest['files'].items()
             if name.startswith(('server/', 'launcher/'))}
    actual = compatibility.inventory(root, ('Server', 'Launcher'))
    if files != actual:
        raise ValueError('Der LAN-Dienst oder Starter wurde veraendert.')


def status_counts(status, expected):
    if status.get('content_id') != expected:
        raise ValueError('Falscher LAN-Dienst.')
    members, active = status.get('members'), status.get('active_members')
    if (type(members) is not int or type(active) is not int
            or not 1 <= members <= MAX_MEMBERS or not 0 <= active <= members
            or status.get('max_members') != MAX_MEMBERS):
        raise ValueError('Ungueltiger Lobby-Status.')
    return members, active


class HubLifetime:
    def __init__(self, *, clock=time.monotonic, grace=60, maximum=86400):
        self.clock, self.grace, self.maximum = clock, grace, maximum
        self.started = self.idle_since = clock()

    def keep(self, active_members):
        if type(active_members) is not int or not 0 <= active_members <= MAX_MEMBERS:
            raise ValueError('Ungueltige Teilnehmerzahl.')
        now = self.clock()
        if active_members:
            self.idle_since = now
        return now - self.started < self.maximum and now - self.idle_since < self.grace


def serve(root, interface, name, expected, room_id, *, discovery_port=DISCOVERY_PORT):
    root = compatibility.plain(root)
    verify_service(root, expected)
    room = LobbyRoom(room_id, public_name(name), content_id(expected),
                     LobbyEndpoint('lan-direct', interface.address), interface)
    data = compatibility.plain(root / 'LocalData'); data.mkdir(exist_ok=True)
    run_id = time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8]
    service = beacon = job = None
    try:
        # This socket serializes local coordinators on the same discovery port.
        # Keep it reserved until the service has released its other ports.
        beacon = reserve_beacon(room, port=discovery_port)
        job = ServiceJob()
        with (data / (run_id + '-lan-server.jsonl')).open('xb') as output, \
                (data / (run_id + '-lan-errors.txt')).open('xb') as error:
            service = job.start([*host_runtime.server_command(root),
                '--local-config-server', '--local-catalogue-probe', '--lan-lobby',
                '--lan-address', interface.address, '--content-id', expected,
                '--http-port', str(HTTP_PORT), '--udp-port', str(GAME_SERVICE_PORT), '--seconds', '86400'],
                stdout=output, stderr=error, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            deadline = time.monotonic() + 20
            while True:
                if service.poll() is not None:
                    raise LanHubError('lan_coordinator_start_failed')
                try:
                    status_counts(request(interface.address, interface.address, 'status', expected=expected), expected)
                    break
                except (OSError, ValueError, http.client.HTTPException):
                    if time.monotonic() >= deadline:
                        raise LanHubError('lan_coordinator_timeout')
                    time.sleep(.1)
            beacon.start()
            lifetime = HubLifetime()
            while (exit_code := service.poll()) is None:
                try:
                    status = request(interface.address, interface.address, 'status', expected=expected)
                    members, active = status_counts(status, expected)
                except (OSError, ValueError, http.client.HTTPException):
                    # A failed status read never prolongs the coordinator.
                    members, active = 1, 0
                beacon.update_members(members)
                if not lifetime.keep(active):
                    break
                time.sleep(1)
            if exit_code not in (None, 0):
                raise LanHubError('lan_coordinator_stopped')
    finally:
        try:
            if beacon is not None:
                beacon.stop_serving()
            try:
                # Also closes automatically if this Python hub is terminated.
                if job is not None:
                    job.close()
            finally:
                if service is not None and service.poll() is None:
                    service.terminate()
                    try:
                        service.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        service.kill(); service.wait(timeout=5)
        finally:
            if beacon is not None:
                beacon.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', required=True)
    parser.add_argument('--address', required=True)
    parser.add_argument('--prefix', type=int, required=True)
    parser.add_argument('--name', required=True)
    parser.add_argument('--content-id', required=True)
    parser.add_argument('--room-id', required=True)
    parser.add_argument('--discovery-port', type=int, default=DISCOVERY_PORT)
    args = parser.parse_args()
    try:
        serve(Path(args.bundle), LanInterface(args.address, args.prefix), args.name, args.content_id,
              args.room_id, discovery_port=args.discovery_port)
        return 0
    except LanHubError as error:
        print(error.code, flush=True)
        return error.startup_status()
    except (OSError, ValueError, RuntimeError, TimeoutError, http.client.HTTPException):
        print('LAN-Lobby beendet oder nicht erreichbar.', flush=True)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
