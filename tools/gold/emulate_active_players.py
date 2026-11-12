"""Audit original active-player writers in bounded offline x86 emulation.

Runs the original row serializer twice: against a recording visitor (including
nested nodes) and the original binary writer with a modeled, bounded stream.
The heap boundary is modeled; the original bitfield expansion still executes.
Synthetic rows are not live preparation, save-reader or world-start evidence.
Owned game images are fingerprinted and never copied into the public receipt.
"""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import struct
import pefile

from emulate_event_payloads import Image, PROFILES, WRITER
from verify_codec_guards import check_image

# Independently located using original field tags/callers, not a global shift.
LAYOUTS = {
    'gold': (0x520d50, 0x5c4, 0xbae4fa, 0xbae7f8),
    'ubisoft': (0x58eb90, 0x5a8, 0xa22bfd, 0xa22d64),
    'steam_analysis': (0x58e650, 0x5a8, 0xa2217d, 0xa222e4),
}
PREPARATION_WRITERS = {'gold': 0x520a20, 'ubisoft': 0x58e910, 'steam_analysis': 0x58e3d0}
S, STACK = 0x20000000, 0x30000000
ROWS, VISITOR, TABLE, STOP, STUB = (S+x for x in (0x10000, 0x2000, 0x3000, 0x4000, 0x5000))
STREAM, STREAM_TABLE, POSITION, WRITE = (S+x for x in (0x6000, 0x6100, 0x6200, 0x6210))
ARENA, HEAP = S+0x20000, S+0x50000
WIDTHS = {0x2c: 16, 0x34: 4, 0x3c: 1, 0x44: 4, 0x54: 4, 0x58: 1, 0x60: 4}


def survey(image, version, rich, binary, fixture=None, *, preparation=False, long_allies=False):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_READ
    from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX,
        UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_EIP)
    if (version not in range(1, 10) or type(rich) is not bool or type(binary) is not bool
            or type(preparation) is not bool or type(long_allies) is not bool
            or long_allies and (not preparation or not rich)):
        raise ValueError('Unsupported synthetic fixture')
    gold = image.label == 'gold'
    entry, stride, allocate, free = LAYOUTS[image.label]
    if preparation:
        entry = PREPARATION_WRITERS[image.label]
    row = ROWS+stride  # Deliberately exercise slot one, not only slot zero.
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(image.base, (image.size+4095)&~4095)
    u.mem_write(image.base, image.mapped)
    u.mem_map(0, 4096)  # Emulated SEH head only.
    u.mem_map(S, 0x60000)
    u.mem_map(STACK, 0x10000)
    base, fixture_size = 0, 0
    if fixture is not None:
        base, fixture_size = fixture.OPTIONAL_HEADER.ImageBase, fixture.OPTIONAL_HEADER.SizeOfImage
        u.mem_map(base, (fixture_size+4095)&~4095)
        u.mem_write(base, fixture.get_memory_mapped_image())
        oracle = b'WePlayPreparationWriteOracle' if preparation else b'WePlayActivePlayerWriteOracle'
        entry = next(base+e.address for e in fixture.DIRECTORY_ENTRY_EXPORT.symbols if e.name == oracle)

    def words(at, *values):
        u.mem_write(at, struct.pack('<'+'I'*len(values), *values))

    def word(at):
        return struct.unpack('<I', u.mem_read(at, 4))[0]

    def name(at):
        if not image.base <= at <= image.base+image.size-128:
            raise RuntimeError('Unowned field tag')
        data = bytes(u.mem_read(at, 128))
        end = data.find(b'\0')
        if end < 0 or any(not 32 <= c < 127 for c in data[:end]):
            raise RuntimeError('Invalid field tag')
        return data[:end].decode('ascii')

    cursor = ARENA

    def owned(data):
        nonlocal cursor
        if not data or cursor+len(data) > HEAP:
            raise RuntimeError('Synthetic arena bound')
        at = cursor
        u.mem_write(at, data)
        cursor += (len(data)+15)&~15
        return at

    def string(at, value):
        data = value.encode('ascii')+b'\0'
        pointer = owned(data)
        words(at, pointer, len(data), len(data)-1, 0)

    def vector(at, data):
        pointer = owned(data)
        words(at, pointer, pointer+len(data), pointer+len(data))

    def linked(offset, values, target=row):
        # Gold has the debug iterator proxy before its head/count pair.
        head = owned(bytes(12))
        nodes = [owned(struct.pack('<IIB', 0, 0, value)+bytes(3)) for value in values]
        for index, node in enumerate(nodes):
            words(node, nodes[index+1] if index+1 < len(nodes) else head,
                  nodes[index-1] if index else head)
        words(head, nodes[0] if nodes else head, nodes[-1] if nodes else head)
        words(target+offset+(4 if gold else 0), head, len(nodes))
        return head

    # Field offsets confirmed by original serializer/destructor pairs. These
    # three containers do not share one Gold-to-History offset difference.
    list_offsets = (0x4e4, 0x4f0, 0x568) if gold else (0x4e0, 0x4e8, 0x554)
    if preparation:
        # Original whole-preparation writers visit all 25 rows, including
        # closed slots. Every row owns valid empty list sentinels.
        for number in range(25):
            if number != 1:
                for offset in list_offsets:
                    values = tuple(i%25 for i in range(26)) if long_allies and number in (21, 24) and offset == list_offsets[2] else ()
                    linked(offset, values, ROWS+number*stride)
        linked(0x907c if gold else 0x8dbc, (0, 1) if rich else (), ROWS)
        linked(0x9088 if gold else 0x8dc4, (2, 24) if rich else (), ROWS)
        u.mem_write(ROWS+(0x9078 if gold else 0x8db8), bytes([int(rich)]))
    for offset in list_offsets:
        linked(offset, (1, 4) if rich else ())
    symbols = owned(bytes(32))
    string(symbols, '')
    string(symbols+16, 'Synthetic Color')
    symbol_array = owned(struct.pack('<2I', symbols, symbols+16))
    symbol_pool = owned(bytes(12))  # Opaque to the original binary writer.
    symbol_registry = owned(struct.pack('<4I', symbol_array, symbol_pool, 2, 2))
    symbol_table = ROWS+(0x9024 if gold else 0x8d68)
    words(symbol_table+0x18, symbol_registry)
    if rich:
        # All values are synthetic. Profile identities and engine object IDs
        # are not mapped or normalized by this audit.
        for offset, value in ((0, 101), (0x18, 202), (0x2c, 1), (0x30, 3), (0x3c, 37),
                              (0x40, 11), (0x44, 500), (0x48, 29), (0x4c, 31),
                              (0x50, 701), (0x54, 702), (0x5c, 1), (0x60, 3),
                              (0x64, 1), (0x6c, 2)):
            words(row+offset, value)
        u.mem_write(row+0x38, b'\1\1')
        u.mem_write(row+0x58, b'\1')
        u.mem_write(row+0x68, b'\2')
        u.mem_write(row+0x70, b'\1')
        string(row+0x1c, 'Synthetic Player')
        # Bit boundaries deliberately cross both 32- and 64-bit words.
        for offset, bits in ((0x84 if gold else 0x80, (0, 31, 32, 63, 64, 255, 511)),
                             (0xc4 if gold else 0xc0, (0, 31, 32, 63, 127, 255))):
            data = bytearray(64 if len(bits) == 7 else 32)
            for bit in bits:
                data[bit//8] |= 1 << (bit%8)
            u.mem_write(row+offset, bytes(data))
        vector(row+(0x78 if gold else 0x74), struct.pack('<2I', 17, 23))
        vector(row+(0x52c if gold else 0x51c), struct.pack('<6I', 81, 82, 83, 91, 92, 93))
        vector(row+(0x558 if gold else 0x544), b'\1\0\1')
        castle = row+(0x574 if gold else 0x55c)
        string(castle+(0 if gold else 4), 'Synthetic Castle')
        string(castle+(0x10 if gold else 0x14), 'Synthetic Type')
        part_strings = owned(bytes(32))
        string(part_strings, 'Synthetic Part A')
        string(part_strings+16, 'Synthetic Part B')
        words(castle+0x24, part_strings, part_strings+32, part_strings+32)
        vector(castle+(0x34 if gold else 0x30), struct.pack('<2i', -1, 0))
        vector(castle+(0x44 if gold else 0x3c), struct.pack('<2i', 2, 5))

    words(VISITOR, image.tables[WRITER][0] if binary else TABLE, 0, STREAM, 0)
    words(STREAM, STREAM_TABLE)
    words(STREAM_TABLE+0x14, POSITION)
    words(STREAM_TABLE+0x28, WRITE)
    for index in range(39):
        words(TABLE+index*4, STUB+index*16)
    # Include all row headers, original list heads and pointed-to fixture data
    # in preservation checks. No CPU writes to any of these ranges are allowed.
    original_rows = bytes(u.mem_read(ROWS, 0x9094))
    original_arena = bytes(u.mem_read(ARENA, cursor-ARENA))
    fields, output, allocations = [], bytearray(), {}
    allocation_count, completed = 0, False
    row_slots = []
    scopes = []

    def returned(count, value=0):
        sp = u.reg_read(UC_X86_REG_ESP)
        u.reg_write(UC_X86_REG_EAX, value)
        u.reg_write(UC_X86_REG_EIP, word(sp))
        u.reg_write(UC_X86_REG_ESP, sp+4+count)

    def span(pointer, size):
        return (ROWS <= pointer <= pointer+size <= ROWS+0x9094
                or ARENA <= pointer <= pointer+size <= cursor
                or STACK <= pointer <= pointer+size <= STACK+0x10000
                or image.base <= pointer <= pointer+size <= image.base+image.size
                or any(p <= pointer <= pointer+size <= p+n for p, n in allocations.items()))

    def code(uc, at, size, data):
        nonlocal completed, allocation_count
        if at == STOP:
            completed = True
            uc.emu_stop()
        elif preparation and at == LAYOUTS[image.label][0]:
            sp = uc.reg_read(UC_X86_REG_ESP)
            # Both native callees consume a byte slot. Their original caller
            # may leave unrelated bytes in the remainder of the stack word.
            number = word(sp+(4 if gold else 8))&0xff
            if (number != len(row_slots) or number >= 25
                    or uc.reg_read(UC_X86_REG_ECX) != ROWS
                    or (uc.reg_read(UC_X86_REG_EDI) if gold else word(sp+4)) != VISITOR
                    or word(sp+(8 if gold else 12)) != version):
                raise RuntimeError('Original preparation row dispatch differs')
            row_slots.append(number)
        elif at == allocate:
            length = word(uc.reg_read(UC_X86_REG_ESP)+4)
            if length not in (256, 512) or allocation_count >= (50 if preparation else 2):
                raise RuntimeError('Unexpected allocation')
            pointer = HEAP+allocation_count*0x400
            allocation_count += 1
            allocations[pointer] = length
            returned(0, pointer)
        elif at == free:
            pointer = word(uc.reg_read(UC_X86_REG_ESP)+4)
            if pointer not in allocations:
                raise RuntimeError('Unowned/double free')
            del allocations[pointer]
            returned(0)
        elif binary and at in (POSITION, WRITE):
            if uc.reg_read(UC_X86_REG_ECX) != STREAM:
                raise RuntimeError('Foreign stream')
            if at == POSITION:
                returned(0, len(output))
            else:
                sp = uc.reg_read(UC_X86_REG_ESP)
                pointer, length = word(sp+4), word(sp+8)
                if length > 4096 or len(output)+length > 65536 or not span(pointer, length):
                    raise RuntimeError('Stream bounds/source')
                output.extend(uc.mem_read(pointer, length))
                returned(8)
        elif not binary and STUB <= at < STUB+39*16 and (at-STUB)%16 == 0:
            index, sp = (at-STUB)//16, uc.reg_read(UC_X86_REG_ESP)
            if uc.reg_read(UC_X86_REG_ECX) != VISITOR:
                raise RuntimeError('Foreign visitor')
            if index == 1:
                tag = name(word(sp+4))
                fields.append({'kind': 'node', 'name': tag})
                scopes.append('node')
                returned(16, 1)
            elif index in (2, 4):
                kind = 'node' if index == 2 else 'container'
                if not scopes or scopes.pop() != kind:
                    raise RuntimeError('Unbalanced serialization scopes')
                fields.append({'kind': kind+'_end'})
                returned(0)
            elif index == 3:
                count = word(sp+8)
                tag = name(word(sp+4))
                if count > 3 and not (preparation and not scopes and count == 25
                        or long_allies and tag == 'Allies' and count == 26 and row_slots[-1] in (21, 24)):
                    raise RuntimeError('Unexpected fixture container length')
                fields.append({'kind': 'container', 'name': tag, 'count': count})
                scopes.append('container')
                returned(12, count)
            elif index*4 == 0x74:
                pointer, length = word(sp+4), word(sp+8)
                if allocations.get(pointer) != length:
                    raise RuntimeError('Unowned bitfield buffer')
                bits = [i for i, byte in enumerate(u.mem_read(pointer, length)) if byte]
                populated = rich and (not preparation or row_slots == [0, 1])
                expected = ([0, 31, 32, 63, 64, 255, 511] if length == 512 else [0, 31, 32, 63, 127, 255]) if populated else []
                if bits != expected:
                    raise RuntimeError('Original bitfield expansion differs')
                fields.append({'kind': 'bitfield', 'name': name(word(sp+12)), 'length': length, 'set_bits': bits})
                returned(16)
            elif index*4 in WIDTHS:
                slot, pointer = index*4, word(sp+4)
                if slot == 0x60 and (word(sp+8) != symbol_table or word(sp+16) != 0x10):
                    raise RuntimeError('Unowned symbolic table/flags')
                width = WIDTHS[slot]
                if not (row <= pointer <= row+stride-width
                        or preparation and ROWS <= pointer <= ROWS+0x9094-width
                        or STACK <= pointer <= STACK+0x10000-width
                        or ARENA <= pointer <= cursor-width):
                    raise RuntimeError('Foreign field storage')
                offset = pointer-row if row <= pointer <= row+stride-width else None
                if preparation:
                    offset = (pointer-ROWS)%stride if ROWS <= pointer <= ROWS+25*stride-width else None
                fields.append({'kind': 'field', 'name': name(word(sp+(12 if slot == 0x60 else 8))),
                               'visitor_slot': slot, 'offset': offset})
                returned(16 if slot == 0x60 else 12)
            else:
                raise RuntimeError('Unmodeled visitor slot '+str(index))
        elif not (image.base <= at < image.base+image.size or base <= at < base+fixture_size):
            raise RuntimeError('Foreign code')

    def write(uc, access, at, size, value, data):
        if not (at == 0 and size == 4 or STACK <= at <= at+size <= STACK+0x10000
                or binary and VISITOR+0xc <= at <= at+size <= VISITOR+0x214
                or any(p <= at <= at+size <= p+n for p, n in allocations.items())):
            raise RuntimeError('Unexpected object/global write')

    def read(uc, access, at, size, value, data):
        if at < 4096 and (at != 0 or size != 4):
            raise RuntimeError('Unexpected null-page read')

    u.hook_add(UC_HOOK_CODE, code)
    u.hook_add(UC_HOOK_MEM_WRITE, write)
    u.hook_add(UC_HOOK_MEM_READ, read)
    sp = STACK+0xf000
    arguments = (STOP, 1, version) if gold else (STOP, VISITOR, 1, version)
    if preparation:
        arguments = (STOP, ROWS, version) if gold else (STOP, VISITOR, version)
    if fixture is not None:
        arguments = (STOP, tuple(PROFILES).index(image.label)+1, ROWS, VISITOR, 1, version)
        if preparation:
            arguments = (STOP, tuple(PROFILES).index(image.label)+1, ROWS, VISITOR, version)
    words(sp, *arguments)
    kept = {UC_X86_REG_EBX: 0x12345678, UC_X86_REG_ESI: 0x34567890,
            UC_X86_REG_EDI: VISITOR if gold and fixture is None and not preparation else 0x456789ab, UC_X86_REG_EBP: 0x23456789}
    registers = {**kept, UC_X86_REG_ECX: ROWS, UC_X86_REG_ESP: sp}
    if preparation and gold:
        registers[UC_X86_REG_EAX] = VISITOR
    for reg, value in registers.items():
        u.reg_write(reg, value)
    u.emu_start(entry, STOP+1, count=6250000 if preparation else 250000,
                timeout=15_000_000 if preparation else 3_000_000)
    if (not completed or u.reg_read(UC_X86_REG_EIP) != STOP or scopes or allocations
            or allocation_count != (50 if preparation else 2) or word(0) != 0
            or preparation and row_slots != list(range(25))
            or u.reg_read(UC_X86_REG_ESP) != sp+(4 if fixture is not None else 12 if gold or preparation else 16)
            or any(u.reg_read(k) != v for k, v in kept.items())
            or bytes(u.mem_read(ROWS, 0x9094)) != original_rows
            or bytes(u.mem_read(ARENA, cursor-ARENA)) != original_arena
            or binary and word(VISITOR+0xc)):
        raise RuntimeError('Incomplete writer or changed ABI/input state')
    return {'fields': fields} if not binary else {
        'length': len(output), 'sha256': hashlib.sha256(output).hexdigest()}


def compare(profiles):
    if set(profiles) != set(PROFILES):
        raise ValueError('All three pinned profiles are required')
    cases = {f'v{version}-{kind}' for version in range(1, 10) for kind in ('empty', 'populated')}
    if any(set(profile) != cases for profile in profiles.values()):
        raise ValueError('All format/fixture cases are required')
    for case, gold in profiles['gold'].items():
        signature = lambda fields: [{k: v for k, v in f.items() if k != 'offset'} for f in fields]
        for label in ('ubisoft', 'steam_analysis'):
            other = profiles[label][case]
            if signature(gold['fields']) != signature(other['fields']) or gold['binary'] != other['binary']:
                raise RuntimeError('Original writer differs: '+label+'/'+case)
            if label == 'steam_analysis' and other != profiles['ubisoft'][case]:
                raise RuntimeError('History row layouts differ')


def reviewed(profiles):
    """Public metadata only: field layout and hashes of synthetic writer data."""
    return {'version8_fields': {label: profiles[label]['v8-populated']['fields'] for label in ('gold', 'ubisoft')},
            'synthetic_binary_cases': {case: value['binary'] for case, value in profiles['gold'].items()}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for label in PROFILES:
        parser.add_argument('--'+label.replace('_', '-'), type=Path, required=True)
    parser.add_argument('--thunk-executable', type=Path, required=True)
    parser.add_argument('--expected-fixtures', type=Path,
                        default=Path(__file__).resolve().parents[2]/'tests/fixtures/gold-native-active-player-fields.json')
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    root, out = Path(__file__).resolve().parents[2], args.output.resolve()
    if not out.is_relative_to((root/'work').resolve()) or out.exists() or not out.parent.is_dir():
        parser.error('Use a fresh output under repository work')
    raw = args.thunk_executable.read_bytes()
    fixture = pefile.PE(data=raw)
    if (fixture.FILE_HEADER.Machine != 0x14c or fixture.OPTIONAL_HEADER.ImageBase != 0x24000000
            or fixture.OPTIONAL_HEADER.SizeOfImage > 4*1024*1024):
        raise ValueError('Expected the bounded fixed x86 ABI fixture')
    profiles, guards = {}, {}
    for edition, label in enumerate(PROFILES, 1):
        image = Image(label, getattr(args, label), types=())
        castle = {'gold': 0x5273a0, 'ubisoft': 0x586b90, 'steam_analysis': 0x586650}[label]
        guards[label] = check_image(image, fixture, edition, oracle=b'WePlayActivePlayerMethodsOracle',
                                    methods=((LAYOUTS[label][0], 0x3000), (castle, 0x2000)), instruction_limit=2000000)
        cases = {}
        for version in range(1, 10):
            for rich in (False, True):
                case = f'v{version}-'+('populated' if rich else 'empty')
                cases[case] = {**survey(image, version, rich, False, fixture),
                               'binary': survey(image, version, rich, True, fixture)}
        profiles[label] = cases
        print(json.dumps({'profile': label, 'completed_original_row_calls': 36, 'guard_checks': guards[label]}), flush=True)
        del image
        gc.collect()
    compare(profiles)
    if args.expected_fixtures.stat().st_size > 65536 or json.loads(args.expected_fixtures.read_text(encoding='utf-8')) != reviewed(profiles):
        raise RuntimeError('Original active-player fields differ from reviewed fixtures')
    receipt = {'format': 'native-active-player-writer-audit-v1',
               'fingerprints': {k: v[0] for k, v in PROFILES.items()},
               'original_row_calls': 108, 'compiled_thunks_executed': True,
               'fixture_sha256': hashlib.sha256(raw).hexdigest(), 'guard_checks': guards,
               'nested_nodes_traversed': True,
               'original_binary_visitors_executed': True, 'heap_and_stream_modeled': True,
               'live_format_version_qualified': False, 'native_save_reader_tested': False,
               'game_process_started': False, 'crossplay_qualified': False, 'profiles': profiles}
    with out.open('x', encoding='utf-8') as stream:
        json.dump(receipt, stream, indent=2)
    print(json.dumps({k: v for k, v in receipt.items() if k != 'profiles'}))


if __name__ == '__main__':
    main()
