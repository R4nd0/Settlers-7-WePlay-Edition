"""Disposable authenticated Gold/History bridge channel, bound only to loopback.

This is a connection service, not a native session adapter or playable catalogue.
Profiles describe expected launches; the game bootstraps must still verify their
actual executables and bind native session callbacks. No account data is read.
"""
import argparse
from dataclasses import dataclass, field
import hmac
import json
import os
from pathlib import Path
import secrets
import selectors
import socket
import struct
import time

HEADER = struct.Struct('<4sHH16sQBBHI')
MAX_PAYLOAD, MAX_BUFFERED = 65536, 8*1024*1024
PINS = {1: bytes.fromhex('76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab'),
        2: bytes.fromhex('4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f'),
        3: bytes.fromhex('8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410')}


@dataclass(frozen=True)
class Launch:
    edition: int
    key: bytes
    native_identity: bytes
    content: bytes

    def valid(self):
        return (type(self.edition) is int and self.edition in PINS
                and all(type(value) is bytes and len(value) == size and any(value)
                        for value, size in ((self.key, 32), (self.native_identity, 16), (self.content, 32)))
                and (self.edition != 1 or 0 < int.from_bytes(self.native_identity[:4], 'little') <= 0x7fffffff
                     and not any(self.native_identity[4:])))


@dataclass
class Connection:
    sock: object
    address: str
    created: float
    slot: int = 0
    sequence_in: int = 0
    sequence_out: int = 0
    authenticated: bool = False
    eof: bool = False
    input: bytearray = field(default_factory=bytearray)
    output: bytearray = field(default_factory=bytearray)


class CrossplayBroker:
    def __init__(self, launches, session=None):
        if (type(launches) is not tuple or len(launches) != 2 or not all(isinstance(x, Launch) and x.valid() for x in launches)
                or sorted(x.edition for x in launches) not in ([1, 2], [1, 3])
                or launches[0].key == launches[1].key or launches[0].content != launches[1].content):
            raise ValueError('Expected independent Gold/History launches with one common content digest')
        if session is None:
            session = secrets.token_bytes(16)
        if type(session) is not bytes or len(session) != 16 or not any(session):
            raise ValueError('Expected a fresh nonzero session identifier')
        self.launches, self.session = launches, session
        self.identities = (secrets.token_bytes(16), secrets.token_bytes(16))
        if (not all(any(x) for x in self.identities) or self.identities[0] == self.identities[1]
                or any(identity == launch.native_identity for identity in self.identities for launch in launches)):
            raise ValueError('Session identity collision')
        # Gold's view of the History player needs its own numeric alias. The
        # reverse view uses a freshly issued WePlay GUID, not a Ubisoft account.
        gold = next(launch for launch in launches if launch.edition == 1)
        alias = int.from_bytes(secrets.token_bytes(4), 'little') & 0x7fffffff
        if not alias or alias == int.from_bytes(gold.native_identity[:4], 'little'):
            raise ValueError('Session projection collision; start a fresh session')
        self.gold_alias = alias.to_bytes(4, 'little')+bytes(12)
        self.selector = selectors.DefaultSelector()
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            self.listener.bind(('127.0.0.1', 0))
            self.listener.listen(8)
            self.listener.setblocking(False)
            self.selector.register(self.listener, selectors.EVENT_READ)
        except BaseException:
            self.listener.close(); self.selector.close(); raise
        self.port = self.listener.getsockname()[1]
        self.connections, self.members = {}, {}
        self.retired = False
        self.rejections = self.relayed = 0

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        self.close()

    def close(self):
        for connection in tuple(self.connections.values()):
            self._drop(connection)
        self.listener.close(); self.selector.close()

    def _drop(self, connection):
        if connection.sock not in self.connections:
            return
        self.connections.pop(connection.sock)
        self.selector.unregister(connection.sock); connection.sock.close()
        if connection.authenticated:
            self.retired = True
            self.members.pop(connection.slot, None)
            # EOF must follow, rather than discard, already authenticated events
            # queued for the remaining peer. No native leave event is invented.
            for other in tuple(self.members.values()):
                if not other.output:
                    self._drop(other)

    def _reject(self, connection):
        self.rejections += 1
        self._drop(connection)

    def _queue(self, connection, kind, source, body):
        if len(body) > MAX_PAYLOAD or connection.sequence_out >= 2**64-1:
            raise ValueError('Outgoing channel bound')
        header = HEADER.pack(b'WPX1', 1, kind, self.session, connection.sequence_out+1,
                             source, connection.slot, 0, len(body))
        frame = header+body
        frame += hmac.digest(self.launches[connection.slot-1].key, frame, 'sha256')
        if len(connection.output)+len(frame) > MAX_BUFFERED:
            raise ValueError('Outgoing channel budget')
        connection.sequence_out += 1
        connection.output.extend(frame)
        self.selector.modify(connection.sock, selectors.EVENT_READ | selectors.EVENT_WRITE, connection)

    def _frames(self, connection):
        for _ in range(64):
            if len(connection.input) < HEADER.size:
                return
            magic, version, kind, session, sequence, source, target, reserved, length = HEADER.unpack_from(connection.input)
            if (magic != b'WPX1' or version != 1 or session != self.session or reserved or length > MAX_PAYLOAD
                    or source not in (1, 2) or connection.address != '127.0.0.'+str(source+1)
                    or sequence != connection.sequence_in+1 or sequence >= 2**64):
                raise ValueError('Invalid channel frame')
            total = HEADER.size+length+32
            if len(connection.input) < total:
                return
            frame = bytes(connection.input[:HEADER.size+length])
            tag = bytes(connection.input[HEADER.size+length:total])
            if not hmac.compare_digest(hmac.digest(self.launches[source-1].key, frame, 'sha256'), tag):
                raise ValueError('Invalid channel authenticator')
            body = frame[HEADER.size:]
            if not connection.authenticated:
                launch, peer = self.launches[source-1], self.launches[2-source]
                expected = bytes((launch.edition, peer.edition, 0, 0))+PINS[launch.edition]+launch.content+launch.native_identity
                if self.retired or kind != 1 or target or sequence != 1 or source in self.members or body != expected:
                    raise ValueError('Launch or session binding mismatch')
                connection.slot = source; connection.authenticated = True; self.members[source] = connection
                if len(self.members) == 2:
                    for slot, member in self.members.items():
                        other = self.launches[2-slot]
                        snapshot = bytes((other.edition, 3-slot, 0, 0))+self.identities[slot-1]+self.identities[2-slot]+other.native_identity
                        if self.launches[slot-1].edition == 1:
                            snapshot += self.gold_alias+self.identities[slot-1]
                        else:
                            snapshot += self.identities[2-slot]+self.gold_alias
                        self._queue(member, 2, 0, snapshot)
            else:
                if (self.retired or kind != 3 or source != connection.slot or target != 3-source
                        or not body or len(self.members) != 2):
                    raise ValueError('No connected peer owns this event')
                self._queue(self.members[target], 3, source, body)
                self.relayed += 1
            connection.sequence_in = sequence
            del connection.input[:total]

    def pump(self, timeout=.01):
        if not 0 <= timeout <= 1:
            raise ValueError('Invalid pump timeout')
        for key, flags in self.selector.select(timeout):
            if key.fileobj is self.listener:
                sock, address = self.listener.accept()
                if len(self.connections) >= 16 or self.retired or address[0] not in ('127.0.0.2', '127.0.0.3'):
                    sock.close(); self.rejections += 1; continue
                sock.setblocking(False)
                connection = Connection(sock, address[0], time.monotonic())
                self.connections[sock] = connection
                self.selector.register(sock, selectors.EVENT_READ, connection)
                continue
            connection = key.data
            if connection.sock not in self.connections:
                continue
            try:
                if flags & selectors.EVENT_READ and not connection.eof:
                    data = connection.sock.recv(16384)
                    if not data:
                        connection.eof = True
                    else:
                        if len(connection.input)+len(data) > MAX_BUFFERED:
                            raise ValueError('Incoming channel budget')
                        connection.input.extend(data)
                    self._frames(connection)
                if flags & selectors.EVENT_WRITE and connection.output:
                    count = connection.sock.send(connection.output[:16384])
                    if not count:
                        self._drop(connection); continue
                    del connection.output[:count]
                    if not connection.output:
                        if self.retired:
                            self._drop(connection)
                        else:
                            self.selector.modify(connection.sock, selectors.EVENT_READ, connection)
            except BlockingIOError:
                pass
            except (ValueError, OSError):
                self._reject(connection)
        for connection in tuple(self.connections.values()):
            if not connection.authenticated and time.monotonic()-connection.created > 60:
                self._reject(connection)
            elif connection.input and not self.retired:
                try:
                    self._frames(connection)
                except ValueError:
                    self._reject(connection)
            if connection.eof and connection.sock in self.connections:
                if not connection.input:
                    self._drop(connection)
                elif (len(connection.input) < HEADER.size
                      or len(connection.input) < HEADER.size+HEADER.unpack_from(connection.input)[-1]+32):
                    self._reject(connection)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ready-file', type=Path, required=True)
    parser.add_argument('--duration', type=int, default=120)
    args = parser.parse_args()
    args.ready_file = args.ready_file.resolve()
    private_root = Path(__file__).resolve().parents[2]/'work'
    if (not 1 <= args.duration <= 600 or args.ready_file.exists()
            or not args.ready_file.is_relative_to(private_root.resolve())
            or not args.ready_file.parent.is_dir()):
        parser.error('Use a new ready file in an existing work directory and a 1..600 second lifetime')
    raw = os.environ.pop('WEPLAY_CROSSPLAY_LAUNCHES', '')
    if not 1 <= len(raw) <= 2048:
        parser.error('Expected disposable environment-only launch bindings')
    try:
        rows = json.loads(raw)
        launches = tuple(Launch(row['edition'], bytes.fromhex(row['key']),
            bytes.fromhex(row['native_identity']), bytes.fromhex(row['content'])) for row in rows)
        broker = CrossplayBroker(launches)
    except (ValueError, TypeError, KeyError):
        parser.error('Invalid launch bindings')
    with broker:
        with args.ready_file.open('x', encoding='utf-8') as stream:
            json.dump({'loopback_only': True, 'port': broker.port, 'session': broker.session.hex(),
                       'native_session_admission': False}, stream)
        deadline = time.monotonic()+args.duration
        while time.monotonic() < deadline and not broker.retired:
            broker.pump()
        while broker.connections and time.monotonic() < deadline:
            broker.pump()


if __name__ == '__main__':
    main()
