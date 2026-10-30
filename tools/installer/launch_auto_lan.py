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
import lan_autoconnect
import player_settings


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


def start_hub(root, interface, expected, name, directory, *, cancel_check=None):
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
            lan_autoconnect.checkpoint(cancel_check)
            if process.poll() is not None:
                raise RuntimeError('Der LAN-Dienst konnte nicht starten. Bitte erneut versuchen.')
            rooms = directory.list_rooms(timeout=.3)
            if any(room.room_id == room_id and room.endpoint.host == interface.address for room in rooms):
                return process
        raise TimeoutError('LAN-Lobby nicht rechtzeitig bereit. Private Firewallfreigabe pruefen.')
    except BaseException:
        if process.poll() is None:
            process.terminate(); process.wait(timeout=10)
        raise


def run(args, *, picker=None, network=None):
    root = compatibility.plain(args.bundle)
    if (root / 'INSTALLATION-INCOMPLETE.txt').exists():
        raise ValueError('Die Installation ist noch nicht abgeschlossen.')
    progress, cancel = getattr(args, 'progress', None), getattr(args, 'cancel_check', None)
    lan_autoconnect.checkpoint(cancel)
    print('Pruefe Spiel und Patch. Auf langsamen Laufwerken kann das einige Minuten dauern ...', flush=True)
    if args.check_only:
        compatibility.verify(root)
        launch_local.verify_bundle(root)
        enumerate_interfaces()
        print('Dateistand und aktive LAN-Verbindung geprueft. Kein Spiel gestartet.', flush=True)
        return 0
    with session_lock(root) as data:
        # Profile installation and other launchers use this same copy lock.
        # Verify once while owning it, so a profile/content change cannot land
        # between the full-file check and admission/startup.
        expected = compatibility.verify(root)['content_id']
        launch_local.verify_bundle(root)
        lan_autoconnect.checkpoint(cancel)
        if network is None:
            interfaces = enumerate_interfaces()
            directory = LanDiscovery(interfaces)
        else:
            # An owned Internet transport supplies exactly its verified overlay.
            # Never discover or offer a coordinator on a physical default route.
            interfaces, directory = network()
        # Explicit injection remains available to diagnostics. The shipped
        # desktop/CLI path never opens a room picker before the game browser.
        choice = picker(root, directory, interfaces, expected) if picker is not None else None
        if picker is not None and choice is None:
            return 0
        name = (choice['display_name'] if choice is not None else
                player_settings.player_name(getattr(args, 'player_name', None) or player_settings.load(root)))
        environment = (launch_local.saved_graphics_environment(root) if getattr(args, 'saved_graphics', False)
                       else launch_local.choose_graphics_environment(root))
        if environment is None:
            return 0
        environment = launch_local.game_launch_environment(root, environment)
        game_arguments = launch_local.graphics_game_arguments(root)
        player_settings.save(root, name)
        if choice is None:
            interface = interfaces[0]  # Active private default route first.
            room = lan_autoconnect.connect(directory, expected,
                lambda: start_hub(root, interface, expected, 'WePlay LAN', directory, cancel_check=cancel),
                cancel_check=cancel, progress=progress)
            interface, coordinator = room.local_interface, room.endpoint.host
        else:
            interface = interfaces[choice['network']]
            coordinator = interface.address if choice['action'] == 'create' else choice['endpoint'].host
        # The coordinator has its own lifetime; a player leaving a match must
        # not terminate another player's directory or invalidate their leases.
        if choice is not None and choice['action'] == 'create':
            own_rooms = [room for room in directory.list_rooms(timeout=.3)
                         if room.endpoint.host == interface.address]
            if own_rooms:
                if len(own_rooms) != 1 or not own_rooms[0].compatible(expected):
                    raise ValueError('Die vorhandene eigene LAN-Lobby verwendet einen anderen Patchstand.')
            else:
                start_hub(root, interface, expected, 'WePlay LAN', directory, cancel_check=cancel)
        membership = Membership(root, interface.address, coordinator, expected)
        runtime = None
        observer = None
        try:
            lan_autoconnect.checkpoint(cancel, progress, 'lan_connect')
            membership.start()
            lan_autoconnect.checkpoint(cancel)
            configuration = launch_lan.remote_configuration(coordinator, interface.address)
            launch_lan.check_content(configuration, expected)
            if getattr(args,'public_observer',None) is not None:
                observer=args.public_observer(interface,coordinator,expected)
            runtime = launch_local.write_configuration(data, configuration, GAME_SERVICE_PORT, True,
                address=coordinator, lan_group=interface.address, display_name=name)
            lan_autoconnect.checkpoint(cancel, progress, 'lan_ready')
            print('LAN bereit. Im Spiel Mehrspieler oeffnen und eine Partie erstellen oder beitreten.', flush=True)
            print('Entwicklermodus ist im LAN gesperrt. Bitte das Starterfenster geoeffnet lassen.', flush=True)
            if getattr(args, 'before_game', None) is not None:
                args.before_game()
            game = subprocess.Popen([str(root / 'Base/Bin/Settlers7R.exe'), *game_arguments], cwd=root / 'Base/Bin', env=environment)
            code = launch_local.wait_for_graphics_game(root, game, monitor=getattr(args, 'game_monitor', None))
            if code:
                print('Das Spiel wurde mit einem Fehler beendet.', flush=True)
            return 0 if code == 0 else 1
        finally:
            try:
                try:
                    if observer is not None:observer.close()
                finally:membership.close()
            finally:
                if runtime is not None and runtime.exists():
                    compatibility.plain(runtime).unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', required=True)
    parser.add_argument('--check-only', action='store_true')
    parser.add_argument('--player-name', help='Lokaler Anzeigename (optional, sonst gespeicherter Name).')
    args = parser.parse_args()
    try:
        return run(args)
    except (OSError, ValueError, RuntimeError, TimeoutError, http.client.HTTPException):
        print('LAN-Start abgebrochen. Netzwerk, passenden Patchstand und private Firewallfreigabe pruefen.', flush=True)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
