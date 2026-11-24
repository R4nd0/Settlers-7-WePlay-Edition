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
from lan_hub import status_counts
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
    if type(getattr(args,'internet_consent',None)) is not int or args.internet_consent != 1:
        raise InternetError('internet_consent_required')
    root = compatibility.plain(args.bundle)
    name = player_settings.player_name(args.player_name)
    invitation = getattr(args, 'invitation', None)
    host = invitation is None
    physical = enumerate_interfaces(allow_empty=True, include_all=True)
    room = (create(physical, mode=getattr(args, 'mode', 'assisted'),
                   direct_endpoint=getattr(args, 'direct_endpoint', None)) if host else parse(invitation))
    if policy_ready:policy_ready(room.mode)
    check_overlap(room, physical)
    cancel, progress = getattr(args, 'cancel_check', None), getattr(args, 'progress', None)
    overlay = overlay_factory(root, room, host=host, cancel_check=cancel, progress=progress)
    interface = None
    expected_content = None
    host_ready = False

    def network():
        nonlocal interface
        if interface is None:
            interface = overlay.start()
            if network_ready:
                network_ready(interface, room.coordinator, host)
        return [interface], LanDiscovery([interface])

    def select(_root, _directory, interfaces, expected):
        nonlocal expected_content
        if len(interfaces) != 1 or interfaces[0] != interface:
            raise InternetError('internet_adapter_invalid')
        expected_content = expected
        return dict(action='create' if host else 'join', network=0, display_name=name,
                    endpoint=LobbyEndpoint('lan-direct', room.coordinator))

    before = getattr(args, 'before_game', None)

    def verify_tunnel_binding():
        selected, peers = overlay.snapshot()
        selected_binding = (selected.address, selected.prefix_length, selected.name)
        expected_binding = (interface.address, interface.prefix_length, interface.name)
        if selected_binding != expected_binding or (not host and room.coordinator not in peers):
            raise InternetError('internet_disconnected')

    def before_game():
        nonlocal host_ready
        verify_tunnel_binding()
        if before:
            before()
        # launch_auto_lan has now reached the real coordinator, joined its
        # membership and validated its content/configuration. An overlay alone
        # must not expose an invitation or report a usable room.
        if progress:
            progress('internet_ready')
        if host and invitation_ready:
            invitation_ready(room.token())
        host_ready = host

    options = dict(vars(args), bundle=root, check_only=False, player_name=name, before_game=before_game)
    observer = getattr(args, 'public_observer', None)
    if observer is not None:
        def public_observer(interface, coordinator, expected):
            # launch_auto_lan calls this only after coordinator membership and
            # content validation. The host broker needs its room identity now;
            # the external invitation_ready callback stays gated by before_game.
            return observer(interface, coordinator, expected,
                            room_invitation=room.token() if host else None)
        options['public_observer'] = public_observer
    try:
        # Renderer alignment can change the content manifest. Do it before the
        # launcher takes its lock and computes the final full content ID.
        if network_ready:
            network()
        code = launch_auto_lan.run(SimpleNamespace(**options), picker=select, network=network)
        if host_ready:
            deadline = time.monotonic() + 86400
            missed_status = 0
            while time.monotonic() < deadline:
                if getattr(args,'switch_requested',None) is not None and args.switch_requested():
                    break
                if cancel:
                    cancel()
                try:
                    status = request(room.coordinator, interface.address, 'status',
                                     expected=expected_content, timeout=2)
                    _, active = status_counts(status, expected_content)
                except ValueError:
                    raise InternetError('internet_status_invalid') from None
                except (OSError, http.client.HTTPException):
                    if code:
                        # A failed game's newly created idle hub may already be gone.
                        break
                    missed_status += 1
                    if missed_status >= 3:
                        raise InternetError('internet_disconnected') from None
                    time.sleep(.5)
                    continue
                missed_status = 0
                if not active:
                    break
                try:
                    verify_tunnel_binding()
                except (OSError, http.client.HTTPException):
                    raise InternetError('internet_disconnected') from None
                if progress:
                    progress('internet_hosting')
                time.sleep(.5)
    except BaseException as primary:
        try:
            overlay.close()
        except Exception as cleanup:
            if not hasattr(primary, 'add_note'):
                raise primary from cleanup  # Python 3.10 on Linux
            primary.add_note('internet_cleanup_failed')
        raise
    else:
        overlay.close()
    return code


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
        print('Andere Spieler koennen deine Verbindungsadresse sehen. Nur Spiel-/Lobbyports sind freigegeben; Fehler im Spiel/VPN und Ueberlastung bleiben moeglich.',flush=True)
        if input('EasyTier fuer diese Verbindung ausdruecklich zulassen? [ja/Nein] ').strip().lower() != 'ja':
            return 0
        args.internet_consent=1
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
