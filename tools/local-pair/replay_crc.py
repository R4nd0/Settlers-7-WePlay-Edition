"""Compare original History replay checksums from one shared save reload."""

import argparse
import gzip
import json
import re
import struct
from pathlib import Path

MAX_REPLAY_BYTES = 64 * 1024 * 1024
CHECKSUM_EVENT = struct.pack('<I', 0x44A67F59)


def checksums(path):
    with gzip.open(path, 'rb') as stream:
        data = stream.read(MAX_REPLAY_BYTES + 1)
    if not data.startswith(b'bin1') or len(data) > MAX_REPLAY_BYTES:
        raise ValueError('Expected a bounded native History replay')
    values = []
    for match in re.finditer(re.escape(CHECKSUM_EVENT), data):
        at = match.start()
        record = data[at:at + 49]
        if len(record) != 49 or struct.unpack_from('<4I', record, 4) != (1, 1, 1, 0x10005):
            continue
        if record[20:25] != b'\x01Coll' or record[45:] != b'coll':
            continue
        if struct.unpack_from('<4I', record, 25) != (0, 1, 1, 1):
            continue
        values.append(struct.unpack_from('<I', record, 41)[0])
    return values


def compare(paths):
    if len(paths) != 3 or len({path.resolve() for path in paths}) != 3:
        raise ValueError('Supply the three distinct replay files from the same reload')
    values = [checksums(path) for path in paths]
    common = min(map(len, values))
    differences = [index for index, turn in enumerate(zip(*values)) if len(set(turn)) != 1]
    return {
        'checksums_per_replay': [len(row) for row in values],
        'common_checksums': common,
        'mismatches': len(differences),
        'first_mismatch_record': differences[0] if differences else None,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('replays', nargs=3, type=Path)
    args = parser.parse_args()
    result = compare(args.replays)
    print(json.dumps(result, indent=2))
    return int(result['common_checksums'] == 0 or result['mismatches'] != 0)


if __name__ == '__main__':
    raise SystemExit(main())
