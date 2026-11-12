"""Verify symbolic registry layout by executing original x86 reserve routines.

No-growth and growth cases use a pointer-valued allocator field. The original
code must preserve count/allocator, write capacity at +0x0c, retain entries and
pass the original allocator identity to its pool helper. CRT allocation, copy,
free and the pool helper are modeled. No game or session is started.
"""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import struct

from emulate_event_payloads import Image, PROFILES

METHODS = {
    'gold': (0x4289d0, 0xbae4fa, 0xbaee6c, 0xbae7f8, 0x428850),
    'ubisoft': (0x4f2630, 0xa22bfd, 0xa237ee, 0xa22d64, 0x4f24b0),
    'steam_analysis': (0x4f22b0, 0xa2217d, 0xa22d6e, 0xa222e4, 0x4f2130),
}
S, STACK = 0x20000000, 0x30000000
REGISTRY, OLD, POOL, NEW, STOP, ITEM = (S+x for x in (0x1000, 0x1100, 0x2000, 0x4000, 0x6000, 0x7000))


def reserve(image, requested):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
    from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX,
        UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_EIP)
    if requested not in (0, 1, 2, 3, 5):
        raise ValueError('Unsupported bounded reserve case')
    entry, allocate, copy, free, pool = METHODS[image.label]
    gold = image.label == 'gold'
    expected_capacity = max(2, requested) if requested > 1 else 1
    growing = requested > 1
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(image.base, (image.size+4095)&~4095)
    u.mem_write(image.base, image.mapped)
    u.mem_map(S, 0x10000)
    u.mem_map(STACK, 0x10000)

    def words(at, *values):
        u.mem_write(at, struct.pack('<'+'I'*len(values), *values))

    def word(at):
        return struct.unpack('<I', u.mem_read(at, 4))[0]

    words(REGISTRY, OLD, POOL, 1, 1)
    words(OLD, ITEM)
    words(POOL, 16, 8, S+0x3000)
    before_pool = bytes(u.mem_read(POOL, 12))
    events = []
    completed = False

    def returned(count, value=0):
        sp = u.reg_read(UC_X86_REG_ESP)
        u.reg_write(UC_X86_REG_EAX, value)
        u.reg_write(UC_X86_REG_EIP, word(sp))
        u.reg_write(UC_X86_REG_ESP, sp+4+count)

    def code(uc, at, size, data):
        nonlocal completed
        sp = uc.reg_read(UC_X86_REG_ESP)
        if at == STOP:
            completed = True
            uc.emu_stop()
        elif at == allocate:
            if not growing or events or word(sp+4) != expected_capacity*4:
                raise RuntimeError('Unexpected native allocation')
            events.append('allocate')
            returned(0, NEW)
        elif at == copy:
            if events != ['allocate'] or (word(sp+4), word(sp+8), word(sp+12)) != (NEW, OLD, 4):
                raise RuntimeError('Unexpected native pointer-array copy')
            events.append('copy')
            uc.mem_write(NEW, bytes(uc.mem_read(OLD, 4)))
            returned(0, NEW)
        elif at == free:
            if events != ['allocate', 'copy'] or word(sp+4) != OLD:
                raise RuntimeError('Unowned native free')
            events.append('free')
            returned(0)
        elif at == pool:
            receiver = uc.reg_read(UC_X86_REG_ESI if gold else UC_X86_REG_ECX)
            capacity = uc.reg_read(UC_X86_REG_EAX) if gold else word(sp+4)
            if events != ['allocate', 'copy', 'free'] or receiver != POOL or capacity != expected_capacity:
                raise RuntimeError('Original pool identity/capacity mismatch')
            events.append('pool_reserve')
            returned(0 if gold else 4)
        elif not image.base <= at < image.base+image.size:
            raise RuntimeError('Foreign native execution')

    def write(uc, access, at, size, value, data):
        if not (STACK <= at <= at+size <= STACK+0x10000
                or growing and at in (REGISTRY, REGISTRY+12) and size == 4):
            raise RuntimeError('Unexpected native registry/pool write')

    u.hook_add(UC_HOOK_CODE, code)
    u.hook_add(UC_HOOK_MEM_WRITE, write)
    sp = STACK+0xf000
    words(sp, *((STOP,) if gold else (STOP, requested)))
    kept = {UC_X86_REG_EBX: 0x12345678, UC_X86_REG_ESI: 0x34567890,
            UC_X86_REG_EDI: 0x456789ab, UC_X86_REG_EBP: 0x23456789}
    args = {UC_X86_REG_EAX: REGISTRY, UC_X86_REG_ECX: requested} if gold else {UC_X86_REG_ECX: REGISTRY}
    for reg, value in {**kept, **args, UC_X86_REG_ESP: sp}.items():
        u.reg_write(reg, value)
    u.emu_start(entry, STOP+1, count=10000, timeout=1_000_000)
    if (not completed or u.reg_read(UC_X86_REG_EIP) != STOP
            or u.reg_read(UC_X86_REG_ESP) != sp+(4 if gold else 8)
            or any(u.reg_read(reg) != value for reg, value in kept.items())
            or word(REGISTRY) != (NEW if growing else OLD) or word(REGISTRY+4) != POOL
            or word(REGISTRY+8) != 1 or word(REGISTRY+12) != expected_capacity
            or word(OLD) != ITEM or growing and word(NEW) != ITEM
            or bytes(u.mem_read(POOL, 12)) != before_pool
            or events != (['allocate', 'copy', 'free', 'pool_reserve'] if growing else [])):
        raise RuntimeError('Original reserve changed its ABI or field layout')
    return {'requested': requested, 'count': 1, 'capacity': expected_capacity,
            'allocator_identity_preserved': True, 'entries_preserved': True,
            'header_write_offsets': [0, 12] if growing else [], 'modeled_operations': events}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for label in PROFILES:
        parser.add_argument('--'+label.replace('_', '-'), type=Path, required=True)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    root, out = Path(__file__).resolve().parents[2], args.output.resolve()
    if not out.is_relative_to((root/'work').resolve()) or out.exists() or not out.parent.is_dir():
        parser.error('Use a fresh output under repository work')
    profiles, method_hashes = {}, {}
    for label in PROFILES:
        image = Image(label, getattr(args, label), types=())
        at = METHODS[label][0]-image.base
        method_hashes[label] = hashlib.sha256(image.mapped[at:at+0x70]).hexdigest()
        profiles[label] = [reserve(image, value) for value in (0, 1, 2, 3, 5)]
        print(json.dumps({'profile': label, 'original_reserve_calls': 5}), flush=True)
        del image
        gc.collect()
    if not profiles['gold'] == profiles['ubisoft'] == profiles['steam_analysis']:
        raise RuntimeError('Native registry reserve semantics differ')
    receipt = {'format': 'native-symbol-registry-layout-v1',
               'fingerprints': {k: v[0] for k, v in PROFILES.items()},
               'reserve_region_sha256': method_hashes,
               'original_reserve_calls': 15, 'layout': {'entries': 0, 'allocator': 4, 'count': 8, 'capacity': 12},
               'modeled_crt_and_pool_helpers': True, 'game_started': False,
               'live_capture_qualified': False, 'crossplay_qualified': False, 'profiles': profiles}
    with out.open('x', encoding='utf-8') as stream:
        json.dump(receipt, stream, indent=2)
    print(json.dumps({k: v for k, v in receipt.items() if k != 'profiles'}))


if __name__ == '__main__':
    main()
