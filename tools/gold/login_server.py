"""Experimental Gold SDK login service. IPv4 loopback only; no game-session admission.

The native Gold transport and scalar readers define this wire profile. It is
separate from History's Storm service. Never import vendor tickets/accounts or
log datagrams, names, passwords, session keys, challenges or station URLs.
"""
from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import os
from pathlib import Path
import secrets
import select
import socket
import struct
import time
import zlib
from contextlib import nullcontext
from dataclasses import dataclass, field

MAX_DATAGRAM = 2048
MAX_MESSAGE = 65536
CHECKSUM_SEED = 3  # Sum of the explicit local transport key, modulo 256.
LOCAL_ACCESS_KEY = "WePlayGoldLoopback"
CLASS_NAME = b"UbiAuthenticationLoginCustomData"
TRANSPORT_KEY = b"CD&ML"  # Native RC4Encryption's compatibility key, not authentication.


def checksum(data: bytes, seed: int = CHECKSUM_SEED) -> int:
    end = len(data) - len(data) % 4
    words = sum(struct.unpack_from("<I", data, i)[0] for i in range(0, end, 4)) & 0xffffffff
    return (seed + sum(words.to_bytes(4, "little")) + sum(data[end:])) & 255


def rc4(data: bytes, key: bytes) -> bytes:
    """Gold resets the permutation for each packet; never use as new encryption."""
    if not key or len(key) > 256:
        raise ValueError("Invalid compatibility key length")
    box = list(range(256))
    j = 0
    for i in range(256):
        j = (j + box[i] + key[i % len(key)]) & 255
        box[i], box[j] = box[j], box[i]
    i = j = 0
    output = bytearray()
    for byte in data:
        i = (i + 1) & 255
        j = (j + box[i]) & 255
        box[i], box[j] = box[j], box[i]
        output.append(byte ^ box[(box[i] + box[j]) & 255])
    return bytes(output)


def unwrap(payload: bytes) -> bytes:
    if not payload or len(payload) > MAX_MESSAGE + 1:
        raise ValueError("Invalid transport payload length")
    value = rc4(payload, TRANSPORT_KEY)
    if value[0] == 0:
        return value[1:]
    decoder = zlib.decompressobj(15)
    result = decoder.decompress(value[1:], MAX_MESSAGE + 1)
    if (len(result) > MAX_MESSAGE or not decoder.eof
            or decoder.unused_data or decoder.unconsumed_tail):
        raise ValueError("Invalid or excessive compressed message")
    return result


def wrap(message: bytes) -> bytes:
    if len(message) > MAX_MESSAGE:
        raise ValueError("Excessive response length")
    return rc4(b"\0" + message, TRANSPORT_KEY)


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.position = 0

    def take(self, count: int) -> bytes:
        if count < 0 or count > len(self.data) - self.position:
            raise ValueError("Truncated argument")
        value = self.data[self.position:self.position + count]
        self.position += count
        return value

    def u32(self) -> int:
        return struct.unpack("<I", self.take(4))[0]

    def text(self, maximum: int = 256) -> bytes:
        size = struct.unpack("<H", self.take(2))[0]
        if not 1 <= size <= maximum + 1:
            raise ValueError("Invalid string length")
        value = self.take(size)
        if value[-1] != 0 or b"\0" in value[:-1] or not value[:-1].isascii():
            raise ValueError("Invalid local ASCII string")
        return value[:-1]

    def finish(self) -> None:
        if self.position != len(self.data):
            raise ValueError("Trailing arguments")


def text(value: str) -> bytes:
    encoded = value.encode("ascii") + b"\0"
    if len(encoded) > 257:
        raise ValueError("Excessive local string")
    return struct.pack("<H", len(encoded)) + encoded


@dataclass(frozen=True)
class Packet:
    kind: int
    flags: int
    session: int
    signature: int
    sequence: int
    connection: int
    fragment: int
    payload: bytes


def decode(data: bytes) -> Packet:
    if not 11 <= len(data) <= MAX_DATAGRAM or data[:2] != b"\x3f\x31":
        raise ValueError("Unsupported Gold virtual ports or length")
    kind, flags = data[2] & 7, data[2] & 248
    if kind > 4 or flags & 128 or checksum(data[:-1]) != data[-1]:
        raise ValueError("Invalid Gold packet")
    offset = 14 if kind < 2 else 11 if kind == 2 else 10
    if len(data) <= offset:
        raise ValueError("Truncated header")
    connection = int.from_bytes(data[10:14], "little") if kind < 2 else 0
    fragment = data[10] if kind == 2 else 0
    if flags & 64:
        if len(data) < offset + 3 or int.from_bytes(data[offset:offset + 2], "little") != len(data) - offset - 3:
            raise ValueError("Invalid payload size")
        offset += 2
    return Packet(kind, flags, data[3], int.from_bytes(data[4:8], "little"),
                  int.from_bytes(data[8:10], "little"), connection, fragment, data[offset:-1])


def encode(kind: int, flags: int, session: int, signature: int, sequence: int,
           payload: bytes = b"", connection: int = 0, fragment: int = 0) -> bytes:
    header = bytes((0x31, 0x3f, kind | flags, session)) + struct.pack("<IH", signature, sequence)
    if kind < 2:
        header += struct.pack("<I", connection)
    elif kind == 2:
        header += bytes((fragment,))
    if flags & 64:
        header += struct.pack("<H", len(payload))
    result = header + payload
    if len(result) + 1 > MAX_DATAGRAM:
        raise ValueError("Excessive response datagram")
    return result + bytes((checksum(result),))


def rpc_request(payload: bytes) -> tuple[int, int, int, bytes]:
    message = unwrap(payload)
    if (len(message) < 13 or int.from_bytes(message[:4], "little") != len(message) - 4
            or not 0x81 <= message[4] <= 0xfe):
        raise ValueError("Unsupported local RPC")
    call, method = struct.unpack_from("<II", message, 5)
    if call == 0 or method >= 0x8000:
        raise ValueError("Missing RPC correlation")
    return message[4] & 127, call, method, message[13:]


def rpc_response(protocol: int, call: int, method: int, arguments: bytes) -> bytes:
    body = bytes((protocol, 1)) + struct.pack("<II", call, method | 0x8000) + arguments
    return wrap(struct.pack("<I", len(body)) + body)


def rpc_rejection(protocol: int, call: int) -> bytes:
    body = bytes((protocol, 0)) + struct.pack("<II", 0x80010002, call)
    return wrap(struct.pack("<I", len(body)) + body)


@dataclass
class Connection:
    server: int = field(default_factory=lambda: secrets.randbits(32) or 1)
    client: int = 0
    session: int = 0
    connected: bool = False
    closed: bool = False
    expires: float = 0
    next_in: int = 2
    next_out: int = 1
    registered: bool = False
    fragments: bytes = b""
    fragment_next: int = 1
    fragment_until: float = 0
    history: dict[int, tuple[bytes, tuple[bytes, ...]]] = field(default_factory=dict)
    pending: dict[int, tuple[bytes, float, int]] = field(default_factory=dict)


class GoldLoginAdapter:
    """One disposable local principal. No lobbies, ownership claims or vendor login."""
    def __init__(self, username: str, password: str, backend_port: int, *, account_mode: bool = False):
        self.username = username.encode("ascii")
        self.password = password.encode("ascii")
        if (not 1 <= len(self.username) <= 64 or b"\0" in self.username
                or len(self.password) != 64 or any(c not in b"0123456789abcdef" for c in self.password)
                or not 1024 <= backend_port <= 65535):
            raise ValueError("Expected local name, fresh 256-bit launch secret and loopback port")
        self.backend_port = backend_port
        self.principal = secrets.randbelow(0x7ffffffe) + 1
        self.connection_id = secrets.randbelow(0x7ffffffe) + 1
        self.session_key = secrets.token_bytes(16)
        self.opaque_ticket = secrets.token_bytes(32)
        self.admitted_peer: tuple[str, int] | None = None
        self.admitted_until = 0.0
        self.connections: dict[tuple[bool, tuple[str, int]], Connection] = {}
        self.registered = False
        self.profile = None
        if account_mode:
            from account_profile import LocalProfile
            self.profile = LocalProfile(username)
        self.events: dict[str, int] = {}
        # Embedded Gold JobUbiAuthenticationLogin ticket password; custom login
        # credentials above remain independent and must match the launch secret.
        self.ticket_key = b"UbiDummyPwd"
        for _ in range(65000 + self.principal % 1024):
            self.ticket_key = hashlib.md5(self.ticket_key).digest()

    def event(self, name: str) -> None:
        self.events[name] = self.events.get(name, 0) + 1

    def ticket(self) -> bytes:
        plain = self.session_key + struct.pack("<II", 1, len(self.opaque_ticket)) + self.opaque_ticket
        encrypted = rc4(plain, self.ticket_key)
        return encrypted + hmac.digest(self.ticket_key, encrypted, "md5")

    def credentials_match(self, args: bytes) -> bool:
        reader = Reader(args)
        reader.text(64)  # Native SDK's outer ticket-granting name, not the local identity.
        if reader.text(64) != CLASS_NAME:
            return False
        outer_size, inner_size = reader.u32(), reader.u32()
        if outer_size != inner_size + 4 or inner_size != len(args) - reader.position:
            return False
        name, online_key, password = reader.text(64), reader.text(256), reader.text(64)
        reader.finish()
        return (hmac.compare_digest(name, self.username) and not online_key
                and hmac.compare_digest(password, self.password))

    def challenge(self, payload: bytes) -> bytes:
        reader = Reader(unwrap(payload))
        ticket = reader.take(reader.u32())
        encrypted = reader.take(reader.u32())
        reader.finish()
        if (not hmac.compare_digest(ticket, self.opaque_ticket) or len(encrypted) != 28
                or not hmac.compare_digest(encrypted[-16:], hmac.digest(self.session_key, encrypted[:-16], "md5"))):
            raise ValueError("Unowned or invalid ticket challenge")
        principal, _connection, nonce = struct.unpack("<III", rc4(encrypted[:-16], self.session_key))
        if principal != self.principal:
            raise ValueError("Wrong local principal")
        return wrap(struct.pack("<II", 4, (nonce + 1) & 0xffffffff))

    def response(self, backend: bool, peer: tuple[str, int], payload: bytes, now: float,
                 state: Connection | None = None) -> bytes:
        protocol, call, method, args = rpc_request(payload)
        if not backend and (protocol, method) == (10, 2):
            if self.admitted_peer not in (None, peer) or not self.credentials_match(args):
                self.event("login_rejected")
                return rpc_rejection(protocol, call)
            self.admitted_peer, self.admitted_until = peer, now + 120
            ticket = self.ticket()
            url = f"prudps:/address=127.0.0.1;port={self.backend_port};CID=1;PID=1;sid=1;stream=3;type=2"
            result = struct.pack("<III", 0x10001, self.principal, len(ticket)) + ticket
            result += text(url) + struct.pack("<I", 0) + text("") + text("")
            # RVConnectionData = regular URL, special-protocol list, special URL;
            # LoginEx then has one additional return-message string.
            self.event("login_answered")
            return rpc_response(protocol, call, method, result)
        if (not backend and (protocol, method) == (10, 3) and self.admitted_peer == peer
                and args == struct.pack("<II", self.principal, 1) and now < self.admitted_until):
            ticket = self.ticket()
            self.event("ticket_answered")
            return rpc_response(protocol, call, method, struct.pack("<II", 0x10001, len(ticket)) + ticket)
        if backend and (protocol, method) == (11, 1) and now < self.admitted_until:
            reader = Reader(args)
            count = reader.u32()
            if not 1 <= count <= 8:
                raise ValueError("Invalid station list")
            for _ in range(count):
                reader.text(256)
            reader.finish()
            url = (f"prudps:/address=127.0.0.1;port={peer[1]};CID=1;PID={self.principal};"
                   "sid=15;stream=3;type=3")
            self.registered = True
            if state is not None:
                state.registered = True
            self.event("registered")
            return rpc_response(protocol, call, method, struct.pack("<II", 0x10001, self.connection_id) + text(url))
        if backend and state is not None and state.registered and self.profile is not None:
            result = self.profile.response(protocol, method, args, Reader)
            if result is not None:
                arguments, event = result
                self.event(event)
                return rpc_response(protocol, call, method, arguments)
        self.event("rpc_unsupported")
        return rpc_rejection(protocol, call)

    def handle(self, backend: bool, peer: tuple[str, int], data: bytes, now: float) -> list[bytes]:
        if peer[0] != "127.0.0.1" or not 1 <= peer[1] <= 65535:
            return []
        try:
            packet = decode(data)
            self.connections = {k: v for k, v in self.connections.items() if v.expires > now}
            key = backend, peer
            if packet.kind == 0:
                if (packet.flags != 32 or packet.session or packet.sequence or packet.signature
                        or packet.connection or packet.payload or (backend and now >= self.admitted_until)):
                    return []
                if key not in self.connections:
                    if len(self.connections) >= 8:
                        return []
                    self.connections[key] = Connection(expires=now + 120)
                state = self.connections[key]
                if state.closed:
                    return []
                self.event("backend_syn" if backend else "auth_syn")
                return [encode(0, 8, 0, 0, 0, connection=state.server)]
            state = self.connections.get(key)
            if state is None or packet.signature != state.server:
                return []
            if state.closed:
                if (packet.kind == 3 and packet.flags == 48 and not packet.payload
                        and packet.session == state.session):
                    return [encode(3, 8, state.session, state.client, packet.sequence)]
                return []
            if packet.kind == 1:
                if packet.flags not in (48, 112) or packet.sequence != 1 or packet.session == 0 or not packet.connection:
                    return []
                if state.connected and (state.client != packet.connection or state.session != packet.session):
                    return []
                if backend:
                    if now >= self.admitted_until:
                        return []
                    response = self.challenge(packet.payload)
                elif packet.payload:
                    return []
                else:
                    response = b""
                state.connected, state.client, state.session = True, packet.connection, packet.session
                self.event("backend_connect" if backend else "auth_connect")
                return [encode(1, 72, packet.session, packet.connection, 1, response, packet.connection)]
            if not state.connected or state.session != packet.session:
                return []
            if packet.kind == 2 and packet.flags == 8 and not packet.payload:
                pending = state.pending.get(packet.sequence)
                if pending is not None and pending[0][10] == packet.fragment:
                    state.pending.pop(packet.sequence)
                    state.expires = now + 120
                return []
            if packet.kind == 2 and packet.flags in (48, 112) and packet.payload:
                previous = state.history.get(packet.sequence)
                if previous is not None:
                    if previous[0] != hashlib.sha256(bytes((packet.fragment,)) + packet.payload).digest():
                        return []
                    ack = encode(2, 8, state.session, state.client, packet.sequence, fragment=packet.fragment)
                    return [ack, *previous[1]]
                # Reserve room for the largest bounded profile response before
                # executing any stateful RPC. Wait for real ACKs under pressure.
                if packet.sequence != state.next_in or state.next_in > 0xffff or len(state.pending) > 8:
                    return []
                if state.fragments and now >= state.fragment_until:
                    state.closed = True
                    state.fragments = b""
                    state.pending.clear()
                    self.event("fragment_timeout")
                    return []
                digest = hashlib.sha256(bytes((packet.fragment,)) + packet.payload).digest()
                replies = [encode(2, 8, state.session, state.client, packet.sequence, fragment=packet.fragment)]
                part = unwrap(packet.payload)
                if not part or len(state.fragments) + len(part) > MAX_MESSAGE:
                    raise ValueError("Excessive fragmented message")
                if packet.fragment:
                    if packet.fragment != state.fragment_next or state.fragment_next >= 64:
                        return []
                    if not state.fragments:
                        state.fragment_until = now + 10
                    state.fragments += part
                    state.fragment_next += 1
                    packets: tuple[bytes, ...] = ()
                    self.event("fragment_accepted")
                else:
                    if state.next_out > 0x10000 - 64:
                        state.closed = True
                        state.fragments = b""
                        state.pending.clear()
                        self.event("sequence_exhausted")
                        return []
                    request = wrap(state.fragments + part)
                    response = self.response(backend, peer, request, now, state)
                    plain = unwrap(response)
                    parts = [plain[i:i + 900] for i in range(0, len(plain), 900)]
                    if (not 1 <= len(parts) <= 64 or len(state.pending) + len(parts) > 64
                            or state.next_out + len(parts) > 0x10000):
                        raise ValueError("Excessive pending response")
                    packets = tuple(encode(2, 112, state.session, state.client, state.next_out + index,
                        wrap(value), fragment=index + 1 if index < len(parts) - 1 else 0)
                        for index, value in enumerate(parts))
                    for index, wire in enumerate(packets):
                        state.pending[state.next_out + index] = wire, now + .25, 1
                    state.next_out += len(packets)
                    if state.fragments:
                        self.event("message_reassembled")
                    if len(packets) > 1:
                        self.event("response_fragmented")
                    state.fragments = b""
                    state.fragment_next = 1
                    state.fragment_until = 0
                state.history[packet.sequence] = digest, packets
                state.next_in += 1
                state.history = {sequence: value for sequence, value in state.history.items()
                                 if sequence >= state.next_in - 64}
                state.expires = now + 120
                return replies + list(packets)
            if packet.kind == 4 and packet.flags == 32 and not packet.payload:
                state.expires = now + 120
                return [encode(4, 8, state.session, state.client, packet.sequence)]
            if packet.kind == 3 and packet.flags == 48 and not packet.payload:
                # Keep the record for duplicate disconnect ACKs until its TTL.
                state.closed = True
                state.pending.clear()
                state.fragments = b""
                state.history.clear()
                self.event("backend_disconnect" if backend else "auth_disconnect")
                return [encode(3, 8, state.session, state.client, packet.sequence)]
            return []
        except (ValueError, struct.error, zlib.error, UnicodeError, OverflowError):
            self.event("invalid_input")
            return []

    def retransmissions(self, now: float) -> list[tuple[bool, tuple[str, int], bytes]]:
        replies = []
        for (backend, peer), state in self.connections.items():
            if state.closed or state.expires <= now:
                state.pending.clear()
                continue
            for sequence, (packet, due, attempts) in list(state.pending.items()):
                if now < due:
                    continue
                if attempts >= 8:
                    del state.pending[sequence]
                    self.event("response_timeout")
                    continue
                replies.append((backend, peer, packet))
                state.pending[sequence] = packet, now + min(2., .25 * 2 ** attempts), attempts + 1
        return replies


def serve(username: str, password: str, ready: Path, report: Path, duration: int,
          *, account_mode: bool = False) -> None:
    if not 1 <= duration <= 600:
        raise ValueError("Duration must be bounded to 1..600 seconds")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as auth, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as backend:
        for sock in (auth, backend):
            sock.bind(("127.0.0.1", 0))
            sock.setblocking(False)
        adapter = GoldLoginAdapter(username, password, backend.getsockname()[1], account_mode=account_mode)
        config_context = nullcontext(None)
        if account_mode:
            from account_config import ConfigService
            config_context = ConfigService(auth.getsockname()[1])
        with config_context as config:
            addresses = {"port": auth.getsockname()[1], "backend_port": backend.getsockname()[1], "loopback_only": True}
            readers = [auth, backend]
            if config is not None:
                addresses["config_port"] = config.server_port
                readers.append(config.socket)
            ready.write_text(json.dumps(addresses), encoding="utf-8")
            started = time.monotonic()
            while time.monotonic() - started < duration:
                for sock in select.select(readers, (), (), .1)[0]:
                    if config is not None and sock is config.socket:
                        config.handle_request()
                        continue
                    data, peer = sock.recvfrom(MAX_DATAGRAM + 1)
                    for reply in adapter.handle(sock is backend, peer, data, time.monotonic()):
                        sock.sendto(reply, peer)
                for is_backend, peer, reply in adapter.retransmissions(time.monotonic()):
                    (backend if is_backend else auth).sendto(reply, peer)
                report.write_text(json.dumps({"registered": adapter.registered, "events": adapter.events,
                    "config_responses": config.responses if config is not None else 0,
                    "config_rejections": config.rejections if config is not None else {},
                    "config_host_rejections": config.host_rejections if config is not None else {},
                    "raw_payload_saved": False, "game_session_admission": False}), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ready-file", required=True, type=Path)
    parser.add_argument("--report-file", required=True, type=Path)
    parser.add_argument("--duration", type=int, default=120)
    parser.add_argument("--account-mode", action="store_true", help="Fresh in-memory local account prototype; no lobby/progression admission")
    args = parser.parse_args()
    password = os.environ.pop("WEPLAY_GOLD_LOCAL_SECRET", "")
    username = os.environ.pop("WEPLAY_GOLD_LOCAL_NAME", "WePlayGoldLocal")
    serve(username, password, args.ready_file, args.report_file, args.duration, account_mode=args.account_mode)


if __name__ == "__main__":
    main()
