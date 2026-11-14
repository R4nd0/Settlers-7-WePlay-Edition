"""Verify original native evm accessors without starting a game.

The compiled mode guard runs against fingerprinted analysis images as data.
Only the two tiny original mode accessors additionally execute in bounded x86
emulation. No imports, account/profile files, heap or network are accessed.
"""
import argparse
import json
from pathlib import Path
import struct

import pefile
import emulate_event_payloads as native
from verify_codec_guards import check_image
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX, UC_X86_REG_ECX

PINS = {
    'gold': (0x10e5b79, 0x41c2e0, 0x41c2d0, 6, 8),
    'ubisoft': (0x1378e48, 0x4e7a80, 0x4e7a70, 6, 7),
    'steam_analysis': (0x1375f29, 0x4e7820, 0x4e7810, 6, 7),
}


def verify(image, fixture, edition):
    flag, getter, setter, getter_size, setter_size = PINS[image.label]
    checks = check_image(image, fixture, edition,
                         oracle=b'WePlaySharedModeGuardOracle',
                         methods=[(getter, getter_size), (setter, setter_size)])
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(image.base, (image.size + 4095) & ~4095)
    u.mem_write(image.base, image.mapped)
    u.mem_map(0x30000000, 0x10000)
    stop, stack = 0x3000f000, 0x30008000
    allowed = [(getter, getter + getter_size), (setter, setter + setter_size)]

    def hook(machine, address, size, unused):
        if not any(begin <= address < end for begin, end in allowed):
            raise AssertionError('Unexpected original mode helper target')

    u.hook_add(UC_HOOK_CODE, hook)

    def call(address):
        u.mem_write(stack, struct.pack('<I', stop))
        u.reg_write(UC_X86_REG_ESP, stack)
        u.emu_start(address, stop, count=16)
        assert u.reg_read(UC_X86_REG_ESP) == stack + 4

    for before in (0, 1):
        u.mem_write(flag - 8, b'\x55' * 8 + bytes([before]) + b'\xaa' * 8)
        call(getter)
        assert u.reg_read(UC_X86_REG_EAX) & 255 == before
        u.reg_write(UC_X86_REG_ECX, 0)
        call(setter)
        call(getter)
        assert u.reg_read(UC_X86_REG_EAX) & 255 == 0
        assert bytes(u.mem_read(flag - 8, 17)) == b'\x55' * 8 + b'\0' + b'\xaa' * 8
    return {'guard_checks': checks, 'original_accessor_cases': 2}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gold', type=Path, required=True)
    parser.add_argument('--ubisoft', type=Path, required=True)
    parser.add_argument('--steam-analysis', type=Path, required=True)
    parser.add_argument('--guard-fixture', type=Path, required=True,
                        help='MSVC x86 crossplay_shared_engine_mode_tests compiled at 0x24000000 /FIXED')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if not output.is_relative_to((root / 'work').resolve()):
        parser.error('Private verification artifacts must remain under work/')
    output.mkdir(parents=True, exist_ok=False)
    fixture = pefile.PE(str(args.guard_fixture))
    if fixture.OPTIONAL_HEADER.ImageBase != 0x24000000:
        parser.error('Requires the fixed-address offline guard fixture')
    paths = {'gold': args.gold, 'ubisoft': args.ubisoft,
             'steam_analysis': args.steam_analysis}
    results = {}
    for edition, label in enumerate(native.PROFILES, 1):
        image = native.Image(label, paths[label], types=())
        results[label] = verify(image, fixture, edition)
        print(label, results[label], flush=True)
    result = {'profiles': results, 'game_started': False,
              'original_code_executed_on_host': False}
    (output / 'result.private.json').write_text(json.dumps(result, indent=2))
    print('PASS all original native mode accessor guards and bounded zero-setter cases')


if __name__ == '__main__':
    main()
