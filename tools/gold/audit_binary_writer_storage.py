"""Check native binary-writer storage using owned images and offline x86 CPU.

Reproduces the old 16-byte allocation failure, checks exact writer boundaries,
and executes all 128 scope entries/exits against guarded C++ stack storage.
This does not start a game or qualify setup agreement/worlds/NEO/save reload.
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
from emulate_active_players import (Image, PROFILES, WRITER, BINARY_WRITER_SIZE,
    STREAM, STREAM_TABLE, POSITION, WRITE, S, STACK, STOP, survey)


def scope_check(image, fixture):
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(image.base, (image.size+4095)&~4095)
    u.mem_write(image.base, image.mapped)
    base, size = fixture.OPTIONAL_HEADER.ImageBase, fixture.OPTIONAL_HEADER.SizeOfImage
    u.mem_map(base, (size+4095)&~4095)
    u.mem_write(base, fixture.get_memory_mapped_image())
    u.mem_map(S, 65536)
    u.mem_map(STACK, 65536)
    u.mem_write(STREAM, struct.pack('<I', STREAM_TABLE))
    u.mem_write(STREAM_TABLE+0x14, struct.pack('<I', POSITION))
    u.mem_write(STREAM_TABLE+0x28, struct.pack('<I', WRITE))
    exports = {e.name: base+e.address for e in fixture.DIRECTORY_ENTRY_EXPORT.symbols}
    output = bytearray()

    def word(at):
        return struct.unpack('<I', u.mem_read(at, 4))[0]

    def code(uc, at, length, data):
        if at == STOP:
            uc.emu_stop()
        elif at in (POSITION, WRITE):
            # Original scopes access only the supplied, bounded stream.
            from unicorn.x86_const import UC_X86_REG_ECX
            if uc.reg_read(UC_X86_REG_ECX) != STREAM:
                raise RuntimeError('Scope oracle used a foreign stream')
            sp = uc.reg_read(UC_X86_REG_ESP)
            if at == POSITION:
                uc.reg_write(UC_X86_REG_EAX, len(output))
                cleanup = 4
            else:
                pointer, count = word(sp+4), word(sp+8)
                if count != 8 or not STACK <= pointer <= STACK+65536-count or len(output)+count > 1024:
                    raise RuntimeError('Scope oracle stream bound')
                output.extend(uc.mem_read(pointer, count))
                cleanup = 12
            uc.reg_write(UC_X86_REG_EIP, word(sp))
            uc.reg_write(UC_X86_REG_ESP, sp+cleanup)
        elif not (base <= at < base+size or image.base <= at < image.base+image.size):
            raise RuntimeError('Scope oracle executed foreign code')

    def write(uc, access, at, count, value, data):
        if not STACK <= at <= at+count <= STACK+65536:
            raise RuntimeError('Scope oracle wrote outside its owned stack')

    u.hook_add(UC_HOOK_CODE, code)
    u.hook_add(UC_HOOK_MEM_WRITE, write)
    preserved = {UC_X86_REG_EBX: 0x12345678, UC_X86_REG_EBP: 0x23456789,
                 UC_X86_REG_ESI: 0x34567890, UC_X86_REG_EDI: 0x456789ab}

    def call(name, arguments, expected):
        sp = STACK+0xf000
        u.mem_write(sp, struct.pack('<'+'I'*(len(arguments)+1), STOP, *arguments))
        for reg, value in {**preserved, UC_X86_REG_ESP: sp}.items():
            u.reg_write(reg, value)
        u.emu_start(exports[name], STOP+1, count=1000000, timeout=5000000)
        if (u.reg_read(UC_X86_REG_EIP) != STOP or u.reg_read(UC_X86_REG_ESP) != sp+4
                or u.reg_read(UC_X86_REG_EAX) != expected
                or any(u.reg_read(reg) != value for reg, value in preserved.items())):
            raise RuntimeError('Compiled scope storage/ABI/canary check failed')

    call(b'WePlayBinaryWriterSizeOracle', (), BINARY_WRITER_SIZE)
    call(b'WePlayBinaryWriterScopeOracle', (image.tables[WRITER][0], STREAM), 1)
    if output != b'Bloc\0\0\0\0'*128:
        raise RuntimeError('Original scope output differs')
    return {'compiled_writer_bytes': BINARY_WRITER_SIZE, 'maximum_scope_depth': 128,
            'original_scope_entries': 128, 'original_scope_exits': 128,
            'cpp_stack_canaries_intact': True, 'nonvolatile_registers_preserved': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for label in PROFILES:
        parser.add_argument('--'+label.replace('_', '-'), type=Path, required=True)
    parser.add_argument('--thunk-executable', type=Path, required=True)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    root, out = Path(__file__).resolve().parents[2], args.output.resolve()
    if not out.is_relative_to((root/'work').resolve()) or out.exists() or not out.parent.is_dir():
        parser.error('Use a fresh output under existing repository work')
    raw = args.thunk_executable.read_bytes()
    fixture = pefile.PE(data=raw)
    if (fixture.FILE_HEADER.Machine != 0x14c or fixture.OPTIONAL_HEADER.ImageBase != 0x24000000
            or fixture.OPTIONAL_HEADER.SizeOfImage > 4*1024*1024):
        raise ValueError('Expected the bounded fixed x86 ABI fixture')
    profiles = {}
    for label in PROFILES:
        image = Image(label, getattr(args, label), types=())
        negative, positive = 0, {}
        for whole in (False, True):
            options = {'preparation': whole, 'long_allies': whole}
            try:
                survey(image, 8, True, True, writer_storage_size=16, **options)
            except RuntimeError as error:
                if str(error) != 'Binary writer exceeded owned visitor storage':
                    raise
                negative += 1
            else:
                raise RuntimeError('Old undersized visitor unexpectedly accepted')
            positive['whole' if whole else 'row'] = survey(image, 8, True, True, **options)
        profiles[label] = {**scope_check(image, fixture), 'old_16_byte_allocations_rejected': negative,
                           'exact_storage_canaries_intact': True, 'binary_cases': positive}
        print(json.dumps({'profile': label, **profiles[label]}), flush=True)
        del image
        gc.collect()
    reference = profiles['gold']['binary_cases']
    if any(profile['binary_cases'] != reference for profile in profiles.values()):
        raise RuntimeError('Bounded serialization differs across profiles')
    receipt = {'format': 'native-binary-writer-storage-audit-v1',
        'fixture_sha256': hashlib.sha256(raw).hexdigest(),
        'fingerprints': {k: v[0] for k, v in PROFILES.items()}, 'profiles': profiles,
        'original_code_executed_in_offline_cpu_only': True,
        'game_process_started': False, 'shared_world_started': False,
        'gold_neo_qualified': False, 'mixed_save_coldreload_qualified': False}
    with out.open('x', encoding='utf-8') as stream:
        json.dump(receipt, stream, indent=2)
    print(json.dumps({'profiles': len(profiles), 'old_allocations_rejected': 6,
                      'original_scope_entries_and_exits': 768, 'world_qualified': False}))


if __name__ == '__main__':
    main()
