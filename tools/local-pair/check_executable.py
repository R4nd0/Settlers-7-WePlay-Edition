"""Read-only validation against a user-supplied original; writes no game files."""
import argparse
from pathlib import Path
import lab_executable


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('original', type=Path)
    args = parser.parse_args()
    original = args.original.read_bytes()
    allowed = set(range(0xD0EC24, 0xD0EC24 + 17)) | set(range(0xD31594, 0xD315B8))
    allowed |= {offset + len(before) - 1 for offset, before, _ in lab_executable.LENGTH_EDITS}
    checks = 0
    for slot in ('A', 'B'):
        candidate = lab_executable.derive(original, slot)
        assert len(candidate) == len(original)
        assert {i for i, (a, b) in enumerate(zip(original, candidate)) if a != b} <= allowed
        assert candidate[0xD31594:0xD315B8] == ('\\Claude\\Projects\\7S_Patcher\\work\\' + slot).encode('ascii') + b'\0' * 2
        assert candidate[0xD315B8:0xD315C0] == original[0xD315B8:0xD315C0] == b'\\DLC' + b'\0' * 4
        checks += 4
        for offset, before, after in lab_executable.LENGTH_EDITS:
            assert original[offset:offset+len(before)] == before
            assert candidate[offset:offset+len(after)] == after
            assert before[:-1] == after[:-1] and len(before) == len(after)
            checks += 3
        for value, name in ((candidate, slot), (original[:-1], slot),
                            (bytes([original[0] ^ 1]) + original[1:], slot), (original, 'C')):
            try:
                lab_executable.derive(value, name)
            except ValueError:
                checks += 1
            else:
                raise AssertionError('Executable guard accepted an invalid input')
    assert args.original.read_bytes() == original
    print(f'PASS {checks + 1} real executable derivation/immutability checks')


if __name__ == '__main__':
    main()
