"""Find or offer a LAN directory without asking players for IP addresses."""
import argparse
from contextlib import contextmanager
import http.client
import msvcrt
from pathlib import Path
import subprocess
import sys
import time
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compatibility
import launch_local
import launch_lan
from lan_control import Membership
from lan_discovery import LanDiscovery
from lan_interfaces import enumerate_interfaces
from lobby_directory import GAME_SERVICE_PORT
from lobby_picker import choose


@contextmanager
def session_lock(root):
    data = compatibility.plain(root / 'LocalData'); data.mkdir(exist_ok=True)
    path = compatibility.plain(data / 'session.lock')
    with path.open('a+b') as lock:
        if lock.tell() == 0:
            lock.write(b'0'); lock.flush()
        lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError:
            raise RuntimeError('Diese Spielkopie laeuft bereits.') from None
        try:
            yield data
        finally:
            lock.seek(0); msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)


def start_hub(root, interface, expected, name, directory):
    room_id = uuid.uuid4().hex
    log_path = root / 'LocalData' / ('lobby-hub-' + room_id + '.txt')
    with log_path.open('xb') as log:
        process = subprocess.Popen([str(root / 'Runtime/python/python.exe'), '-B',
            str(root / 'Launcher/lan_hub.py'), '--bundle', str(root),
            '--address', interface.address, '--prefix', str(interface.prefix_length),
            '--name', name, '--content-id', expected, '--room-id', room_id],
            stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError('LAN-Lobby konnte nicht starten. Eine vorhandene Lobby bitte aus der Liste oeffnen.')
            rooms = directory.list_rooms(timeout=.3)
            if any(room.room_id == room_id and room.endpoint.host == interface.address for room in rooms):
                return process
        raise TimeoutError('LAN-Lobby nicht rechtzeitig bereit. Private Firewallfreigabe pruefen.')
    except BaseException:
        if process.poll() is None:
            process.terminate(); process.wait(timeout=10)
        raise


def run(args):
    root = compatibility.plain(args.bundle)
    if (root / 'INSTALLATION-INCOMPLETE.txt').exists():
        raise ValueError('Die Installation ist noch nicht abgeschlossen.')
    print('Pruefe Spiel und Patch. Auf langsamen Laufwerken kann das einige Minuten dauern ...', flush=True)
    expected = compatibility.verify(root)['content_id']
    launch_local.verify_bundle(root)
    interfaces = enumerate_interfaces()
    if args.check_only:
        print('Dateistand und aktive LAN-Verbindung geprueft. Kein Spiel gestartet.', flush=True)
        return 0
    with session_lock(root) as data:
        directory = LanDiscovery(interfaces)
        choice = choose(root, directory, interfaces, expected)
        if choice is None:
            return 0
        environment = launch_local.choose_graphics_environment(root)
        if environment is None:
            return 0
        game_arguments = launch_local.graphics_game_arguments(root)
        interface = interfaces[choice['network']]
        coordinator = interface.address if choice['action'] == 'create' else choice['endpoint'].host
        # The coordinator has its own lifetime; a player leaving a match must
        # not terminate another player's directory or invalidate their leases.
        if choice['action'] == 'create':
            own_rooms = [room for room in directory.list_rooms(timeout=.3)
                         if room.endpoint.host == interface.address]
            if own_rooms:
                if len(own_rooms) != 1 or not own_rooms[0].compatible(expected):
                    raise ValueError('Die vorhandene eigene LAN-Lobby verwendet einen anderen Patchstand.')
            else:
                start_hub(root, interface, expected, 'LAN von ' + choice['display_name'], directory)
        membership = Membership(root, interface.address, coordinator, expected)
        runtime = None
        try:
            membership.start()
            configuration = launch_lan.remote_configuration(coordinator, interface.address)
            launch_lan.check_content(configuration, expected)
            runtime = launch_local.write_configuration(data, configuration, GAME_SERVICE_PORT, True,
                address=coordinator, lan_group=interface.address, display_name=choice['display_name'])
            print('LAN bereit. Im Spiel Mehrspieler oeffnen und eine Partie erstellen oder beitreten.', flush=True)
            print('Entwicklermodus ist im LAN gesperrt. Bitte das Starterfenster geoeffnet lassen.', flush=True)
            game = subprocess.Popen([str(root / 'Base/Bin/Settlers7R.exe'), *game_arguments], cwd=root / 'Base/Bin', env=environment)
            code = launch_local.wait_for_graphics_game(root, game)
            if code:
                print('Das Spiel wurde mit einem Fehler beendet.', flush=True)
            return 0 if code == 0 else 1
        finally:
            try:
                membership.close()
            finally:
                if runtime is not None and runtime.exists():
                    compatibility.plain(runtime).unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', required=True)
    parser.add_argument('--check-only', action='store_true')
    args = parser.parse_args()
    try:
        return run(args)
    except (OSError, ValueError, RuntimeError, TimeoutError, http.client.HTTPException):
        print('LAN-Start abgebrochen. Netzwerk, passenden Patchstand und private Firewallfreigabe pruefen.', flush=True)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
