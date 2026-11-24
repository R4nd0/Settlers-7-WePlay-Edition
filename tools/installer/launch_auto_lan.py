"""Find or offer a LAN directory without asking players for IP addresses."""
import argparse
from contextlib import contextmanager, ExitStack
import http.client
import portable_lock as msvcrt
from pathlib import Path
import subprocess
import sys
import time
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compatibility
import launch_local
import launch_lan
from lan_control import Membership, request
from lan_discovery import LanDiscovery
from lan_hub import LanHubError, status_counts
from lan_interfaces import enumerate_interfaces
from lobby_directory import DISCOVERY_PORT, GAME_SERVICE_PORT
import lan_autoconnect
import player_settings
import host_runtime


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


def stop_hub(process):
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)

def stop_idle_hub(process, interface, expected):
    if process.poll() is not None:
        return
    try:
        status = request(interface.address, interface.address, 'status', expected=expected, timeout=.5)
        _, active = status_counts(status, expected)
        if not active:
            stop_hub(process)
    except (OSError, ValueError, http.client.HTTPException, subprocess.TimeoutExpired):
        pass  # An unconfirmed room keeps its normal idle lease.


def start_hub(root, interface, expected, name, directory, *, cancel_check=None, discovery_port=DISCOVERY_PORT):
    room_id = uuid.uuid4().hex
    log_path = root / 'LocalData' / ('lobby-hub-' + room_id + '.txt')
    # The coordinator belongs to all admitted peers. WSL tears down the
    # creator's process session when its CLI exits; keep this normal-user hub
    # independent. Its own lease/grace timer still closes the native service.
    lifetime = dict(start_new_session=True, stdin=subprocess.DEVNULL) if sys.platform == 'linux' else {}
    port_option = ['--discovery-port', str(discovery_port)] if discovery_port != DISCOVERY_PORT else []
    with log_path.open('xb') as log:
        process = subprocess.Popen([host_runtime.python_command(root), '-B',
            str(root / 'Launcher/lan_hub.py'), '--bundle', str(root),
            '--address', interface.address, '--prefix', str(interface.prefix_length),
            '--name', name, '--content-id', expected, '--room-id', room_id, *port_option],
            stdout=log, stderr=log, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0), **lifetime)
    try:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            lan_autoconnect.checkpoint(cancel_check)
            status = process.poll()
            if status is not None:
                raise LanHubError.from_startup_status(status)
            rooms = directory.list_rooms(timeout=.3)
            if any(room.room_id == room_id and room.endpoint.host == interface.address for room in rooms):
                return process
        raise LanHubError('lan_discovery_timeout')
    except BaseException as primary:
        try:
            stop_hub(process)
        except Exception as cleanup:
            raise primary from cleanup
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
    with session_lock(root) as data, ExitStack() as abort_host:
        # Profile installation and other launchers use this same copy lock.
        # Verify once while owning it, so a profile/content change cannot land
        # between the full-file check and admission/startup.
        expected = compatibility.network_id(compatibility.verify(root))
        launch_local.verify_bundle(root)
        lan_autoconnect.checkpoint(cancel)
        discovery_port = DISCOVERY_PORT
        if network is None:
            discovery_port = getattr(args, 'discovery_port', DISCOVERY_PORT)
            interfaces = enumerate_interfaces()
            directory = LanDiscovery(interfaces, port=discovery_port)
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
        renderer=getattr(args,'renderer',None)
        renderer_option={'renderer':renderer} if renderer is not None else {}
        if sys.platform == 'linux':
            import linux_runtime
            environment = linux_runtime.graphics_environment(root)
        else:
            environment = (launch_local.saved_graphics_environment(root, **renderer_option) if getattr(args, 'saved_graphics', False)
                       else launch_local.choose_graphics_environment(root))
        if environment is None:
            return 0
        environment = launch_local.game_launch_environment(root, environment)
        game_arguments = ('-vSynch', '-noMapInMainMenu') if sys.platform == 'linux' else launch_local.graphics_game_arguments(root, **renderer_option)
        player_settings.save(root, name)

        def start_owned(target):
            hub = start_hub(root, target, expected, 'WePlay LAN', directory,
                            cancel_check=cancel, discovery_port=discovery_port)
            abort_host.callback(stop_idle_hub, hub, target, expected)
            return hub

        def start_automatic():
            for target in interfaces[:-1]:
                try:
                    return start_owned(target)
                except LanHubError as error:
                    if error.code != 'lan_discovery_socket_failed':
                        raise
            return start_owned(interfaces[-1])

        if choice is None:
            room = lan_autoconnect.connect(directory, expected, start_automatic,
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
                start_owned(interface)
        membership = Membership(root, interface.address, coordinator, expected)
        runtime = None
        observer = None

        def close_launch():
            try:
                try:
                    if observer is not None:observer.close()
                finally:membership.close()
            finally:
                if runtime is not None and runtime.exists():
                    compatibility.plain(runtime).unlink()

        try:
            lan_autoconnect.checkpoint(cancel, progress, 'lan_connect')
            membership.start(cancel_check=cancel)
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
            game = launch_local.spawn_game(root,game_arguments,environment,
                lan={'local':interface.address,'coordinator':coordinator,'group':True},**renderer_option)
            code = launch_local.wait_for_graphics_game(root, game, enabled=sys.platform != 'linux',
                monitor=getattr(args, 'game_monitor', None),startup=getattr(args, 'game_startup', None),
                during_game=getattr(args, 'during_game', None))
            if code:
                print('Das Spiel wurde mit einem Fehler beendet.', flush=True)
            else:
                abort_host.pop_all()
        except BaseException as primary:
            try:
                close_launch()
            except Exception as cleanup:
                raise primary from cleanup
            raise
        close_launch()
        return 0 if code == 0 else 1


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
