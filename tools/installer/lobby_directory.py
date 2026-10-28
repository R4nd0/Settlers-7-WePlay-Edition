"""Versioned lobby directory boundary; LAN transport is one implementation.

Room discovery is not admission or simulation synchronization. A future remote
directory can implement the same list/resolve interface with another endpoint
kind without teaching the native game how to browse HTTP services.
"""
from dataclasses import dataclass
import ipaddress
import json
import math
import re
import time
from typing import Protocol
import unicodedata

DIRECTORY_FORMAT = 's7-lobby-directory-v1'
DISCOVERY_FORMAT = 's7-lan-discovery-v1'
CONTROL_FORMAT = 's7-lobby-control-v1'
DISCOVERY_PORT, HTTP_PORT, GAME_SERVICE_PORT = 9112, 9110, 9111
MAX_MEMBERS, MAX_ROOMS, MAX_DATAGRAM = 8, 32, 1024
LEASE_SECONDS, BEACON_TTL = 15, 9
PRIVATE_NETWORKS = tuple(ipaddress.IPv4Network(value) for value in
                         ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16'))


def private_address(value):
    if not isinstance(value, str):
        raise ValueError('Ungueltige LAN-Adresse.')
    address = ipaddress.IPv4Address(value)
    if str(address) != value or address.packed[-1] in (0, 255) or not any(address in n for n in PRIVATE_NETWORKS):
        raise ValueError('Eine genaue private IPv4-Adresse wird benoetigt.')
    return value


def content_id(value):
    if not isinstance(value, str) or not re.fullmatch('[0-9a-f]{64}', value):
        raise ValueError('Unbekannter Spiel- oder Patchstand.')
    return value


def public_name(value, limit=64):
    if (not isinstance(value, str) or not value.strip() or value != value.strip()
            or len(value.encode('utf-8')) > limit
            or any(unicodedata.category(c).startswith('C') or c in '\u2028\u2029' for c in value)):
        raise ValueError('Der Name enthaelt ungueltige Zeichen oder ist zu lang.')
    return value


def json_record(data, limit=MAX_DATAGRAM):
    if not isinstance(data, bytes) or not 0 < len(data) <= limit:
        raise ValueError('Ungueltige Lobby-Nachrichtengroesse.')
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('Doppelte Felder in der Lobby-Nachricht.')
            result[key] = value
        return result
    try:
        value = json.loads(data.decode('utf-8', errors='strict'), object_pairs_hook=unique,
                           parse_constant=lambda _: (_ for _ in ()).throw(ValueError('Nichtendliche Zahl.')))
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise ValueError('Ungueltige Lobby-Nachricht.') from error
    if not isinstance(value, dict):
        raise ValueError('Unbekannte Lobby-Nachricht.')
    return value


def encode(value):
    return json.dumps(value, separators=(',', ':'), ensure_ascii=True, allow_nan=False).encode('ascii')


@dataclass(frozen=True)
class LanInterface:
    address: str
    prefix_length: int
    name: str = 'Lokales Netzwerk'
    default_route: bool = False

    def __post_init__(self):
        private_address(self.address)
        if type(self.prefix_length) is not int or not 8 <= self.prefix_length <= 30:
            raise ValueError('Nicht unterstuetztes LAN-Netz.')
        public_name(self.name, 128)
        if type(self.default_route) is not bool or ipaddress.IPv4Address(self.address) in (
                self.network.network_address, self.network.broadcast_address):
            raise ValueError('Ungueltige Netzwerkschnittstelle.')

    @property
    def network(self):
        return ipaddress.IPv4Network(f'{self.address}/{self.prefix_length}', strict=False)

    def contains(self, address):
        try:
            return ipaddress.IPv4Address(private_address(address)) in self.network
        except (ValueError, TypeError):
            return False


@dataclass(frozen=True)
class LobbyEndpoint:
    kind: str
    host: str
    control_port: int = HTTP_PORT
    rendezvous_port: int = GAME_SERVICE_PORT

    def __post_init__(self):
        # A community provider must add an explicit transport implementation;
        # a public advertisement cannot silently switch LAN clients to WAN.
        if self.kind != 'lan-direct':
            raise ValueError('Dieser Lobby-Transport ist noch nicht implementiert.')
        private_address(self.host)
        if type(self.control_port) is not int or type(self.rendezvous_port) is not int or (
                self.control_port, self.rendezvous_port) != (HTTP_PORT, GAME_SERVICE_PORT):
            raise ValueError('Unerwartete Lobby-Ports.')


@dataclass(frozen=True)
class LobbyRoom:
    room_id: str
    name: str
    content_id: str
    endpoint: LobbyEndpoint
    local_interface: LanInterface
    members: int = 1
    capacity: int = MAX_MEMBERS
    expires_at: float = 0

    def __post_init__(self):
        if not isinstance(self.room_id, str) or not re.fullmatch('[0-9a-f]{32}', self.room_id):
            raise ValueError('Ungueltige Lobbykennung.')
        public_name(self.name)
        content_id(self.content_id)
        if type(self.members) is not int or type(self.capacity) is not int or not 1 <= self.members <= self.capacity <= MAX_MEMBERS:
            raise ValueError('Ungueltige Teilnehmerzahl.')
        if not self.local_interface.contains(self.endpoint.host):
            raise ValueError('Die Lobby liegt ausserhalb des gewaehlten Netzwerks.')
        if type(self.expires_at) not in (int, float) or not math.isfinite(self.expires_at):
            raise ValueError('Ungueltige Lobby-Geltungsdauer.')

    def compatible(self, expected):
        return self.content_id == content_id(expected)


class LobbyDirectory(Protocol):
    """The launcher depends on this contract, not broadcast or a future server."""
    def list_rooms(self, *, timeout: float = 2) -> list[LobbyRoom]: ...
    def resolve(self, room_id: str) -> LobbyRoom: ...


class RoomCache:
    def __init__(self, clock=time.monotonic):
        self.clock = clock
        self.rooms = {}

    def add(self, room):
        self.prune()
        if room.expires_at <= self.clock():
            return
        key = (room.room_id, room.endpoint.host)
        if key not in self.rooms and len(self.rooms) >= MAX_ROOMS:
            return
        self.rooms[key] = room

    def prune(self):
        now = self.clock()
        self.rooms = {key: room for key, room in self.rooms.items() if room.expires_at > now}

    def list_rooms(self):
        self.prune()
        return sorted(self.rooms.values(), key=lambda r: (r.name.casefold(), r.room_id, r.endpoint.host))

    def resolve(self, room_id):
        matches = [room for room in self.list_rooms() if room.room_id == room_id]
        if len(matches) != 1:
            raise ValueError('Die Lobby ist nicht mehr eindeutig erreichbar. Bitte erneut suchen.')
        return matches[0]
