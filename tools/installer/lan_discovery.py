"""Bounded UDP LAN lookup. Announcements contain no accounts or game payloads."""
from collections import OrderedDict
from dataclasses import replace
import ctypes
import secrets
import math
import socket
import threading
import time

from lobby_directory import (BEACON_TTL, DISCOVERY_FORMAT, DISCOVERY_PORT, HTTP_PORT,
    GAME_SERVICE_PORT, MAX_DATAGRAM, MAX_MEMBERS, LobbyEndpoint, LobbyRoom, RoomCache,
    content_id, encode, json_record, private_address, public_name)


class IpAddressRow(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in ('address', 'index', 'mask', 'broadcast', 'reassembly')]
    _fields_ += [('unused', ctypes.c_uint16), ('type', ctypes.c_uint16)]


class IpAddressTable(ctypes.Structure):
    _fields_ = [('count', ctypes.c_uint32), ('rows', IpAddressRow * 1)]


def interface_index_from_table(data, address):
    wanted = socket.inet_aton(private_address(address))
    offset, stride = IpAddressTable.rows.offset, ctypes.sizeof(IpAddressRow)
    if len(data) < offset:
        raise ValueError('Netzwerkschnittstelle konnte nicht gelesen werden.')
    count = int.from_bytes(data[:4], 'little')
    if count > (len(data) - offset) // stride:
        raise ValueError('Unvollstaendige Netzwerkschnittstellenliste.')
    rows = [IpAddressRow.from_buffer_copy(data, offset + i * stride) for i in range(count)]
    matches = {row.index for row in rows if row.address.to_bytes(4, 'little') == wanted}
    if len(matches) != 1:
        raise ValueError('Das ausgewaehlte LAN ist nicht mehr eindeutig verfuegbar.')
    index = matches.pop()
    if not 0 < index <= 0xffffff or any(row.index == index and
            row.address.to_bytes(4, 'little') != wanted for row in rows):
        # UNICAST_IF chooses an interface, not one of its secondary addresses.
        raise ValueError('Bitte ein LAN mit genau einer IPv4-Adresse pro Netzwerkkarte verwenden.')
    return index


def select_outgoing_interface(sock, interface):
    # Only this UDP socket changes. IP_UNICAST_IF expects the DWORD in network
    # byte order; GetIpAddrTable does not expose or alter system routes.
    function = ctypes.WinDLL('iphlpapi', use_last_error=True).GetIpAddrTable
    function.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32), ctypes.c_int]
    function.restype = ctypes.c_uint32
    capacity = 4096
    for _ in range(3):
        data = ctypes.create_string_buffer(capacity)
        size = ctypes.c_uint32(capacity)
        result = function(data, ctypes.byref(size), False)
        if result == 122 and capacity < size.value <= 65536:
            capacity = size.value
            continue
        if result:
            raise OSError(result, 'Netzwerkschnittstelle konnte nicht gelesen werden.')
        if not 4 <= size.value <= capacity:
            raise ValueError('Ungueltige Netzwerkschnittstellenliste.')
        index = interface_index_from_table(data.raw[:size.value], interface.address)
        sock.setsockopt(socket.IPPROTO_IP, 31, index.to_bytes(4, 'big'))
        return
    raise ValueError('Das ausgewaehlte Netzwerk hat sich waehrend des Starts geaendert.')


def query_message(nonce):
    if not isinstance(nonce, str) or len(nonce) != 32 or any(c not in '0123456789abcdef' for c in nonce):
        raise ValueError('Ungueltige Suchanfrage.')
    return encode({'format': DISCOVERY_FORMAT, 'kind': 'query', 'nonce': nonce})


def answer_message(query, room):
    value = json_record(query)
    if set(value) != {'format', 'kind', 'nonce'} or value['format'] != DISCOVERY_FORMAT or value['kind'] != 'query':
        raise ValueError('Unbekannte Suchanfrage.')
    query_message(value['nonce'])
    return encode({'format': DISCOVERY_FORMAT, 'kind': 'room', 'nonce': value['nonce'],
        'room_id': room.room_id, 'name': room.name, 'content_id': room.content_id,
        'members': room.members, 'capacity': room.capacity, 'ttl': BEACON_TTL,
        'http_port': HTTP_PORT, 'udp_port': GAME_SERVICE_PORT})


def read_answer(data, source, interface, nonce, now):
    value = json_record(data)
    if (set(value) != {'format', 'kind', 'nonce', 'room_id', 'name', 'content_id',
                      'members', 'capacity', 'ttl', 'http_port', 'udp_port'}
            or value['format'] != DISCOVERY_FORMAT or value['kind'] != 'room'
            or value['nonce'] != nonce or type(value['ttl']) is not int or not 1 <= value['ttl'] <= BEACON_TTL
            or source[1] != DISCOVERY_PORT or not interface.contains(source[0])):
        raise ValueError('Die Antwort gehoert nicht zur lokalen Suche.')
    return LobbyRoom(value['room_id'], value['name'], value['content_id'],
        LobbyEndpoint('lan-direct', source[0], value['http_port'], value['udp_port']),
        interface, value['members'], value['capacity'], now + value['ttl'])


class LanDiscovery:
    def __init__(self, interfaces, *, socket_factory=socket.socket, clock=time.monotonic,
                 route_socket=select_outgoing_interface):
        if not 1 <= len(interfaces) <= 16:
            raise ValueError('Kein passendes lokales Netzwerk gefunden.')
        self.interfaces = tuple(interfaces)
        self.socket_factory, self.clock = socket_factory, clock
        self.route_socket = route_socket
        self.cache = RoomCache(clock)

    def list_rooms(self, *, timeout=2):
        if type(timeout) not in (int, float) or not math.isfinite(timeout) or not .1 <= timeout <= 5:
            raise ValueError('Ungueltige Suchdauer.')
        nonce = secrets.token_hex(16)
        packet = query_message(nonce)
        sockets = []
        try:
            for interface in self.interfaces:
                sock = None
                try:
                    sock = self.socket_factory(socket.AF_INET, socket.SOCK_DGRAM)
                    sockets.append((sock, interface))
                    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
                    sock.bind((interface.address, 0)); sock.settimeout(.03)
                    self.route_socket(sock, interface)
                    sock.sendto(packet, (str(interface.network.broadcast_address), DISCOVERY_PORT))
                    # Also permit finding a coordinator on this PC without relying
                    # on the NIC's broadcast-loopback behaviour.
                    sock.sendto(packet, (interface.address, DISCOVERY_PORT))
                except (OSError, ValueError):
                    if sock is not None and sockets and sockets[-1][0] is sock:
                        sockets.pop(); sock.close()
            deadline = self.clock() + timeout
            packets = 0
            while sockets and self.clock() < deadline and packets < 256:
                for sock, interface in sockets:
                    if packets >= 256 or self.clock() >= deadline:
                        break
                    try:
                        data, source = sock.recvfrom(MAX_DATAGRAM + 1)
                        packets += 1
                        self.cache.add(read_answer(data, source, interface, nonce, self.clock()))
                    except (OSError, ValueError, TypeError):
                        pass
            return self.cache.list_rooms()
        finally:
            for sock, _ in sockets:
                sock.close()

    def resolve(self, room_id):
        return self.cache.resolve(room_id)


class LanBeacon:
    """Reply-only beacon, tied to an explicitly selected private interface."""
    # Immutable defaults; assignments below belong to each beacon instance.
    # A global bound cannot be evaded by rotating source addresses through LRU.
    MAX_REPLIES_PER_SECOND = 32
    _reply_window = None
    _reply_count = 0

    def __init__(self, room, *, clock=time.monotonic, socket_factory=socket.socket,
                 route_socket=select_outgoing_interface):
        self.room, self.clock = room, clock
        self.stop_event = threading.Event()
        self.rates = OrderedDict()
        self.sock = socket_factory(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            route_socket(self.sock, room.local_interface)
            # A wildcard is needed to receive Windows directed broadcasts. Every
            # source is checked against the selected interface before replying.
            self.sock.bind(('0.0.0.0', DISCOVERY_PORT)); self.sock.settimeout(.2)
        except BaseException:
            self.sock.close(); raise
        self.thread = None

    def _answer(self, data, source):
        if len(data) > MAX_DATAGRAM or not self.room.local_interface.contains(source[0]):
            return None
        now = self.clock()
        previous = self.rates.get(source[0], -100)
        if now - previous < .2:
            return None
        if self._reply_window is None or now - self._reply_window >= 1:
            self._reply_window, self._reply_count = now, 0
        if self._reply_count >= self.MAX_REPLIES_PER_SECOND:
            return None
        self._reply_count += 1
        if source[0] not in self.rates and len(self.rates) >= 128:
            self.rates.popitem(last=False)
        self.rates[source[0]] = now; self.rates.move_to_end(source[0])
        try:
            return answer_message(data, self.room)
        except (ValueError, TypeError, KeyError):
            return None

    def serve(self):
        while not self.stop_event.is_set():
            try:
                data, source = self.sock.recvfrom(MAX_DATAGRAM + 1)
                answer = self._answer(data, source)
                if answer is not None:
                    self.sock.sendto(answer, source)
            except OSError:
                if self.stop_event.is_set():
                    break

    def start(self):
        self.thread = threading.Thread(target=self.serve, name='s7-lan-beacon', daemon=True)
        self.thread.start()

    def close(self):
        self.stop_event.set(); self.sock.close()
        if self.thread:
            self.thread.join(timeout=1)

    def update_members(self, members):
        if type(members) is int and 1 <= members <= MAX_MEMBERS:
            self.room = replace(self.room, members=members)
