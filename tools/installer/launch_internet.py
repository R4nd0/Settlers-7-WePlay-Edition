"""Integrated EasyTier room launch. No separate VPN UI or hosted account."""
import argparse
import http.client
from pathlib import Path
import sys
import time
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
source_local = Path(__file__).resolve().parent.parent / 'offline-ui'
if source_local.is_dir():
    sys.path.insert(0, str(source_local))
import compatibility
from easytier_elevation import open_overlay
from internet_room import InternetError, create, parse, check_overlap
from lan_control import request
from lan_discovery import LanDiscovery
from lan_interfaces import enumerate_interfaces
from lobby_directory import LobbyEndpoint
import launch_auto_lan
import player_settings


def run(args, *, invitation_ready=None, network_ready=None, policy_ready=None, overlay_factory=open_overlay):
    """A room creator stays the coordinator; joining cannot create another hub.

    `invitation_ready` is a private UI event, not a log/progress message. Keep
    the creator's overlay alive while admitted guests are playing after its own
    game exits. Cancellation explicitly closes the room; normal game exit does
    not disconnect other players.
    """
    root = compatibility.plain(args.bundle)
    name = player_settings.player_name(args.player_name)
    invitation = getattr(args, 'invitation', None)
    host = invitation is None
    physical = enumerate_interfaces()
    room = (create(physical, mode=getattr(args, 'mode', 'assisted'),
                   direct_endpoint=getattr(args, 'direct_endpoint', None)) if host else parse(invitation))
    if policy_ready:policy_ready(room.mode)
    check_overlap(room, physical)
    cancel, progress = getattr(args, 'cancel_check', None), getattr(args, 'progress', None)
    overlay = overlay_factory(root, room, host=host, cancel_check=cancel, progress=progress)
    interface = None

    def network():
        nonlocal interface
        if interface is None:
            interface = overlay.start()
            if network_ready:
                network_ready(interface, room.coordinator, host)
            if host and invitation_ready:
                invitation_ready(room.token())
        return [interface], LanDiscovery([interface])

    def select(_root, _directory, interfaces, _expected):
        if len(interfaces) != 1 or interfaces[0] != interface:
            raise InternetError('internet_adapter_invalid')
        return dict(action='create' if host else 'join', network=0, display_name=name,
                    endpoint=LobbyEndpoint('lan-direct', room.coordinator))

    before = getattr(args, 'before_game', None)

    def before_game():
        selected, peers = overlay.snapshot()
        if selected != interface or (not host and room.coordinator not in peers):
            raise InternetError('internet_disconnected')
        if before:
            before()

    options = dict(vars(args), bundle=root, check_only=False, player_name=name, before_game=before_game)
    try:
        # Renderer alignment can change the content manifest. Do it before the
        # launcher takes its lock and computes the final full content ID.
        if network_ready:
            network()
        code = launch_auto_lan.run(SimpleNamespace(**options), picker=select, network=network)
        if host and interface is not None:
            deadline = time.monotonic() + 86400
            while time.monotonic() < deadline:
                if cancel:
                    cancel()
                try:
                    status = request(room.coordinator, interface.address, 'status', timeout=2)
                    active = status.get('active_members')
                    if type(active) is not int or not 0 <= active <= 8:
                        raise InternetError('internet_status_invalid')
                    if not active:
                        break
                    overlay.snapshot()
                except (OSError, http.client.HTTPException):
                    raise InternetError('internet_disconnected') from None
                if progress:
                    progress('internet_hosting')
                time.sleep(.5)
        return code
    finally:
        overlay.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--player-name', required=True)
    parser.add_argument('--join', action='store_true', help='Einladung verdeckt einlesen; nie als Kommandozeilenargument.')
    parser.add_argument('--direct-endpoint', help='Optionaler erreichbarer Host-Endpunkt fuer direkten Betrieb.')
    args = parser.parse_args()
    args.mode = 'direct' if args.direct_endpoint else 'assisted'
    args.saved_graphics = True
    try:
        if args.join:
            from getpass import getpass
            args.invitation = getpass('WePlay-Einladung: ')
        print('EasyTier-Internetraum. Der Ersteller muss erreichbar bleiben.', flush=True)
        print('Ohne direkten Endpunkt werden oeffentliche Vermittlungsknoten und bei Bedarf Relays verwendet.', flush=True)
        def private_file(value):
            import tempfile
            directory = Path(tempfile.mkdtemp(prefix='internet-invitation-', dir=args.bundle / 'LocalData'))
            (directory / 'invitation.txt').write_text(value, encoding='ascii')
            print('Einladung in privater Datei: ' + str(directory / 'invitation.txt'), flush=True)
        return run(args, invitation_ready=private_file)
    except KeyboardInterrupt:
        return 130
    except (OSError, ValueError, RuntimeError, TimeoutError, http.client.HTTPException) as error:
        print('Internetstart fehlgeschlagen: ' + getattr(error, 'code', 'internet_failed'), flush=True)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
