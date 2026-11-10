"""Guarded Gold 1.12.1396 offline unlock in a new, separate copy.

No game launch, account/profile editing, downloads or multiplayer admission.
The embedded upstream definitions are used only for their Gold default block.
This is a development tool, not a History Edition installer profile.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import stat
import struct

GOLD_SHA256 = 'ec27c7fab2c83a1444f043eb4730f0065393ca2e91607f71e5fd78ee5a5c7c94'
DEFINITIONS_SHA256 = '98f30124c274c1de15f46e1701a819358394dfeebaa0d9924b1f56596b6b6a7f'
DXVK_SHA256 = '265888c31ca78dffa290c39cb7e50bfb02762590e41927906e46fb32f01497fa'
EXE = Path('Data/Base/_Dbg/Bin/Release/Settlers7R.exe')
DEFINITIONS = Path(__file__).resolve().parents[2] / 'Definitions/Definitions.bin'
DXVK_CONFIG = (b'[Settlers7R.exe]\nd3d9.maxFrameRate = 60\n'
               b'd3d9.floatEmulation = Strict\nd3d9.samplerAnisotropy = 16\n'
               b'd3d9.enumerateByDisplays = False\n')
# Original instruction/data guards from the fingerprinted Gold executable.
# Replacement bytes remain in the existing, separately fingerprinted definitions.
GUARDS = {
    0x00d40d: '881d795b0e01', 0x696dce: '8b542430db4424308b43048b4b08',
    0x1a978e: '74', 0x1a977c: '7512', 0x64477c: '8ac1',
    0x21929c: '8ac1', 0x219224: '32c0', 0x195c34: '7415',
    0x69000f: '95', 0x58bc2e: '00', 0x696d83: '508bcbffd2',
    0x696dc8: '0f840a030000', 0x62f0a9: '0f82f1000000',
    0x2031c5: '74', 0x1ae673: '83e4f86aff6826e1e200', 0x860344: '740a',
}
# The upstream edit bytes replace two existing absolute operands and introduce
# one new absolute global address. Loader relocation must follow those changes.
# These locations belong only to the fully fingerprinted Gold image above.
OLD_RELOCATIONS = ((0xce43f0, 0x300f), (0xcf079a, 0x3279))
RELOCATION_APPEND = 0xd7937c
RELOCATION_BLOCK = struct.pack('<IIHH', 0x697000, 12, 0x39d3, 0)


def repair_relocations(data):
    """Repair reviewed HIGHLOW entries without changing image/security flags."""
    if len(data) < RELOCATION_APPEND + len(RELOCATION_BLOCK):
        raise ValueError('Truncated Gold relocation data.')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if pe + 0xa8 > len(data) or struct.unpack_from('<II', data, pe + 0xa0) != (0xd2c000, 0x9577c):
        raise ValueError('Gold relocation directory guard failed.')
    if any(struct.unpack_from('<H', data, offset)[0] != expected for offset, expected in OLD_RELOCATIONS):
        raise ValueError('Gold original relocation entry guard failed.')
    if any(data[RELOCATION_APPEND:RELOCATION_APPEND + len(RELOCATION_BLOCK)]):
        raise ValueError('Gold relocation append-space guard failed.')
    # ABSOLUTE entries are ignored by the Windows loader. Other existing
    # relocation records and section boundaries remain byte-for-byte intact.
    for offset, _ in OLD_RELOCATIONS:
        struct.pack_into('<H', data, offset, 0)
    data[RELOCATION_APPEND:RELOCATION_APPEND + len(RELOCATION_BLOCK)] = RELOCATION_BLOCK
    struct.pack_into('<I', data, pe + 0xa4, 0x9577c + len(RELOCATION_BLOCK))


def sha256(value):
    return hashlib.sha256(value).hexdigest()


def digest(path):
    with plain(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def plain(path):
    path = Path(path).absolute()
    for item in (path, *path.parents):
        try:
            info = item.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise ValueError('Links/reparse points are not allowed in Gold preparation.')
    return path.resolve()


def source_root(path):
    root = plain(path)
    matches = [candidate for candidate in (root, plain(root / 'Settlers 7'))
               if plain(candidate / EXE).is_file()]
    if len(matches) != 1:
        raise ValueError('Select the Gold installation containing Data/Base/_Dbg/Bin/Release, '
                         'or its S7Gold parent. Ambiguous sources are rejected.')
    return matches[0]


def gold_edits(definitions):
    if sha256(definitions) != DEFINITIONS_SHA256:
        raise ValueError('The embedded Gold definitions fingerprint does not match.')
    data = gzip.decompress(definitions)
    if data[:5] != b'EMX\x01\x00':
        raise ValueError('The Gold definitions header does not match.')
    size = struct.unpack_from('<I', data, 5)[0]
    end = 9 + size
    if end > len(data):
        raise ValueError('Truncated Gold definitions block.')
    pos, edits = 9, {}
    while pos < end:
        if pos + 9 > end:
            raise ValueError('Truncated Gold definition.')
        tag = data[pos:pos + 3]
        offset, length = struct.unpack_from('<IH', data, pos + 3)
        if not length or pos + 9 + length > end:
            raise ValueError('Invalid Gold definition length.')
        if tag == b'\0\0\0':
            if offset in edits:
                raise ValueError('Duplicate Gold definition offset.')
            edits[offset] = data[pos + 9:pos + 9 + length]
        pos += 9 + length
    if edits.keys() != GUARDS.keys() or any(len(value) != len(bytes.fromhex(GUARDS[offset]))
                                           for offset, value in edits.items()):
        raise ValueError('The Gold default patch set does not match the reviewed guards.')
    return edits


def pe_checksum(data):
    """PE32 image checksum; never treat a recalculated checksum as identity."""
    if data[:2] != b'MZ' or len(data) < 64:
        raise ValueError('Invalid Gold PE header.')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if pe + 0x5c > len(data) or data[pe:pe + 6] != b'PE\0\0\x4c\x01':
        raise ValueError('Expected an x86 Gold executable.')
    if data[pe + 24:pe + 26] != b'\x0b\x01':
        raise ValueError('Expected a PE32 Gold executable.')
    offset = pe + 0x58
    if offset & 1:
        raise ValueError('Unaligned Gold PE header.')
    view = memoryview(data)
    even_length = len(data) & ~1
    total = sum(word[0] for word in struct.iter_unpack('<H', view[:offset]))
    total += sum(word[0] for word in struct.iter_unpack('<H', view[offset + 4:even_length]))
    if len(data) & 1:
        total += data[-1]
    while total >> 16:
        total = (total & 0xffff) + (total >> 16)
    return offset, (total & 0xffff) + len(data)


def unlock_bytes(original, definitions):
    if sha256(original) != GOLD_SHA256:
        raise ValueError('Unknown or modified Gold executable. Expected Ubisoft Gold 1.12.1396.')
    edits = gold_edits(definitions)
    for offset, value in edits.items():
        before = bytes.fromhex(GUARDS[offset])
        if original[offset:offset + len(before)] != before:
            raise ValueError('Gold original-byte guard failed at ' + hex(offset))
    result = bytearray(original)
    for offset, value in edits.items():
        result[offset:offset + len(value)] = value
    repair_relocations(result)
    checksum_offset, checksum = pe_checksum(result)
    struct.pack_into('<I', result, checksum_offset, checksum)
    return bytes(result)


def inspect(source, definitions=DEFINITIONS):
    root = source_root(source)
    raw = plain(root / EXE).read_bytes()
    patched = unlock_bytes(raw, plain(definitions).read_bytes())
    return {'format': 's7-gold-inspection-v1', 'edition': 'gold-ubisoft-1.12.1396',
            'source_exe_sha256': sha256(raw), 'unlocked_exe_sha256': sha256(patched),
            'unlock_edits': len(GUARDS), 'weplay_adapter_available': False,
            'loader_relocations_repaired': True,
            'crossplay_verified': False, 'graphics_verified': False,
            'profile_changes': False, 'game_launched': False}


def prepare(source, destination, *, definitions=DEFINITIONS, dxvk=None):
    source, destination = source_root(source), plain(destination)
    # Both directions matter: a target ancestor could otherwise consume source data.
    if destination.is_relative_to(source) or source.is_relative_to(destination):
        raise ValueError('The Gold copy must be separate from the original installation.')
    if destination.exists():
        raise ValueError('The Gold destination already exists; select a new directory.')
    report = inspect(source, definitions)
    patched = unlock_bytes(plain(source / EXE).read_bytes(), plain(definitions).read_bytes())
    if dxvk is not None and digest(dxvk) != DXVK_SHA256:
        raise ValueError('The selected DXVK DLL fingerprint does not match the pinned x86 build.')
    destination.mkdir(parents=True, exist_ok=False)
    incomplete = destination / 'PREPARATION-INCOMPLETE.txt'
    incomplete.write_text('Gold development copy is incomplete. Do not launch.\n', encoding='utf-8')
    records = {}
    # Copy only game data, excluding launcher download/state and account directories.
    stack = [plain(source / 'Data')]
    while stack:
        folder = stack.pop()
        for item in sorted(folder.iterdir()):
            item = plain(item)
            relative = item.relative_to(source)
            if item.is_dir():
                stack.append(item)
            elif item.is_file():
                output = plain(destination / relative)
                output.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(item, output)
                original_hash = digest(item)
                if digest(output) != original_hash:
                    raise ValueError('Gold copy verification failed: ' + relative.as_posix())
                records[relative.as_posix()] = original_hash
            else:
                raise ValueError('Unsupported Gold source file type.')
    original_exe = plain(destination / EXE)
    if digest(original_exe) != GOLD_SHA256:
        raise ValueError('Gold executable changed during preparation.')
    backup = destination / 'Backup/Settlers7R.original.exe'
    backup.parent.mkdir()
    shutil.copy2(original_exe, backup)
    original_exe.write_bytes(patched)
    if digest(original_exe) != report['unlocked_exe_sha256']:
        raise ValueError('Gold unlock output verification failed.')
    report.update(format='s7-gold-development-copy-v1', source_files=records,
                  definitions_sha256=DEFINITIONS_SHA256, dxvk_staged=dxvk is not None,
                  shared_profile_isolated=False, starter_created=False)
    if dxvk is not None:
        dll = original_exe.with_name('d3d9.dll')
        config = original_exe.with_name('dxvk.conf')
        if dll.exists() or config.exists():
            raise ValueError('An existing graphics proxy/configuration must not be overwritten.')
        shutil.copy2(plain(dxvk), dll)
        if digest(dll) != DXVK_SHA256:
            raise ValueError('DXVK copy verification failed.')
        config.write_bytes(DXVK_CONFIG)
        report.update(dxvk_sha256=DXVK_SHA256, dxvk_config_sha256=sha256(DXVK_CONFIG))
    (destination / 'gold-development.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    (destination / 'README.txt').write_text(
        'WePlay Gold development copy\n\n'
        'Gold offline unlock applied; original executable retained in Backup.\n'
        'No WePlay multiplayer adapter is available for this executable.\n'
        'Crossplay and modern graphics have not been qualified.\n'
        'No starter is created: isolate/back up the actual profile and saves before testing.\n'
        'Do not use the History Edition installer or native adapter on this copy.\n', encoding='utf-8')
    incomplete.unlink()
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    check = sub.add_parser('inspect')
    check.add_argument('source', type=Path)
    stage = sub.add_parser('prepare')
    stage.add_argument('source', type=Path)
    stage.add_argument('destination', type=Path)
    stage.add_argument('--dxvk', type=Path, help='Optional existing pinned x86 DXVK DLL; no download.')
    args = parser.parse_args(argv)
    try:
        report = (inspect(args.source) if args.command == 'inspect' else
                  prepare(args.source, args.destination, dxvk=args.dxvk))
    except (OSError, ValueError) as error:
        print(json.dumps({'error': str(error), 'crossplay_verified': False}))
        return 2
    print(json.dumps({key: value for key, value in report.items() if key != 'source_files'}, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
