"""Bounded BBA addition retaining every existing payload and lookup entry.

Format and crypto follow the existing MIT S7Packer-derived bba module. Unlike
its replacement-only API, this also links the terminal entry and inserts at
the native CRC32 slot with linear probing. An arbitrary empty slot is invalid.
"""
import struct
from bba import Archive, crc32, crypt, canonical_gzip, _gzip_xor, FILE_KEYS, HEAD_KEYS


def append_resource(raw, name, data):
    if (not isinstance(name, str) or not name.startswith('Config/Environment/')
            or not name.endswith('.xml') or len(name) > 128
            or any(part in ('', '.', '..') for part in name.split('/'))
            or any(ord(c) < 32 or ord(c) > 126 or c in '\\:' for c in name)
            or not isinstance(data, bytes) or not 0 < len(data) <= 1024 * 1024):
        raise ValueError('Invalid environment resource')
    archive = Archive(raw)
    if name.lower() in {key.lower() for key in archive.entries}:
        raise ValueError('Environment resource already exists')
    directory = bytearray(archive.directory)
    table = archive.hash_table_offset
    count = struct.unpack_from('<I', directory, table)[0]
    spelled = name.replace('/', '\\').encode('ascii')
    hashed = crc32(spelled)
    if not hashed or not count:
        raise ValueError('Invalid resource hash table')
    slot = hashed % count
    for _ in range(count):
        if struct.unpack_from('<I', directory, table + 4 + slot * 8)[0] == 0:
            break
        slot = (slot + 1) % count
    else:
        raise ValueError('Resource hash table is full')
    template = archive.entries['Config/Environment/Neutral.xml']
    archive.read(template.name)
    values = list(template.values)
    size = len(spelled) + 1
    padded = ((size + 3) & ~3) + (4 if size % 4 == 0 else 0)
    encoded = _gzip_xor(canonical_gzip(data), size, True)
    result = bytearray(raw)
    values[0:2] = (0, 0)  # deterministic timestamp, no local identity
    values[2], values[3], values[6] = len(data), crc32(data), len(result)
    values[8], values[9], values[12] = len(encoded), crc32(encoded), size
    values[13] = len('Config/Environment/')
    values[14] = values[15] = 0xffffffff
    entry = struct.pack('<16I', *values) + spelled + b'\0' * (padded - len(spelled))
    last = list(archive.entries.values())[-1]
    link = 14 if last.values[4] == 256 else 15
    if last.values[link] != 0xffffffff:
        raise ValueError('Unknown terminal resource entry')
    struct.pack_into('<I', directory, last.directory_offset + link * 4, table - 276)
    directory[table:table] = entry
    struct.pack_into('<I', directory, 8, table + len(entry))
    struct.pack_into('<I', directory, 272, len(archive.entries) + 1)
    struct.pack_into('<2I', directory, table + len(entry) + 4 + slot * 8, hashed, table - 276)
    result.extend(encoded)
    encoded_directory = crypt(directory, FILE_KEYS, 1532281822, 8, True)
    header = bytearray(archive.header)
    struct.pack_into('<4I', header, 0, len(result), 0, len(encoded_directory), crc32(encoded_directory))
    result[16:16 + archive.header_size] = crypt(header, HEAD_KEYS, 1231853475, 7, True)
    result.extend(encoded_directory)
    verified = Archive(result)
    if verified.read(name) != data or any(verified.entries[key].values[:14] != old.values[:14]
                                         for key, old in archive.entries.items()):
        raise ValueError('Resource addition failed verification')
    return bytes(result)
