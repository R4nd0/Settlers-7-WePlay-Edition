"""Bounded UDP LAN lookup. Announcements contain no accounts or game payloads."""
from collections import OrderedDict
from dataclasses import replace
import ctypes
import secrets
import math
import select
import socket
import threading
import time
import sys

from lobby_directory import (BEACON_TTL, DISCOVERY_FORMAT, DISCOVERY_PORT, HTTP_PORT,
    GAME_SERVICE_PORT, MAX_DATAGRAM, MAX_MEMBERS, MAX_ROOMS, LobbyEndpoint, LobbyRoom, RoomCache,
    content_id, encode, json_record, private_address, public_name, validate_discovery_port)


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
    if sys.platform == 'linux':
        from lan_interfaces import enumerate_interfaces
        matches = [row for row in enumerate_interfaces(include_all=True) if row.address == interface.address]
        if len(matches) != 1:
            raise ValueError('Das ausgewaehlte LAN ist nicht mehr eindeutig verfuegbar.')
        index = socket.if_nametoindex(matches[0].name)
        # Linux IP_UNICAST_IF (50), network byte order; affects this socket only.
        sock.setsockopt(socket.IPPROTO_IP, 50, index.to_bytes(4, 'big'))
        return
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


def read_answer(data, source, interface, nonce, now, *, port=DISCOVERY_PORT):
    value = json_record(data)
    if (set(value) != {'format', 'kind', 'nonce', 'room_id', 'name', 'content_id',
                      'members', 'capacity', 'ttl', 'http_port', 'udp_port'}
            or value['format'] != DISCOVERY_FORMAT or value['kind'] != 'room'
            or value['nonce'] != nonce or type(value['ttl']) is not int or not 1 <= value['ttl'] <= BEACON_TTL
            or source[1] != port or not interface.contains(source[0])):
        raise ValueError('Die Antwort gehoert nicht zur lokalen Suche.')
    return LobbyRoom(value['room_id'], value['name'], value['content_id'],
        LobbyEndpoint('lan-direct', source[0], value['http_port'], value['udp_port']),
        interface, value['members'], value['capacity'], now + value['ttl'])


class LanDiscovery:
    def __init__(self, interfaces, *, port=DISCOVERY_PORT, socket_factory=socket.socket, clock=time.monotonic,
                 route_socket=select_outgoing_interface):
        if not 1 <= len(interfaces) <= 16:
            raise ValueError('Kein passendes lokales Netzwerk gefunden.')
        self.interfaces = tuple(interfaces)
        self.port = validate_discovery_port(port)
        self.socket_factory, self.clock = socket_factory, clock
        self.route_socket = route_socket
        self.cache = RoomCache(clock)

    def _send_queries(self, sock, interface, packet):
        # A NIC may not support broadcast or loopback. Each destination is
        # independent, so failure on one must not discard the other's reply.
        for address in (str(interface.network.broadcast_address), interface.address):
            try:
                sock.sendto(packet, (address, self.port))
            except OSError:
                pass

    def scan(self, *, timeout=2):
        """Return fresh replies with each distinct source-bound route intact."""
        if type(timeout) not in (int, float) or not math.isfinite(timeout) or not .1 <= timeout <= 5:
            raise ValueError('Ungueltige Suchdauer.')
        nonce = secrets.token_hex(16)
        packet = query_message(nonce)
        sockets = {}
        routes = {}
        try:
            for interface in self.interfaces:
                sock = None
                try:
                    sock = self.socket_factory(socket.AF_INET, socket.SOCK_DGRAM)
                    sockets[sock] = interface
                    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
                    sock.bind((interface.address, 0))
                    sock.setblocking(False)
                    self.route_socket(sock, interface)
                    # Keep an already bound route for the scheduled retries,
                    # even if the adapter cannot send its first queries yet.
                    self._send_queries(sock, interface, packet)
                    continue
                except (OSError, ValueError):
                    pass
                if sock is not None:
                    sockets.pop(sock)
                    sock.close()
            started = self.clock()
            deadline = started + timeout
            # Up to two retransmissions within the original scan budget.
            # The interval exceeds the beacon's per-source reply cooldown.
            retries = iter((started + .25, started + .5))
            next_retry = min(next(retries), deadline)
            packets = 0
            while sockets and packets < 256:
                now = self.clock()
                if now >= deadline:
                    break
                if now >= next_retry:
                    for sock, interface in sockets.items():
                        self._send_queries(sock, interface, packet)
                    next_retry = min(next(retries, deadline), deadline)
                ready, _, _ = select.select(list(sockets), [], [], max(0, next_retry - self.clock()))
                for sock in ready:
                    if packets >= 256 or self.clock() >= deadline:
                        break
                    packets += 1
                    try:
                        data, source = sock.recvfrom(MAX_DATAGRAM + 1)
                        answer = read_answer(data, source, sockets[sock], nonce, self.clock(), port=self.port)
                        self.cache.add(answer)
                        key = (answer.room_id, answer.endpoint.host, answer.local_interface.address)
                        if key in routes or len(routes) < MAX_ROOMS:
                            routes[key] = answer
                    except (OSError, ValueError, TypeError):
                        pass
            return list(routes.values())
        finally:
            for sock in sockets:
                sock.close()

    def list_rooms(self, *, timeout=2):
        self.scan(timeout=timeout)
        return self.cache.list_rooms()

    def resolve(self, room_id):
        return self.cache.resolve(room_id)


class BeaconBindError(OSError):
    """The private-LAN discovery socket could not bind its configured port."""


class LanBeacon:
    """Reply-only beacon, tied to an explicitly selected private interface."""
    # Immutable defaults; assignments below belong to each beacon instance.
    # A global bound cannot be evaded by rotating source addresses through LRU.
    MAX_REPLIES_PER_SECOND = 32
    _reply_window = None
    _reply_count = 0

    def __init__(self, room, *, port=DISCOVERY_PORT, clock=time.monotonic, socket_factory=socket.socket,
                 route_socket=select_outgoing_interface):
        port = validate_discovery_port(port)
        self.room, self.clock = room, clock
        self.stop_event = threading.Event()
        self.rates = OrderedDict()
        self.sock = socket_factory(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            route_socket(self.sock, room.local_interface)
            # A wildcard is needed to receive Windows directed broadcasts. Every
            # source is checked against the selected interface before replying.
            try:
                self.sock.bind(('0.0.0.0', port))
            except OSError as error:
                number = getattr(error, 'winerror', None) or error.errno
                raise BeaconBindError(number, 'LAN discovery bind failed') from error
            self.sock.settimeout(.2)
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
                if answer is not None and not self.stop_event.is_set():
                    self.sock.sendto(answer, source)
            except OSError:
                if self.stop_event.is_set():
                    break

    def start(self):
        thread = threading.Thread(target=self.serve, name='s7-lan-beacon', daemon=True)
        thread.start()
        self.thread = thread

    def stop_serving(self):
        self.stop_event.set()
        if self.thread:
            self.thread.join(timeout=1)

    def close(self):
        self.stop_serving()
        self.sock.close()

    def update_members(self, members):
        if type(members) is int and 1 <= members <= MAX_MEMBERS:
            self.room = replace(self.room, members=members)
