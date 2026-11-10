"""Emulate Gold's original writer with empty synthetic control/data packets.

No game process or network. Only SDK thread-index zero and CRT memcpy are
stubbed. Native packet writing and checksum instructions execute unchanged.
Nonempty payloads, compression and crypto are outside this oracle's scope.
"""
import argparse
import json
from pathlib import Path
import struct

from emulate_checksum import original_bytes


def examine(path):
    import pefile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
    from unicorn.x86_const import (UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EAX,
        UC_X86_REG_ECX, UC_X86_REG_EBX, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI)
    pe = pefile.PE(data=original_bytes(path))
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(0x400000, (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095)
    u.mem_write(0x400000, pe.get_memory_mapped_image())
    scratch, stack_base = 0x20000000, 0x30000000
    stop, packet = scratch + 0xf000, scratch
    stream, buffer, output = scratch + 0x100, scratch + 0x200, scratch + 0x1000
    u.mem_map(scratch, 0x10000)
    u.mem_map(stack_base, 0x10000)

    def put(at, *values):
        u.mem_write(at, struct.pack('<' + 'I' * len(values), *values))

    def word(at):
        return struct.unpack('<I', u.mem_read(at, 4))[0]

    def returned(value):
        esp = u.reg_read(UC_X86_REG_ESP)
        pc = word(esp)
        u.reg_write(UC_X86_REG_ESP, esp + 4)
        u.reg_write(UC_X86_REG_EAX, value)
        u.reg_write(UC_X86_REG_EIP, pc)

    def code(uc, address, size, data):
        if address == 0xb96950:
            returned(0)  # Synthetic SDK thread index; no Windows TLS call.
        elif address == 0xbaee6c:
            esp = u.reg_read(UC_X86_REG_ESP)
            dest, source, length = [word(esp + x) for x in (4, 8, 12)]
            if not (length <= 128 and scratch <= dest < dest + length <= scratch + 0x10000):
                raise RuntimeError('Native writer requested an unexpected memcpy range')
            u.mem_write(dest, bytes(u.mem_read(source, length)))
            returned(dest)

    def writes(uc, access, address, size, value, data):
        if not (scratch <= address <= address + size <= scratch + 0x10000
                or stack_base <= address <= address + size <= stack_base + 0x10000):
            raise RuntimeError('Native packet writer wrote outside its synthetic arena/stack')

    u.hook_add(UC_HOOK_CODE, code)
    u.hook_add(UC_HOOK_MEM_WRITE, writes)
    fixtures = []
    for kind in range(5):
        for flags in (0, 8, 16, 32, 64, 120):
            for seed in (0, 23, 255):
                for auto_size in (0, 1):
                    u.mem_write(packet, b'\0' * 0x80)
                    u.mem_write(output, b'\0' * 128)
                    u.mem_write(packet + 0x10, bytes((0x21, 0x22, kind | flags, 0x7f)))
                    put(packet + 0x14, 0x12345678, 65535, 0x89abcdef)
                    u.mem_write(packet + 0x20, b'\x05')
                    put(stream, 0, buffer, 0)
                    put(buffer, 0, 0, output, 0, 128)
                    config = 0x10ce4b8 + 2 * 0xb0 + 0x10
                    u.mem_write(config, bytes((seed,)) + b'\0' * 31)
                    u.mem_write(config + 0x18, bytes((auto_size,)))
                    stack = stack_base + 0xf000
                    put(stack, stop, stream)
                    saved = {UC_X86_REG_EBX: 0x1234abcd, UC_X86_REG_EBP: 0x2345bcde,
                        UC_X86_REG_ESI: 0x3456cdef, UC_X86_REG_EDI: 0x4567def0}
                    for reg, value in {**saved, UC_X86_REG_ECX: packet, UC_X86_REG_ESP: stack}.items():
                        u.reg_write(reg, value)
                    u.emu_start(0xd1d1f0, stop, count=200000, timeout=2_000_000)
                    if (u.reg_read(UC_X86_REG_EIP) != stop
                            or u.reg_read(UC_X86_REG_ESP) != stack + 8
                            or u.reg_read(UC_X86_REG_EAX) & 255 != 1
                            or any(u.reg_read(k) != v for k, v in saved.items())):
                        raise RuntimeError('Native packet writer failed its bounded ABI check')
                    length = word(buffer + 0xc)
                    if not 11 <= length <= 17:
                        raise RuntimeError('Unexpected native empty-packet length')
                    native = bytes(u.mem_read(output, length))
                    effective = flags | (64 if auto_size else 0)
                    expected = bytes((0x21, 0x22, kind | effective, 0x7f))
                    expected += struct.pack('<IH', 0x12345678, 65535)
                    if kind <= 1:
                        expected += struct.pack('<I', 0x89abcdef)
                    if kind == 2:
                        expected += b'\x05'
                    if effective & 64:
                        expected += b'\0\0'
                    end = len(expected) - len(expected) % 4
                    total = sum(struct.unpack_from('<I', expected, i)[0]
                                for i in range(0, end, 4)) & 0xffffffff
                    checksum = (seed + sum(total.to_bytes(4, 'little')) + sum(expected[end:])) & 255
                    expected += bytes((checksum,))
                    if native != expected:
                        raise RuntimeError('Native packet bytes differ from the mapped empty header')
                    fixtures.append({'type': kind, 'flags': flags, 'seed': seed,
                        'auto_size': auto_size, 'native_hex': native.hex()})
    return {'format': 'gold-native-empty-packet-oracle-v1', 'count': len(fixtures),
        'actual_native_writer': True, 'actual_native_checksum': True, 'payloads': 'empty-only',
        'stubs': ['sdk-thread-index-zero', 'CRT-memcpy'], 'actual_game_or_network': False,
        'fixtures': fixtures}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('original_gold_exe', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    work = Path(__file__).resolve().parents[2] / 'work'
    if not output.is_relative_to(work.resolve()) or output.exists() or not output.parent.is_dir():
        parser.error('Use a new output file in an existing directory under repository work/')
    result = examine(args.original_gold_exe)
    with output.open('x', encoding='utf-8') as target:
        json.dump(result, target, indent=2)
    print(json.dumps({k: v for k, v in result.items() if k != 'fixtures'}))


if __name__ == '__main__':
    main()
