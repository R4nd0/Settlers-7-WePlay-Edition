"""Fresh, in-memory Gold laboratory profile; no vendor account or entitlement.

This implements the native account-startup RPC schemas. Progression evaluation,
earned titles, persistent profiles and game-session admission are not provided.
Opaque castle/progression data stays in the owning process and is never logged.
"""
import struct

MAX_BLOB = 32768
MAX_STRING = 4096


def string(value: str) -> bytes:
    data = value.encode("utf-8") + b"\0"
    if len(data) > MAX_STRING + 1:
        raise ValueError("Profile string bound")
    return struct.pack("<H", len(data)) + data


def read_string(reader) -> str:
    size = int.from_bytes(reader.take(2), "little")
    if not 1 <= size <= MAX_STRING + 1:
        raise ValueError("Profile string bound")
    data = reader.take(size)
    if data[-1] or b"\0" in data[:-1]:
        raise ValueError("Profile string terminator")
    return data[:-1].decode("utf-8")


def boolean(reader) -> bool:
    data = reader.take(1)
    if data not in (b"\0", b"\1"):
        raise ValueError("Profile boolean")
    return bool(data[0])


def profile(reader, identity: str) -> bytes:
    start = reader.position
    for _ in range(3):
        read_string(reader)
    boolean(reader)
    reader.take(16)
    count = reader.u32()
    if count > 64:
        raise ValueError("Profile list bound")
    for _ in range(count):
        read_string(reader)
    reader.take(12)
    if read_string(reader) != identity:
        raise ValueError("Profile belongs to another local identity")
    boolean(reader)
    read_string(reader)
    if reader.position - start > 8192:
        raise ValueError("Profile record bound")
    return reader.data[start:reader.position]


def blob(reader) -> bytes:
    size = reader.u32()
    if size > MAX_BLOB:
        raise ValueError("Profile buffer bound")
    return reader.take(size)


class LocalProfile:
    def __init__(self, identity: str):
        if not 1 <= len(identity) <= 64 or not identity.isascii() or "\0" in identity:
            raise ValueError("Expected an independent local identity")
        self.identity = identity
        empty = string("")
        self.base = (string(identity) + empty * 2 + b"\0" + struct.pack("<IIIIIIQ", 0, 0, 0, 0, 0, 0, 0)
                     + string(identity) + b"\0" + empty)
        self.castle = b""
        self.progress = b""
        self.title_option = 0

    def response(self, protocol: int, method: int, args: bytes, reader_type) -> tuple[bytes, str] | None:
        reader = reader_type(args)
        if (protocol, method) == (111, 2):
            reader.take(12)  # Three native scalar query selectors.
            reader.finish()
            return self.base + struct.pack("<I", len(self.castle)) + self.castle, "profile_read"
        if (protocol, method) == (111, 5):
            base = profile(reader, self.identity)
            castle = blob(reader)
            reader.finish()
            self.base, self.castle = base, castle
            return self.base, "profile_stored"
        if (protocol, method) == (111, 7):
            progress = blob(reader)
            reader.finish()
            self.progress = progress
            return b"", "progress_stored"
        if (protocol, method) == (117, 3):
            # Gold serializes the user's +88 title option as an opaque u8,
            # not a canonical boolean. Preserve the full native byte domain.
            option = reader.take(1)[0]
            reader.finish()
            self.title_option = option
            return b"", "title_option_stored"
        if (protocol, method) in ((111, 8), (111, 9), (117, 1), (118, 1)):
            reader.finish()
            if protocol == 117:
                return bytes((self.title_option,)) + struct.pack("<I", 0), "titles_read"
            if protocol == 118:
                card = (struct.pack("<II", 0, 0) + string(self.identity) + string("") + b"\0"
                        + struct.pack("<II", 0, 0) + b"\0" * 6)
                return card, "gamer_card_read"
            if method == 8:
                return struct.pack("<I", len(self.progress)) + self.progress, "progress_read"
            # Five native option maps. This laboratory account starts empty;
            # interpreting progression into these maps is a separate feature.
            return struct.pack("<IIIII", 0, 0, 0, 0, 0), "profile_options_read"
        return None
