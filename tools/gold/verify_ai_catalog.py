"""Verify compiled catalog guards and original x86 visibility/filter branches.

Requires owned, pinned analysis images, pefile and Unicorn. The content-service
result is modeled. Does not launch a game or qualify multiplayer by itself.
"""
from pathlib import Path
import argparse
import hashlib
import json

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX,
    UC_X86_REG_EDX, UC_X86_REG_EDI, UC_X86_REG_EIP)

from emulate_event_payloads import Image
from verify_codec_guards import check_image


def branch_cases(image, entry, length, site, start, following, visible, skip):
    cases = 0
    for patched in (False, True):
        for shown in (False, True):
            for locked in (False, True):
                cpu = Uc(UC_ARCH_X86, UC_MODE_32)
                cpu.mem_map(image.base, (image.size + 4095) & ~4095)
                cpu.mem_write(image.base, image.mapped)
                scratch = 0x20000000
                cpu.mem_map(scratch, 4096)
                cpu.mem_write(scratch + visible, bytes([shown]))
                for register, value in ((UC_X86_REG_ECX, 0), (UC_X86_REG_EDX, 0),
                        (UC_X86_REG_EDI, scratch), (UC_X86_REG_EAX, scratch)):
                    cpu.reg_write(register, value)
                if patched:
                    cpu.mem_write(site, b'\x90\x90')
                reached = []

                def trace(machine, address, size, data):
                    if address == following:
                        # Execute both original policy branches. Only the
                        # external service result between them is modeled.
                        machine.reg_write(UC_X86_REG_EAX, int(locked))
                        machine.reg_write(UC_X86_REG_EIP, site - 2)
                    if address in (skip, site + 2):
                        reached.append(address)
                        machine.emu_stop()

                cpu.hook_add(UC_HOOK_CODE, trace)
                cpu.emu_start(start, skip + 1, count=30)
                expected = site + 2 if shown and (patched or not locked) else skip
                if reached != [expected]:
                    raise AssertionError((image.label, patched, shown, locked, reached))
                body = bytes(cpu.mem_read(entry, length))
                original = image.mapped[entry-image.base:entry-image.base+length]
                changed = [i for i, (a, b) in enumerate(zip(body, original)) if a != b]
                if changed != ([site-entry, site-entry+1] if patched else []):
                    raise AssertionError('Unexpected change outside the content filter')
                cases += 1
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('gold', 'ubisoft', 'steam-analysis', 'fixture'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if not output.is_relative_to((root/'work').resolve()) or output.exists() or not output.parent.is_dir():
        parser.error('Use a fresh output under repository work')
    raw = args.fixture.read_bytes()
    fixture = pefile.PE(data=raw)
    if (fixture.FILE_HEADER.Machine != 0x14c or fixture.OPTIONAL_HEADER.ImageBase != 0x24000000
            or fixture.OPTIONAL_HEADER.SizeOfImage > 4*1024*1024):
        parser.error('Expected a bounded fixed-address x86 catalog fixture')
    rows = []
    for edition, kind, path, entry, length, site, start, following, visible, skip in (
        (1, 'gold', args.gold, 0x5b7990, 944, 0x5b7aed, 0x5b7a8c, 0x5b7a97, 0x6c, 0x5b7b2a),
        (2, 'ubisoft', args.ubisoft, 0x54c800, 900, 0x54c922, 0x54c8e7, 0x54c8f3, 0x74, 0x54c97c),
        (3, 'steam_analysis', args.steam_analysis, 0x54be50, 900, 0x54bf72, 0x54bf37, 0x54bf43, 0x74, 0x54bfcc),
    ):
        image = Image(kind, path, types=())
        guards = check_image(image, fixture, edition, oracle=b'WePlayAICatalogMethodsOracle',
            methods=((entry, length),))
        cases = branch_cases(image, entry, length, site, start, following, visible, skip)
        rows.append(dict(edition=edition, guard_checks=guards, actual_cpu_branch_cases=cases,
            original_visibility_filter_retained=True, external_content_result_modeled=True,
            game_started=False))
        print(kind, guards, 'guard checks and', cases, 'original CPU branch cases passed')
    receipt = dict(fixture_sha256=hashlib.sha256(raw).hexdigest(), editions=rows)
    with output.open('x') as stream:
        json.dump(receipt, stream, indent=2)


if __name__ == '__main__':
    main()
