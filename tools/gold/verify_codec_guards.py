"""Run the compiled x86 codec integrity guard against pinned analysis images.

Original game code is read but never executed. Only the compiled guard executes;
VirtualQuery is modeled for the bounded native image. This does not qualify a
native factory, an event delivery, a lobby or a mixed-edition match.
"""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import struct

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_EBP,
    UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_ESP, UC_X86_REG_EIP)
import emulate_event_payloads as native

SCRATCH, STACK = 0x20000000, 0x30000000
STOP, QUERY = SCRATCH, SCRATCH+16
STREAMS = {'gold': 0xf26560, 'ubisoft': 0x112f5bc, 'steam_analysis': 0x112db4c}


def check_image(image, fixture, edition, *, oracle=b'WePlayCodecTablesOracle', methods=()):
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(image.base, (image.size+4095) & ~4095)
    u.mem_write(image.base, image.mapped)
    base, size = fixture.OPTIONAL_HEADER.ImageBase, fixture.OPTIONAL_HEADER.SizeOfImage
    u.mem_map(base, (size+4095) & ~4095)
    u.mem_write(base, fixture.get_memory_mapped_image())
    u.mem_map(SCRATCH, 4096)
    u.mem_map(STACK, 65536)
    entry = next(base+e.address for e in fixture.DIRECTORY_ENTRY_EXPORT.symbols
                 if e.name == oracle)
    queries = [imp.address for library in fixture.DIRECTORY_ENTRY_IMPORT
               for imp in library.imports if imp.name == b'VirtualQuery']
    if len(queries) != 1:
        raise ValueError('Expected the compiled VirtualQuery import')
    u.mem_write(queries[0], struct.pack('<I', QUERY))
    blocked = None

    def word(address):
        return struct.unpack('<I', u.mem_read(address, 4))[0]

    def code(uc, address, length, data):
        if address == STOP:
            uc.emu_stop()
        elif address == QUERY:
            sp = uc.reg_read(UC_X86_REG_ESP)
            pointer, output, requested = (word(sp+4*i) for i in range(1, 4))
            if requested != 28 or not STACK <= output <= STACK+65536-28:
                raise RuntimeError('VirtualQuery output outside the owned stack')
            if image.base <= pointer < image.base+image.size and pointer != blocked:
                uc.mem_write(output, struct.pack('<7I', image.base, image.base, 2,
                                                image.size, 0x1000, 2, 0x1000000))
                result = 28
            else:
                result = 0
            uc.reg_write(UC_X86_REG_EAX, result)
            uc.reg_write(UC_X86_REG_EIP, word(sp))
            uc.reg_write(UC_X86_REG_ESP, sp+16)
        elif not base <= address < base+size:
            raise RuntimeError('Attempted to execute original game or foreign code')

    def write(uc, access, address, length, value, data):
        if not STACK <= address <= address+length <= STACK+65536:
            raise RuntimeError('Guard wrote outside its owned stack')

    u.hook_add(UC_HOOK_CODE, code)
    u.hook_add(UC_HOOK_MEM_WRITE, write)
    preserved = {UC_X86_REG_EBX: 0x12345678, UC_X86_REG_EBP: 0x23456789,
                 UC_X86_REG_ESI: 0x34567890, UC_X86_REG_EDI: 0x456789ab}

    def call(expected):
        sp = STACK+0xf000
        u.mem_write(sp, struct.pack('<2I', STOP, edition))
        for register, value in {**preserved, UC_X86_REG_ESP: sp}.items():
            u.reg_write(register, value)
        u.emu_start(entry, STOP+1, count=500000, timeout=5000000)
        if (u.reg_read(UC_X86_REG_EIP) != STOP or u.reg_read(UC_X86_REG_ESP) != sp+4
                or bool(u.reg_read(UC_X86_REG_EAX)) != expected
                or any(u.reg_read(k) != v for k, v in preserved.items())):
            raise RuntimeError('Compiled native table guard result/ABI mismatch')

    tables = () if methods else ((STREAMS[image.label], 11), (image.tables[native.WRITER][0], 39),
                                 (image.tables[native.READER][0], 34))
    call(True)
    checks = 1
    for address, length in methods:
        for offset in (0, length-1):
            original = bytes(u.mem_read(address+offset, 1))
            u.mem_write(address+offset, bytes((original[0] ^ 1,)))
            call(False)
            u.mem_write(address+offset, original)
            checks += 1
        blocked = address
        call(False)
        blocked = None
        checks += 1
    for table, count in tables:
        for index in (0, count-1):
            address = table+4*index
            original = bytes(u.mem_read(address, 4))
            changed = bytes((original[0] ^ 1,))+original[1:]
            u.mem_write(address, changed)
            call(False)
            u.mem_write(address, original)
            checks += 1
        method = word(table+4*(count-1))
        original = bytes(u.mem_read(method, 1))
        u.mem_write(method, bytes((original[0] ^ 1,)))
        call(False)
        u.mem_write(method, original)
        blocked = method
        call(False)
        blocked = None
        checks += 2
    call(True)
    return checks+1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for label in STREAMS:
        parser.add_argument('--'+label.replace('_', '-'), type=Path, required=True)
    parser.add_argument('--thunk-executable', type=Path, required=True)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if not output.is_relative_to((root/'work').resolve()) or output.exists() or not output.parent.is_dir():
        parser.error('Use a new output under the existing repository work directory')
    raw = args.thunk_executable.read_bytes()
    fixture = pefile.PE(data=raw)
    if (fixture.FILE_HEADER.Machine != 0x14c or fixture.OPTIONAL_HEADER.ImageBase != 0x24000000
            or fixture.OPTIONAL_HEADER.SizeOfImage > 4*1024*1024):
        raise ValueError('Expected the bounded compiled x86 codec guard fixture')
    # This reader still independently verifies each original image fingerprint.
    native.EVENTS = ()
    results = {}
    for edition, label in enumerate(STREAMS, 1):
        image = native.Image(label, getattr(args, label))
        results[label] = check_image(image, fixture, edition)
        del image
        gc.collect()
    receipt = {'checks': sum(results.values()), 'per_profile': results,
               'fixture_sha256': hashlib.sha256(raw).hexdigest(),
               'compiled_guard_executed': True, 'original_game_code_executed': False,
               'native_factory_qualified': False, 'game_process_started': False,
               'network_used': False, 'mixed_gameplay_qualified': False}
    output.write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps(receipt))


if __name__ == '__main__':
    main()
