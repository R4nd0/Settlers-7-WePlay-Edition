"""Automatic LAN coordination; match selection belongs to the game only.

Discovery remains a source-checked private-LAN provider. Neither a beacon nor
this election admits peers or bypasses the full content check. Stable ordering
settles simultaneous idle starts; an already active coordinator takes priority.
Partitioned networks cannot be merged safely during a running match.
"""
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, replace
import http.client
import ipaddress

from lan_control import request
from lan_hub import status_counts
from lobby_directory import LobbyRoom, MAX_MEMBERS, MAX_ROOMS, content_id


class LanConnectionError(ValueError):
    """Fixed category for localized launchers; no addresses in the UI."""
    def __init__(self, code, message):
        self.code = code
        super().__init__(message)


@dataclass(frozen=True)
class Candidate:
    room: LobbyRoom
    members: int
    active: int


def checkpoint(cancel_check=None, progress=None, phase='lan_search'):
    if cancel_check is not None:
        cancel_check()
    if progress is not None:
        progress(phase)


def order(room):
    # Names and local adapter ordering differ between computers. They cannot
    # choose the winner of a simultaneous start on the same LAN.
    return (int(ipaddress.IPv4Address(room.endpoint.host)), room.room_id,
            not room.local_interface.default_route, room.local_interface.address)


def identity(room):
    # A restarted beacon may leave two UUIDs cached for one live endpoint.
    # There is one fixed control service at that address, not two groups.
    return room.endpoint.host, room.local_interface.address, room.content_id


def inspect(room):
    try:
        status = request(room.endpoint.host, room.local_interface.address,
                         'status', timeout=.65)
        # Classify the live service, not a possibly cached previous generation.
        # Otherwise an active different build could disappear behind a stale
        # same-build beacon and accidentally encourage a separate group.
        actual = content_id(status.get('content_id'))
        members, active = status_counts(status, actual)
        return Candidate(replace(room, content_id=actual), members, active)
    except (OSError, ValueError, http.client.HTTPException):
        # A cached beacon from a stopped service is not a usable coordinator.
        return None


def discover_preferred(directory, expected, acceptable_content_ids=(), *,
                       cancel_check=None, progress=None, timeout=1):
    """Return a live coordinator, None on an empty LAN, or a fixed error.

Known renderer alternatives may be passed by the desktop *before* aligning its
files. Admission still requires the final exact content ID. Up to32 source-
validated replies are checked with four bounded workers; no subnet scanning,
arbitrary endpoint URLs, account data or native game calls are involved.
"""
    allowed = {content_id(expected), *(content_id(x) for x in acceptable_content_ids)}
    checkpoint(cancel_check, progress)
    rooms = directory.list_rooms(timeout=timeout)
    if len(rooms) > MAX_ROOMS or any(not isinstance(room, LobbyRoom) for room in rooms):
        raise ValueError('Ungueltige LAN-Suchergebnisse.')
    # Only distinct source-bound endpoints need a status request. A stale room
    # generation at that address can fail its content guard independently.
    unique = {}
    for room in rooms:
        key = identity(room)
        if key not in unique or (room.expires_at, room.room_id) > (unique[key].expires_at, unique[key].room_id):
            unique[key] = room
    rooms = sorted(unique.values(), key=order)
    if not rooms:
        return None
    candidates = []
    with ThreadPoolExecutor(max_workers=4, thread_name_prefix='weplay-lan-status') as pool:
        pending = [pool.submit(inspect, room) for room in rooms]
        try:
            for future in pending:
                checkpoint(cancel_check)
                item = future.result()
                if item is not None:
                    candidates.append(item)
        finally:
            for future in pending:
                future.cancel()
    checkpoint(cancel_check)
    if not candidates:
        # A beacon with an unreachable control service often means a missing
        # private firewall permission. Creating another hub would hide friends.
        raise LanConnectionError('lan_unreachable',
            'WePlay wurde gefunden, ist aber nicht erreichbar. Private Netzwerkfreigabe pruefen und erneut versuchen.')
    compatible = [item for item in candidates if item.room.content_id in allowed]
    if not compatible:
        raise LanConnectionError('lan_version',
            'Im LAN laeuft eine andere WePlay-Version. Bitte auf allen PCs dieselbe Version installieren.')
    all_active = [item for item in candidates if item.active]
    active = [item for item in compatible if item.active]
    if all_active and not active:
        raise LanConnectionError('lan_version',
            'Die aktive LAN-Gruppe verwendet eine andere WePlay-Version. Bitte alle PCs aktualisieren.')
    # Multiple established groups indicate a partition/startup race. Do not
    # silently strand a new player in one of two disconnected session lists.
    active_hosts = {item.room.endpoint.host for item in all_active}
    if len(active_hosts) > 1:
        raise LanConnectionError('lan_multiple',
            'Mehrere getrennte WePlay-Gruppen sind aktiv. Nach den Partien WePlay auf allen PCs neu starten.')
    # All starters must elect from the same idle view, even across renderer
    # variants. Filtering idle hosts to each caller's own ID would let two
    # simultaneous first starts elect themselves into disconnected groups.
    preferred = min(active or candidates, key=lambda item: order(item.room))
    if preferred.room.content_id not in allowed:
        raise LanConnectionError('lan_version',
            'Die ausgewaehlte LAN-Gruppe verwendet einen anderen Spiel- oder Grafikstand. Bitte WePlay erneut starten.')
    if preferred.members >= MAX_MEMBERS and preferred.room.endpoint.host != preferred.room.local_interface.address:
        raise LanConnectionError('lan_full', 'Die LAN-Gruppe ist voll. Bitte warten, bis ein Platz frei wird.')
    return preferred.room


def connect(directory, expected, start_local, *, cancel_check=None, progress=None):
    """Find/start once, then require a stable view before admitting a game.

The extra searches are deliberate: clients starting together must observe each
other's idle hubs before admission makes a coordinator sticky. Hubs that lose
the election expire on their existing idle lifetime, never by killing a peer's
service. Four passes bound changing views and retain cancellation checkpoints.
"""
    def discover():
        return discover_preferred(directory, expected, cancel_check=cancel_check, progress=progress)
    selected = discover()
    if selected is None:
        checkpoint(cancel_check, progress, 'lan_connect')
        try:
            start_local()
        except (OSError, RuntimeError, TimeoutError):
            # Another copy can win the fixed local ports during startup.
            # Reuse it only after the same live/content guards pass.
            selected = discover()
            if selected is None:
                raise
    previous = None
    for _ in range(4):
        selected = discover()
        if selected is None:
            raise LanConnectionError('lan_unreachable', 'Die LAN-Verbindung wurde unterbrochen. Bitte erneut versuchen.')
        current = identity(selected)
        if previous == current:
            checkpoint(cancel_check, progress, 'lan_connect')
            return selected
        previous = current
    raise LanConnectionError('lan_changing',
        'Das LAN wird gerade eingerichtet. Bitte kurz warten und erneut starten.')
