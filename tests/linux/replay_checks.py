"""Bounded native replay CRC comparison; never emits replay contents or identities."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

HEADER = struct.pack('<5I', 0x44a67f59, 1, 1, 1, 0x10005) + b'\x01Coll'
FIELDS = struct.pack('<4I', 0, 1, 1, 1)


def read_checksums(path, *, max_packed=16*1024*1024, max_raw=128*1024*1024):
    path = Path(path)
    with path.open('rb') as source:
        packed = source.read(max_packed + 1)
    if not packed or len(packed) > max_packed:
        raise ValueError('Replay is empty or exceeds the compressed size limit')
    remaining, chunks, total, members = packed, [], 0, 0
    while remaining:
        if not remaining.startswith(b'\x1f\x8b') or members >= 4096:
            raise ValueError('Unexpected replay trailing data or too many gzip members')
        decoder = zlib.decompressobj(31)
        try:
            raw = decoder.decompress(remaining, max_raw - total + 1)
        except zlib.error as error:
            raise ValueError('Invalid replay gzip member') from error
        total += len(raw)
        if total > max_raw or not decoder.eof or decoder.unconsumed_tail:
            raise ValueError('Replay member is incomplete or exceeds the raw size limit')
        if len(decoder.unused_data) >= len(remaining):
            raise ValueError('Replay decoder made no progress')
        chunks.append(raw); members += 1
        remaining = decoder.unused_data
    # A native replay appends gzip members. Reading only the first member can
    # silently miss later desyncs. Join before parsing, including split records.
    raw = b''.join(chunks); checksums = []
    for match in re.finditer(re.escape(HEADER), raw):
        record = raw[match.start():match.start()+49]
        if len(record) != 49 or record[25:41] != FIELDS or record[45:49] != b'coll':
            raise ValueError('Unsupported or incomplete native checksum record')
        checksums.append(struct.unpack_from('<I', record, 41)[0])
    if not checksums:
        raise ValueError('Replay contains no recognized native checksum records')
    return checksums, dict(records=len(checksums), gzip_members=members,
        compressed_bytes=len(packed), raw_bytes=total, stream_complete=True,
        file_sha256=hashlib.sha256(packed).hexdigest())


def compare(left, right):
    first, first_info = read_checksums(left)
    second, second_info = read_checksums(right)
    common = min(len(first), len(second))
    mismatches = [index for index in range(common) if first[index] != second[index]]
    return dict(left=first_info, right=second_info, common=common,
        mismatches=len(mismatches), first_mismatches=mismatches[:8],
        unmatched_tail=dict(left=len(first)-common, right=len(second)-common),
        all_gzip_members_read=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('left', type=Path); parser.add_argument('right', type=Path)
    args = parser.parse_args()
    try:
        result = compare(args.left, args.right)
    except (OSError, ValueError) as error:
        parser.exit(2, str(error) + '\n')
    # The CLI emits aggregate evidence, without local paths, raw CRC values,
    # game contents, identities or file fingerprints.
    for side in ('left', 'right'): result[side].pop('file_sha256')
    print(json.dumps(result, sort_keys=True))
    return 0 if result['common'] and not result['mismatches'] else 1


if __name__ == '__main__': raise SystemExit(main())
