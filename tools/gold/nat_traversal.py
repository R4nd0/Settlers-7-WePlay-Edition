"""Bounded native Gold probe initiation between published loopback hosts.

This relays the original 3/1 request as 3/2 to the registered host. The game
still sends and answers its own UDP probes; no ping result is manufactured.
"""
from dataclasses import dataclass
import struct

from login_server import Reader, encode, text, wrap


def station_fields(value):
    scheme, separator, tail = value.partition(":/")
    if scheme != "prudp" or not separator:
        raise ValueError("Unsupported probe station")
    fields = {}
    for item in tail.removesuffix(";").split(";"):
        key, separator, value = item.partition("=")
        if not separator or not key or not value or key in fields:
            raise ValueError("Invalid probe station attribute")
        fields[key] = value
    # Native StationURL readers store attributes in a map. Serialization may
    # reorder them, but may not change, add or duplicate their values.
    return tuple(sorted(fields.items()))


@dataclass
class Probe:
    source_adapter: object
    source: object
    target_adapter: object
    target: object
    expires: float
    call: int = 0
    sequence: int = 0


class ProbeRelay:
    def __init__(self, adapters, gatherings):
        self.adapters = tuple(adapters)
        self.gatherings = gatherings
        self.jobs = {}
        self.next_call = 0x40000000

    @staticmethod
    def live(adapter, state, now):
        return (state.connected and state.registered and not state.closed
            and state.expires > now and state.public_url is not None
            and any(backend and owner is state for (backend, _), owner in adapter.connections.items()))

    def published(self, state, now):
        return any(record.owner is state
            and record.value in self.gatherings.catalogue(0, 20, 16, now)
            for record in self.gatherings.records.values())

    def request(self, adapter, source, arguments, now):
        if not self.live(adapter, source, now):
            raise ValueError("Probe requester unavailable")
        reader = Reader(arguments)
        count = reader.u32()
        if not 1 <= count <= 8:
            raise ValueError("Invalid probe target count")
        urls = tuple(station_fields(reader.text(256).decode("ascii")) for _ in range(count))
        reader.finish()
        if len(set(urls)) != count:
            raise ValueError("Duplicate probe target")
        targets = []
        for url in urls:
            matches = [(other, state) for other in self.adapters for (backend, _), state in other.connections.items()
                if backend and state is not source and state.public_url is not None
                and station_fields(state.public_url) == url
                and self.live(other, state, now) and self.published(state, now)]
            if len(matches) != 1:
                raise ValueError("Probe target is not a live published host")
            targets.append(matches[0])
        keys = [(id(source), id(state)) for _, state in targets]
        if len(self.jobs) + sum(key not in self.jobs for key in keys) > 32:
            raise ValueError("Probe queue full")
        for key, (other, target) in zip(keys, targets):
            if key not in self.jobs:
                self.jobs[key] = Probe(adapter, source, other, target, now + 5)
        adapter.event("native_probe_initiation_queued")

    def pump(self, now):
        for key, job in list(self.jobs.items()):
            if (job.expires <= now or not self.live(job.source_adapter, job.source, now)
                    or not self.live(job.target_adapter, job.target, now) or not self.published(job.target, now)):
                if job.sequence:
                    job.target.pending.pop(job.sequence, None)
                del self.jobs[key]
                continue
            state = job.target
            if job.call or len(state.pending) > 8:
                continue
            if state.next_out >= 0x10000 - 64 or self.next_call > 0xffffffff:
                del self.jobs[key]
                continue
            # CF28C0/C46910 writes the URL list for method 1. CF2530 reads a
            # single StationURL for method 2; CF2580 reads no response values.
            body = b"\x83" + struct.pack("<II", self.next_call, 2) + text(job.source.public_url)
            payload = wrap(struct.pack("<I", len(body)) + body)
            job.call, job.sequence = self.next_call, state.next_out
            self.next_call += 1
            state.next_out += 1
            wire = encode(2, 112, state.session, state.client, job.sequence, payload)
            state.pending[job.sequence] = wire, now, 0
            job.target_adapter.event("native_probe_notification_sent")

    def response(self, adapter, state, message):
        if len(message) != 14 or int.from_bytes(message[:4], "little") != 10 or message[4] != 3:
            raise ValueError("Invalid probe response")
        if message[5] == 1:
            call, method = struct.unpack_from("<II", message, 6)
            if method != 0x8002:
                raise ValueError("Unexpected probe method")
        elif message[5] == 0:
            _error, call = struct.unpack_from("<II", message, 6)
        else:
            raise ValueError("Invalid probe response flag")
        matches = [(key, job) for key, job in self.jobs.items()
            if job.target_adapter is adapter and job.target is state and job.call == call and call != 0]
        if len(matches) != 1:
            raise ValueError("Unsolicited probe response")
        del self.jobs[matches[0][0]]
        adapter.event("native_probe_notification_answered" if message[5] else "native_probe_notification_rejected")
