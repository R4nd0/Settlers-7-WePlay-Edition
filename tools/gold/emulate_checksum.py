"""Bounded x86 oracle for Gold's checksum, using an owned original executable.

Developer dependencies: pefile and Unicorn 2.1.4. No game process, native game
call, import forwarding, socket, account cache or runtime installation is used.
Output contains only synthetic vectors and their checksums, never game code.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct

PIN = 'ec27c7fab2c83a1444f043eb4730f0065393ca2e91607f71e5fd78ee5a5c7c94'


def original_bytes(path):
    path = Path(path)
    if path.stat().st_size != 14141768:
        raise ValueError('Requires the exact original Gold 1.12.1396 executable')
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise ValueError('Requires the exact original Gold 1.12.1396 executable')
    return raw


def examine(path):
    raw = original_bytes(path)
    pin = PIN
    import pefile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_MEM_WRITE
    from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_EBP,
        UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EIP)
    pe = pefile.PE(data=raw)
    emulator = Uc(UC_ARCH_X86, UC_MODE_32)
    emulator.mem_map(0x400000, (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095)
    emulator.mem_write(0x400000, pe.get_memory_mapped_image())
    scratch, stack_base = 0x20000000, 0x30000000
    stop, buffer, payload_base = scratch + 0x2f000, scratch, scratch + 0x1000
    emulator.mem_map(scratch, 0x30000)
    emulator.mem_map(stack_base, 0x10000)

    def word(address, value):
        emulator.mem_write(address, struct.pack('<I', value))

    def writes(uc, access, address, size, value, data):
        if not stack_base <= address <= address + size <= stack_base + 0x10000:
            raise RuntimeError('Native checksum wrote outside its stack')

    emulator.hook_add(UC_HOOK_MEM_WRITE, writes)
    fixtures = []
    rng = random.Random(0x7c0de)

    def oracle(payload, seed):
        emulator.mem_write(payload_base, payload)
        emulator.mem_write(buffer, b'\0' * 32)
        word(buffer + 8, payload_base)
        stack = stack_base + 0xf000
        for offset, value in enumerate((stop, len(payload), seed)):
            word(stack + offset * 4, value)
        preserved = {UC_X86_REG_EBX: 0x1234abcd, UC_X86_REG_EBP: 0x2345bcde,
                     UC_X86_REG_ESI: 0x3456cdef, UC_X86_REG_EDI: 0x4567def0}
        registers = {**preserved, UC_X86_REG_ESP: stack,
                     UC_X86_REG_ECX: buffer, UC_X86_REG_EAX: 0}
        for register, value in registers.items():
            emulator.reg_write(register, value)
        before = bytes(emulator.mem_read(buffer, 32))
        emulator.emu_start(0xc456e0, stop, timeout=2_000_000, count=200000)
        if (emulator.reg_read(UC_X86_REG_EIP) != stop
                or emulator.reg_read(UC_X86_REG_ESP) != stack + 12
                or any(emulator.reg_read(k) != v for k, v in preserved.items())):
            raise RuntimeError('Native checksum did not preserve its bounded ABI')
        if (bytes(emulator.mem_read(payload_base, len(payload))) != payload
                or bytes(emulator.mem_read(buffer, 32)) != before):
            raise RuntimeError('Native checksum changed its inputs')
        return emulator.reg_read(UC_X86_REG_EAX) & 255

    for length in list(range(33)) + [63, 64, 65, 255, 256, 257, 4096, 4097, 65507]:
        for pattern in ('ascending', 'ff', 'random'):
            if pattern == 'ascending':
                payload = bytes(i & 255 for i in range(length))
            elif pattern == 'ff':
                payload = b'\xff' * length
            else:
                payload = bytes(rng.randrange(256) for _ in range(length))
            for seed in (0, 1, 23, 255):
                value = oracle(payload, seed)
                full_end = len(payload) - len(payload) % 4
                total = sum(struct.unpack('<I', payload[i:i + 4])[0]
                            for i in range(0, full_end, 4)) & 0xffffffff
                expected = (seed + sum(total.to_bytes(4, 'little'))
                            + sum(payload[full_end:])) & 255
                if value != expected:
                    raise RuntimeError('Native checksum disagrees with the independently decoded arithmetic')
                fixtures.append({'length': length, 'pattern': pattern, 'seed': seed,
                    'native_checksum': value, 'payload_sha256': hashlib.sha256(payload).hexdigest(),
                    'hex': payload.hex() if len(payload) <= 65 else None})
    return {'format': 'gold-native-checksum-oracle-v1', 'original_gold_exe_sha256': pin,
            'entry_rva': '0x8456e0', 'count': len(fixtures),
            'stack_and_nonvolatile_registers_preserved': True, 'native_input_writes': False,
            'actual_game_or_datagram_test': False, 'fixtures': fixtures}


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
