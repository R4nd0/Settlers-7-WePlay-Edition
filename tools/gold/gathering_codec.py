"""Bounded Gold gathering wire shapes, without session admission.

The seven u32/two u16 fields follow the original Gold reader/writer order.
Unresolved field semantics and the dynamic attribute bytes remain opaque.
Decoding this record does not authorize its identifiers, members or content.
"""
from dataclasses import dataclass
import struct

CLASS = "DynamicGathering"
MAX_DESCRIPTION = 1024
MAX_ATTRIBUTES = 8192
MAX_RECORD = 12 * 1024
SCALARS = struct.Struct("<IIIHHIIII")


@dataclass(frozen=True)
class Gathering:
    words: tuple[int, int, int, int, int, int, int]
    shorts: tuple[int, int]
    description: str | None
    attributes: bytes


@dataclass(frozen=True)
class SessionGathering:
    base: Gathering
    strings: tuple[str | None, str | None, str | None, str | None]
    words: tuple[int, int, int, int, int, int]
    flags: tuple[bool, bool, bool, bool]
    matchmaking_word: int | None = None


def _text(reader, maximum):
    size = int.from_bytes(reader.take(2), "little")
    if size == 0:
        return None  # Native QString's null pointer is distinct from L"".
    if not 1 <= size <= maximum + 1:
        raise ValueError("Gathering string bound")
    data = reader.take(size)
    if data[-1] or b"\0" in data[:-1]:
        raise ValueError("Gathering string terminator")
    return data[:-1].decode("utf-8")


def _string(value, maximum):
    if value is None:
        return b"\0\0"
    if not isinstance(value, str) or "\0" in value:
        raise ValueError("Invalid gathering string")
    data = value.encode("utf-8") + b"\0"
    if len(data) > maximum + 1:
        raise ValueError("Gathering string bound")
    return struct.pack("<H", len(data)) + data


def decode(data: bytes, reader_type) -> Gathering | SessionGathering:
    if not 1 <= len(data) <= MAX_RECORD:
        raise ValueError("Gathering record bound")
    reader = reader_type(data)
    kind = _text(reader, 64)
    if kind not in (CLASS, "S7Session", "S7SessionMM"):
        raise ValueError("Unsupported gathering class")
    size = reader.u32()
    if not SCALARS.size + 6 <= size <= MAX_RECORD:
        raise ValueError("Gathering body bound")
    body = reader_type(reader.take(size))
    reader.finish()
    if body.u32() != size - 4:
        raise ValueError("Gathering stream length mismatch")
    a, b, c, short_a, short_b, d, e, f, g = SCALARS.unpack(body.take(SCALARS.size))
    description = _text(body, MAX_DESCRIPTION)
    base = Gathering((a, b, c, d, e, f, g), (short_a, short_b), description, b"")
    if kind != CLASS:
        strings = tuple(_text(body, MAX_DESCRIPTION) for _ in range(3))
        words = tuple(body.u32() for _ in range(4))
        strings += (_text(body, MAX_DESCRIPTION),)

        def flag():
            value = body.take(1)[0]
            if value > 1:
                raise ValueError("Noncanonical gathering boolean")
            return bool(value)

        flags = tuple(flag() for _ in range(3))
        words += (body.u32(), body.u32())
        flags += (flag(),)
        extra = body.u32() if kind == "S7SessionMM" else None
        body.finish()
        return SessionGathering(base, strings, words, flags, extra)
    remaining = size - body.position
    if remaining > MAX_ATTRIBUTES:
        raise ValueError("Gathering attribute bound")
    attributes = bytes(body.take(remaining))
    body.finish()
    return Gathering((a, b, c, d, e, f, g), (short_a, short_b), description, attributes)


def _base_bytes(value: Gathering) -> bytes:
    if (len(value.words) != 7 or len(value.shorts) != 2
            or any(type(n) is not int or not 0 <= n <= 0xffffffff for n in value.words)
            or any(type(n) is not int or not 0 <= n <= 0xffff for n in value.shorts)
            or not isinstance(value.attributes, bytes) or len(value.attributes) > MAX_ATTRIBUTES):
        raise ValueError("Gathering field bound")
    a, b, c, d, e, f, g = value.words
    body = SCALARS.pack(a, b, c, *value.shorts, d, e, f, g)
    return body + _string(value.description, MAX_DESCRIPTION)


def encode(value: Gathering | SessionGathering) -> bytes:
    kind = CLASS
    if isinstance(value, SessionGathering):
        if (value.base.attributes or len(value.strings) != 4 or len(value.words) != 6
                or len(value.flags) != 4 or any(type(n) is not bool for n in value.flags)
                or any(type(n) is not int or not 0 <= n <= 0xffffffff for n in value.words)):
            raise ValueError("Session gathering field bound")
        body = _base_bytes(value.base)
        body += b"".join(_string(s, MAX_DESCRIPTION) for s in value.strings[:3])
        body += struct.pack("<IIII", *value.words[:4]) + _string(value.strings[3], MAX_DESCRIPTION)
        body += bytes(value.flags[:3]) + struct.pack("<II", *value.words[4:]) + bytes(value.flags[3:])
        kind = "S7Session"
        if value.matchmaking_word is not None:
            if type(value.matchmaking_word) is not int or not 0 <= value.matchmaking_word <= 0xffffffff:
                raise ValueError("Matchmaking gathering field bound")
            kind = "S7SessionMM"
            body += struct.pack("<I", value.matchmaking_word)
    else:
        body = _base_bytes(value) + value.attributes
    # Native OutputStream reserves a u32 and C646D0 finalizes it before the
    # holder writes its own buffer length. Both lengths are mandatory.
    body = struct.pack("<I", len(body)) + body
    data = _string(kind, 64) + struct.pack("<I", len(body)) + body
    if len(data) > MAX_RECORD:
        raise ValueError("Gathering record bound")
    return data
