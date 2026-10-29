"""Versioned local invitations for one private EasyTier game group.

An invitation contains a group secret: display/copy it only on explicit room
creation. Never include it in diagnostic logs, command lines or package files.
"""
import base64
from dataclasses import dataclass, field
import ipaddress
import json
import re
import secrets
from urllib.parse import urlsplit

from lobby_directory import private_address

PREFIX = 'weplay1.'
PUBLIC_BOOTSTRAP = 'tcp://public.easytier.cn:11010'


class InternetError(ValueError):
    def __init__(self, code):
        self.code = code
        super().__init__(code)


def endpoint(value):
    if not isinstance(value, str) or len(value) > 256:
        raise InternetError('internet_invitation_invalid')
    try:
        u = urlsplit(value)
        if (u.scheme not in ('tcp', 'udp') or not u.hostname or not u.port or
                u.username is not None or u.password is not None or
                u.path or u.query or u.fragment):
            raise ValueError()
        # Direct invites use numeric endpoints. Only the pinned public bootstrap
        # hostname is accepted; arbitrary URLs, DNS aliases and credentials aren't.
        if value != PUBLIC_BOOTSTRAP:
            address = ipaddress.ip_address(u.hostname)
            if address.is_unspecified or address.is_multicast or address.is_loopback or address.is_link_local:
                raise ValueError()
            canonical = f'[{address}]' if address.version == 6 else str(address)
            if value != f'{u.scheme}://{canonical}:{u.port}':
                raise ValueError()
    except ValueError:
        raise InternetError('internet_invitation_invalid') from None
    return value


@dataclass(frozen=True)
class Room:
    room_id: str
    secret: str = field(repr=False)
    subnet: str
    mode: str = 'assisted'
    bootstrap: tuple = (PUBLIC_BOOTSTRAP,)

    def __post_init__(self):
        try:
            if not re.fullmatch('[0-9a-f]{32}', self.room_id) or not re.fullmatch('[0-9a-f]{64}', self.secret):
                raise ValueError()
            net = ipaddress.IPv4Network(self.subnet)
            if str(net) != self.subnet or net.prefixlen != 24:
                raise ValueError()
            private_address(str(net.network_address + 1))
            if self.mode not in ('direct', 'assisted') or not isinstance(self.bootstrap, tuple) or len(self.bootstrap) > 3:
                raise ValueError()
            for value in self.bootstrap:
                endpoint(value)
            if self.mode == 'assisted' and self.bootstrap != (PUBLIC_BOOTSTRAP,):
                raise ValueError()
            if self.mode == 'direct' and (not self.bootstrap or PUBLIC_BOOTSTRAP in self.bootstrap):
                raise ValueError()
        except (ValueError, TypeError, AttributeError):
            raise InternetError('internet_invitation_invalid') from None

    @property
    def network(self):
        return ipaddress.IPv4Network(self.subnet)

    @property
    def coordinator(self):
        return str(self.network.network_address + 1)

    def token(self):
        data = dict(v=1, room=self.room_id, secret=self.secret, subnet=self.subnet,
                    mode=self.mode, bootstrap=list(self.bootstrap))
        raw = json.dumps(data, sort_keys=True, separators=(',', ':')).encode('ascii')
        return PREFIX + base64.urlsafe_b64encode(raw).decode('ascii').rstrip('=')


def parse(token):
    if not isinstance(token, str) or not token.startswith(PREFIX) or len(token) > 2048:
        raise InternetError('internet_invitation_invalid')
    body = token[len(PREFIX):]
    if not re.fullmatch('[A-Za-z0-9_-]+', body):
        raise InternetError('internet_invitation_invalid')
    try:
        def unique(items):
            result = {}
            for key, value in items:
                if key in result:
                    raise ValueError()
                result[key] = value
            return result
        data = json.loads(base64.b64decode(body + '=' * (-len(body) % 4), altchars=b'-_', validate=True),
                          object_pairs_hook=unique)
        if (not isinstance(data, dict) or set(data) != {'v', 'room', 'secret', 'subnet', 'mode', 'bootstrap'} or
                type(data['v']) is not int or data['v'] != 1 or not isinstance(data['bootstrap'], list)):
            raise ValueError()
        room = Room(data['room'], data['secret'], data['subnet'], data['mode'], tuple(data['bootstrap']))
        if room.token() != token:
            raise ValueError()
        return room
    except (ValueError, TypeError, KeyError, UnicodeError):
        raise InternetError('internet_invitation_invalid') from None


def check_overlap(room, interfaces):
    if any(room.network.overlaps(interface.network) for interface in interfaces):
        raise InternetError('internet_network_overlap')


def create(interfaces, *, mode='assisted', direct_endpoint=None):
    if mode not in ('direct', 'assisted') or (mode == 'direct') != (direct_endpoint is not None):
        raise InternetError('internet_invitation_invalid')
    for _ in range(32):
        subnet = f'10.{secrets.randbelow(192) + 32}.{secrets.randbelow(256)}.0/24'
        room = Room(secrets.token_hex(16), secrets.token_hex(32), subnet, mode,
                    (endpoint(direct_endpoint),) if mode == 'direct' else (PUBLIC_BOOTSTRAP,))
        try:
            check_overlap(room, interfaces)
            return room
        except InternetError:
            continue
    raise InternetError('internet_network_overlap')


def select_interface(room, address, name, interfaces):
    private_address(address)
    if ipaddress.IPv4Address(address) not in room.network:
        raise InternetError('internet_adapter_invalid')
    matches = [i for i in interfaces if i.name == name]
    if len(matches) != 1 or matches[0].address != address or matches[0].prefix_length != 24:
        raise InternetError('internet_adapter_invalid')
    # The requested device cannot be accepted merely because its display name
    # matches. Require the core's own IP and no competing physical/overlay route.
    check_overlap(room, [i for i in interfaces if i is not matches[0]])
    return matches[0]
