"""Loopback transport for rooms with a mix of original editions.

Reuses the pair broker's bounded nonblocking I/O and drain-before-EOF lifecycle.
The separate WPX2 handshake cannot be mistaken for a connected v1 pair.
No account caches, raw traffic logging or native game results are involved.
"""
import hmac
import secrets
import selectors

from crossplay_broker import CrossplayBroker, HEADER, Launch, MAX_BUFFERED, MAX_PAYLOAD, PINS


class CrossplayRoomBroker(CrossplayBroker):
    def __init__(self, launches, *, host_slot, session=None, generation=1):
        if (type(launches) is not tuple or not 2 <= len(launches) <= 8
                or not all(isinstance(value, Launch) and value.valid() for value in launches)
                or len({value.key for value in launches}) != len(launches)
                or len({value.native_identity for value in launches}) != len(launches)
                or len({value.content for value in launches}) != 1
                or type(host_slot) is not int or not 1 <= host_slot <= len(launches)
                or type(generation) is not int or not 1 <= generation <= 0xffffffff):
            raise ValueError('Invalid room roster, host slot or generation')
        session = secrets.token_bytes(16) if session is None else session
        if type(session) is not bytes or len(session) != 16 or not any(session):
            raise ValueError('Expected a fresh nonzero room identifier')
        self.launches, self.session = launches, session
        self.host_slot, self.generation = host_slot, generation
        self.count = len(launches)
        self.allowed_addresses = tuple(f'127.0.0.{slot+1}' for slot in range(1,self.count+1))
        self.identities = tuple(secrets.token_bytes(16) for _ in launches)
        if (not all(any(value) for value in self.identities)
                or len(set(self.identities)) != self.count or session in self.identities
                or any(value == launch.native_identity for value in self.identities for launch in launches)):
            raise ValueError('Room identity collision')
        gold_view = tuple(launch.native_identity if launch.edition == 1 else
                          (secrets.randbelow(0x7fffffff) + 1).to_bytes(4, 'little')+bytes(12)
                          for launch in launches)
        history_view = tuple(peer if launch.edition == 1 else launch.native_identity
                             for launch, peer in zip(launches, self.identities))
        if len(set(gold_view)) != self.count or len(set(history_view)) != self.count:
            raise ValueError('Native projection collision; use a fresh room')
        self.projections = tuple(gold_view if launch.edition == 1 else history_view for launch in launches)
        slots = range(1,self.count+1)
        order = [host_slot]+[slot for slot in slots if slot != host_slot]
        self.logical_slots = tuple(order.index(slot)+1 for slot in slots)
        self._open_listener()

    def _queue(self, connection, kind, source, body):
        if len(body) > MAX_PAYLOAD or connection.sequence_out >= 2**64-1:
            raise ValueError('Outgoing room bound')
        header = HEADER.pack(b'WPX2', 2, kind, self.session, connection.sequence_out+1,
                             source, connection.slot, 0, len(body))
        frame = header+body
        frame += hmac.digest(self.launches[connection.slot-1].key, frame, 'sha256')
        if len(connection.output)+len(frame) > MAX_BUFFERED:
            raise ValueError('Outgoing room budget')
        connection.sequence_out += 1
        connection.output.extend(frame)
        self.selector.modify(connection.sock, selectors.EVENT_READ | selectors.EVENT_WRITE, connection)

    def _connected(self, viewer):
        body = bytearray((self.count, self.host_slot, viewer, 0))
        body.extend(self.generation.to_bytes(4, 'little'))
        body.extend(self.launches[0].content)
        for slot, launch in enumerate(self.launches, 1):
            body.extend((launch.edition, slot, self.logical_slots[slot-1], 0))
            body.extend(self.identities[slot-1])
            body.extend(launch.native_identity)
            body.extend(self.projections[viewer-1][slot-1])
        return bytes(body)

    def _frames(self, connection):
        for _ in range(64):
            if len(connection.input) < HEADER.size:
                return
            magic, version, kind, session, sequence, source, target, reserved, length = HEADER.unpack_from(connection.input)
            if (magic != b'WPX2' or version != 2 or session != self.session or reserved or length > MAX_PAYLOAD
                    or not 1 <= source <= self.count or connection.address != self.allowed_addresses[source-1]
                    or sequence != connection.sequence_in+1 or sequence >= 2**64):
                raise ValueError('Invalid room frame')
            total = HEADER.size+length+32
            if len(connection.input) < total:
                return
            frame = bytes(connection.input[:HEADER.size+length]);tag = bytes(connection.input[HEADER.size+length:total])
            if not hmac.compare_digest(hmac.digest(self.launches[source-1].key, frame, 'sha256'), tag):
                raise ValueError('Invalid room authenticator')
            body = frame[HEADER.size:]
            if not connection.authenticated:
                launch = self.launches[source-1]
                expected = (bytes((launch.edition, self.count, self.host_slot, 0))+self.generation.to_bytes(4, 'little')
                            +PINS[launch.edition]+launch.content+launch.native_identity)
                if self.retired or kind != 1 or target or sequence != 1 or source in self.members or body != expected:
                    raise ValueError('Room launch binding mismatch')
                connection.slot = source;connection.authenticated = True;self.members[source] = connection
                if len(self.members) == self.count:
                    for slot, member in self.members.items():
                        self._queue(member, 2, 0, self._connected(slot))
            else:
                if (self.retired or kind != 3 or source != connection.slot or not body or len(self.members) != self.count
                        or target > self.count or target == source):
                    raise ValueError('No complete room owns this event')
                targets = [value for slot, value in self.members.items() if slot != source and (not target or slot == target)]
                for other in targets:
                    if other.sequence_out >= 2**64-1 or len(other.output)+total > MAX_BUFFERED:
                        raise ValueError('Room fanout would exceed a bound')
                for other in targets:
                    self._queue(other, 3, source, body)
                    self.relayed += 1
            connection.sequence_in = sequence
            del connection.input[:total]
