"""Compare native event payload writers in bounded, offline x86 emulation.

Requires pefile and Unicorn. Inputs are owned, fingerprinted analysis images;
no game process, native host call, import forwarding or network is used.
This checks synthetic event payloads, not session admission or crossplay.
The Steam input is the existing private analysis image, never a launch image.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

PROFILES = {
    'gold': ('ec27c7fab2c83a1444f043eb4730f0065393ca2e91607f71e5fd78ee5a5c7c94',
             14141768, None, 0x4284d0),
    'ubisoft': ('4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f',
                17333680, 0x4f1740, 0x4f2450),
    'steam_analysis': ('b45794aef1ddef76fea13501fa7630023f0c11653c2f644a06c0ae86cb0812b4',
                       17460224, 0x4f13c0, 0x4f20d0),
}
EVENTS = ('CEndOfTurnEvent', 'CEndOfTurnBatchEvent', 'CLogicStateEvent', 'CGameStartEvent')
WRITER = 'CBinarySerializationWriterNoOverhead@CBase'
READER = 'CBinarySerializationReaderNoOverhead@CBase'
SCRATCH, STACK = 0x20000000, 0x30000000
OBJECT, VISITOR, STREAM = SCRATCH+0x1000, SCRATCH+0x2000, SCRATCH+0x3000
STREAM_VTABLE, POSITION, WRITE = SCRATCH+0x4000, SCRATCH+0x5000, SCRATCH+0x5010
STOP, NAME, NESTED, REFLECTION = (SCRATCH+0x6000, SCRATCH+0x7000,
                                 SCRATCH+0x8000, SCRATCH+0x9000)


class Image:
    def __init__(self, label, path, *, types=None):
        import pefile
        pin, size, self.name_helper, self.compare_helper = PROFILES[label]
        path = Path(path)
        if path.stat().st_size != size:
            raise ValueError('Wrong analysis image size: '+label)
        raw = path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != pin:
            raise ValueError('Wrong analysis image fingerprint: '+label)
        self.label, self.pin = label, pin
        pe = pefile.PE(data=raw)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        if self.base != 0x400000 or pe.FILE_HEADER.Machine != 0x14c:
            raise ValueError('Requires the pinned x86 image')
        self.size = pe.OPTIONAL_HEADER.SizeOfImage
        self.mapped = pe.get_memory_mapped_image()
        self.crt_imports = {i.name: i.address for library in pe.DIRECTORY_ENTRY_IMPORT for i in library.imports
                            if i.name in (b'malloc', b'free', b'memcpy', b'memmove', b'__std_type_info_name')}
        executable = [(self.base+s.VirtualAddress,
                       self.base+s.VirtualAddress+s.Misc_VirtualSize)
                      for s in pe.sections if s.Characteristics & 0x20000000]

        def word(va):
            return struct.unpack('<I', pe.get_data(va-self.base, 4))[0]

        def refs(value):
            needle, start = struct.pack('<I', value), 0
            while (at := raw.find(needle, start)) >= 0:
                yield self.base+pe.get_rva_from_offset(at)
                start = at+4

        self.tables = {}
        for name in (*(EVENTS if types is None else types), WRITER, READER):
            qualified = name if '@' in name else name+'@CNetwork'
            needle = ('.?AV'+qualified+'@@').encode()+b'\0'
            at = raw.find(needle)
            if at < 8 or raw.find(needle, at+1) >= 0:
                raise ValueError('Ambiguous native type: '+name)
            descriptor = self.base+pe.get_rva_from_offset(at)-8
            tables = []
            for reference in refs(descriptor):
                col = reference-12
                if word(col) != 0 or word(col+4) != 0 or word(col+8) != 0:
                    continue
                for reference in refs(col):
                    table = reference+4
                    count = 39 if name == WRITER else 34 if name == READER else 6
                    methods = [word(table+4*i) for i in range(count)]
                    if all(any(a <= method < b for a, b in executable) for method in methods):
                        tables.append((table, methods))
            if len(tables) != 1:
                raise ValueError('Ambiguous native vtable: '+name)
            self.tables[name] = tables[0]


class InputBoundError(RuntimeError):
    """The synthetic input stream refused an overread or trailing payload."""


def payload(image, name, nonempty=False, scalar=0x10000000, asynchronous=0, *, read_wire=None, read_batch=False,
            nested_event='CEndOfTurnEvent', nested_player=1):
    reading = read_wire is not None
    placement = nested_event == 'CPosOrientationTypeAndCategoryEvent@ELBase'
    road = nested_event == 'CPlaceStreetEvent@ELBase'
    if nested_event != 'CEndOfTurnEvent' and not (
            (placement or road) and name == 'CLogicStateEvent' and nonempty and not reading and nested_player in (1, 2)):
        raise ValueError('Only explicit bounded placement-writer fixtures accept a gameplay event')
    batch = reading and read_batch and name == 'CEndOfTurnBatchEvent'
    if read_batch and not batch:
        raise ValueError('Batch input requires the explicit batch event and read stream')
    if reading and (not batch and name not in (EVENTS[0], 'CLoadProgressEvent', 'CDesyncDetectionTurnEvent') or nonempty or type(read_wire) is not bytes
                    or len(read_wire) > 65536):
        raise ValueError('Only scalar events or explicitly bounded turn batches are mapped')
    batch_count = struct.unpack_from('<I', read_wire, 13)[0] if batch and len(read_wire) >= 17 else 0
    if batch and (len(read_wire) < 17 or batch_count > 64):
        raise InputBoundError('Turn-batch count/header bound')
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
    from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_EBP,
        UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EIP)
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(image.base, (image.size+4095) & ~4095)
    u.mem_write(image.base, image.mapped)
    u.mem_map(0, 4096)  # Only the emulated x86 SEH head may be written here.
    u.mem_map(SCRATCH, 0x10000)
    u.mem_map(STACK, 0x10000)

    def words(address, *values):
        u.mem_write(address, struct.pack('<'+'I'*len(values), *values))

    def word(address):
        return struct.unpack('<I', u.mem_read(address, 4))[0]

    table, methods = image.tables[name]
    words(OBJECT, table, 0x11112222, 0x33334444, asynchronous)
    for offset in range(0x10, 0x80, 4):
        words(OBJECT+offset, (scalar+offset) & 0xffffffff)
    if reading:
        u.mem_write(OBJECT+4, bytes(0x20))
    if name == 'CGameStartEvent':
        text = b'synthetic/map' if scalar else b''
        u.mem_write(NAME+0x100, text+b'\0')
        words(OBJECT+0x10, NAME+0x100, len(text)+1, len(text))
    if name in ('CLogicStateEvent', 'CEndOfTurnBatchEvent'):
        for offset in range(0x10 if name == 'CLogicStateEvent' else 0x14, 0x80, 4):
            words(OBJECT+offset, 0)
    if nonempty:
        nested_table = image.tables[nested_event][0]
        for index in range(2):
            words(NESTED+index*0x24, nested_table, 0x1000+index, 0x2000+index,
                  index, 0x3000+index, 0x4000+index, 0x5000+index,
                  0x6000+index, 0x7000+index)
        if name == 'CLogicStateEvent':
            words(OBJECT+0x10, NESTED)
        elif name == 'CEndOfTurnBatchEvent':
            offset = 0x18 if image.label == 'gold' else 0x14
            words(OBJECT+offset, NESTED, NESTED+2*0x24, NESTED+2*0x24)
        else:
            raise ValueError('Only container events accept nested fixtures')
    if placement or road:
        # Synthetic normal placement, never a live command or heap.
        words(OBJECT+4, 7 | (11 << 16), 42, asynchronous)
        words(OBJECT+0x14, nested_player, 1)  # Original SourceSlot / IsSync.
        if placement:
            words(NESTED, nested_table, 2 | (4 << 16), nested_player,
                  0x472fc000, 0x47435000, 0x80000000, 260, 14)
        else:
            words(NESTED, nested_table, 2 | (11 << 16), nested_player,
                  1, 0x472fc000, 0x47435000, 0x47300000, 0x47440000)
    if batch:
        # Preallocate synthetic storage; no native heap or allocation import is
        # executed. Original vector resize/constructors and readers still run.
        vector = 0x18 if image.label == 'gold' else 0x14
        words(OBJECT+vector, NESTED, NESTED, NESTED+64*0x24)
    words(VISITOR, image.tables[READER if reading else WRITER][0], int(reading), STREAM, 0)
    words(STREAM, STREAM_VTABLE)
    words(STREAM_VTABLE+0x14, POSITION)
    words(STREAM_VTABLE+(0x20 if reading else 0x28), WRITE)
    u.mem_write(NAME, b'class SyntheticEvent\0')
    if image.label == 'gold':
        words(0xebf574, REFLECTION)
    elif placement or road:
        # Original ELBase visitors use this CRT type-name import. Model only
        # that reflection result; no CRT import executes on the host.
        words(image.crt_imports[b'__std_type_info_name'], REFLECTION)
    output = bytearray()
    read_position = 0
    completed = False

    def returned(count, value=0):
        esp = u.reg_read(UC_X86_REG_ESP)
        u.reg_write(UC_X86_REG_EAX, value)
        u.reg_write(UC_X86_REG_ESP, esp+4+count)
        u.reg_write(UC_X86_REG_EIP, word(esp))

    def code(uc, address, size, data):
        nonlocal completed, read_position
        if address == STOP:
            completed = True
            uc.emu_stop()
        elif address == REFLECTION or address == image.name_helper:
            returned(4 if image.label == 'gold' else 0, NAME)
        elif address == image.compare_helper:
            returned(0, 1)
        elif address == POSITION:
            if uc.reg_read(UC_X86_REG_ECX) != STREAM:
                raise RuntimeError('Unexpected stream ownership')
            returned(0, read_position if reading else len(output))
        elif address == WRITE:
            esp = uc.reg_read(UC_X86_REG_ESP)
            pointer, length = word(esp+4), word(esp+8)
            if reading:
                if (uc.reg_read(UC_X86_REG_ECX) != STREAM or word(esp+12) != 0
                        or length > 4096 or not (
                            OBJECT+4 <= pointer <= pointer+length <= OBJECT+0x24
                            or STACK <= pointer <= pointer+length <= STACK+0x10000
                            or batch and NESTED <= pointer <= pointer+length <= NESTED+batch_count*0x24)):
                    raise RuntimeError('Unexpected native read destination/ownership')
                if read_position+length > len(read_wire):
                    raise InputBoundError('Synthetic input stream overread')
                uc.mem_write(pointer, read_wire[read_position:read_position+length])
                read_position += length
                returned(12, length)
            elif (uc.reg_read(UC_X86_REG_ECX) != STREAM or length > 4096
                    or len(output)+length > 65536):
                raise RuntimeError('Synthetic stream bound/ownership')
            else:
                output.extend(uc.mem_read(pointer, length))
                returned(8)
        elif not image.base <= address < image.base+image.size:
            raise RuntimeError('Unexpected execution outside native image')

    def writes(uc, access, address, size, value, data):
        if not (STACK <= address <= address+size <= STACK+0x10000
                or address == 0 and size == 4
                or VISITOR+0xc <= address <= address+size <= VISITOR+0x214
                or reading and OBJECT+4 <= address <= address+size <= OBJECT+0x24
                or batch and NESTED <= address <= address+size <= NESTED+batch_count*0x24):
            raise RuntimeError('Native serializer wrote outside its stack/writer')

    u.hook_add(UC_HOOK_CODE, code)
    u.hook_add(UC_HOOK_MEM_WRITE, writes)
    esp = STACK+0xf000
    words(esp, STOP, VISITOR, 0)
    preserved = {UC_X86_REG_EBX: 0x12345678, UC_X86_REG_EBP: 0x23456789,
                 UC_X86_REG_ESI: 0x34567890, UC_X86_REG_EDI: 0x456789ab}
    for register, value in {**preserved, UC_X86_REG_ESP: esp, UC_X86_REG_ECX: OBJECT}.items():
        u.reg_write(register, value)
    before = bytes(u.mem_read(OBJECT, 0x100))
    nested_before = bytes(u.mem_read(NESTED, 2*0x24))
    u.emu_start(methods[3], STOP+1, timeout=2_000_000, count=100000)
    if (not completed or u.reg_read(UC_X86_REG_ESP) != esp+12
            or any(u.reg_read(k) != v for k, v in preserved.items())
            or (not reading and bytes(u.mem_read(OBJECT, 0x100)) != before)
            or not batch and bytes(u.mem_read(NESTED, 2*0x24)) != nested_before
            or word(VISITOR+0xc) != 0):
        raise RuntimeError('Native serializer changed its bounded ABI/input state')
    if reading:
        if word(OBJECT) != table or bytes(u.mem_read(OBJECT+0x24, 0xdc)) != before[0x24:]:
            raise RuntimeError('Native reader changed fields outside the mapped event')
        if read_position != len(read_wire):
            raise InputBoundError('Trailing native input payload')
        if batch:
            if (word(OBJECT+vector) != NESTED
                    or word(OBJECT+vector+4) != NESTED+batch_count*0x24
                    or word(OBJECT+vector+8) != NESTED+64*0x24):
                raise RuntimeError('Original vector changed its synthetic storage ownership')
            nested = []
            for index in range(batch_count):
                at = NESTED+index*0x24
                if word(at) != image.tables['CEndOfTurnEvent'][0]:
                    raise RuntimeError('Original nested constructor vtable differs')
                nested.append(tuple(word(at+offset) for offset in range(4, 0x24, 4)))
            return {'base': tuple(word(OBJECT+offset) for offset in (4, 8, 12, 16)), 'nested': nested}
        return tuple(word(OBJECT+offset) for offset in range(4, 0x24, 4))
    return bytes(output)


def compare(images):
    fixtures = []
    cases = [(name, nested, scalar, asynchronous)
             for name, nested in ((EVENTS[0], False), (EVENTS[1], False),
                                  (EVENTS[1], True), (EVENTS[2], False), (EVENTS[2], True),
                                  (EVENTS[3], False))
             for scalar, asynchronous in ((0, 0), (0xfffffff0, 1))]
    for name, nested, scalar, asynchronous in cases:
        outputs = {image.label: payload(image, name, nested, scalar, asynchronous)
                   for image in images}
        if len(set(outputs.values())) != 1:
            raise RuntimeError('Cross-edition native payload mismatch: '+name)
        wire = next(iter(outputs.values()))
        fixtures.append({'event': name, 'nested': nested, 'scalar': scalar,
                         'asynchronous': asynchronous, 'hex': wire.hex()})
    return {'format': 'native-event-payload-oracle-v1', 'fingerprints':
            {image.label: image.pin for image in images},
            'native_executions': len(cases)*len(images),
            'native_writer_and_event_visitors': True,
            'synthetic_output_stream': True, 'actual_game_test': False,
            'crossplay_qualified': False, 'fixtures': fixtures}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for label in PROFILES:
        parser.add_argument('--'+label.replace('_', '-'), required=True, type=Path)
    parser.add_argument('--expected-fixtures', type=Path,
                        default=Path(__file__).resolve().parents[2]/
                        'tests/fixtures/gold-native-event-payloads.json')
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    work = Path(__file__).resolve().parents[2]/'work'
    if not output.is_relative_to(work.resolve()) or output.exists() or not output.parent.is_dir():
        parser.error('Use a new output file in an existing repository work directory')
    result = compare([Image(label, getattr(args, label)) for label in PROFILES])
    if (args.expected_fixtures.stat().st_size > 65536
            or json.loads(args.expected_fixtures.read_text(encoding='utf-8')) != result):
        raise RuntimeError('Native output differs from the reviewed synthetic fixtures')
    with output.open('x', encoding='utf-8') as target:
        json.dump(result, target, indent=2)
    print(json.dumps({k: v for k, v in result.items() if k != 'fixtures'}))


if __name__ == '__main__':
    main()
