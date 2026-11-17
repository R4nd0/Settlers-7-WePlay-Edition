"""Three-edition loopback transport; no native join or release activation.

Reuses the pair broker's bounded nonblocking I/O and drain-before-EOF lifecycle.
The separate WPX2 handshake cannot be mistaken for a connected v1 pair.
No account caches, raw traffic logging or native game results are involved.
"""
import hmac
import secrets
import selectors

from crossplay_broker import CrossplayBroker, HEADER, Launch, MAX_BUFFERED, MAX_PAYLOAD, PINS


class CrossplayRoomBroker(CrossplayBroker):
    allowed_addresses = ('127.0.0.2', '127.0.0.3', '127.0.0.4')

    def __init__(self, launches, *, host_edition, session=None, generation=1):
        if (type(launches) is not tuple or len(launches) != 3
                or not all(isinstance(value, Launch) and value.valid() for value in launches)
                or [value.edition for value in launches] != [1, 2, 3]
                or len({value.key for value in launches}) != 3
                or len({value.native_identity for value in launches}) != 3
                or len({value.content for value in launches}) != 1
                or type(host_edition) is not int or host_edition not in (1, 3)
                or type(generation) is not int or not 1 <= generation <= 0xffffffff):
            raise ValueError('Expected one Gold/Ubisoft/Steam room and a Gold or Steam host')
        session = secrets.token_bytes(16) if session is None else session
        if type(session) is not bytes or len(session) != 16 or not any(session):
            raise ValueError('Expected a fresh nonzero room identifier')
        self.launches, self.session = launches, session
        self.host_slot, self.generation = host_edition, generation
        self.identities = tuple(secrets.token_bytes(16) for _ in launches)
        if (not all(any(value) for value in self.identities)
                or len(set(self.identities)) != 3 or session in self.identities
                or any(value == launch.native_identity for value in self.identities for launch in launches)):
            raise ValueError('Room identity collision')
        aliases = [launches[0].native_identity]
        for _ in range(2):
            value = (int.from_bytes(secrets.token_bytes(4), 'little') & 0x7fffffff).to_bytes(4, 'little')+bytes(12)
            if not any(value) or value in aliases:
                raise ValueError('Native projection collision; use a fresh room')
            aliases.append(value)
        # Viewer-specific existing namespace shapes. A common room peer ID
        # remains separate from a native ID and from any ProfileName reference.
        history_view = (self.identities[0], launches[1].native_identity, launches[2].native_identity)
        if len(set(history_view)) != 3:
            raise ValueError('History namespace collision')
        self.projections = (tuple(aliases), history_view, history_view)
        order = [host_edition]+[edition for edition in (1, 2, 3) if edition != host_edition]
        self.logical_slots = tuple(order.index(edition)+1 for edition in (1, 2, 3))
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
        body = bytearray((3, self.host_slot, viewer, 0))
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
                    or source not in (1, 2, 3) or connection.address != '127.0.0.'+str(source+1)
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
                expected = (bytes((launch.edition, 3, self.host_slot, 0))+self.generation.to_bytes(4, 'little')
                            +PINS[launch.edition]+launch.content+launch.native_identity)
                if self.retired or kind != 1 or target or sequence != 1 or source in self.members or body != expected:
                    raise ValueError('Room launch binding mismatch')
                connection.slot = source;connection.authenticated = True;self.members[source] = connection
                if len(self.members) == 3:
                    for slot, member in self.members.items():
                        self._queue(member, 2, 0, self._connected(slot))
            else:
                if (self.retired or kind != 3 or source != connection.slot or not body or len(self.members) != 3
                        or target not in (0, 1, 2, 3) or target == source):
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
