"""Verify original placement-command writer layouts in bounded x86 emulation.

No game, original host execution, account, native heap or network is accessed.
This verifies synthetic writer outputs; runtime admission and native enqueue
need separate actual paired-game tests.
"""
import argparse
import json
from pathlib import Path
import struct

import emulate_event_payloads as native


def guard_regions(image, road=False):
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)

    def body(address):
        for instruction in decoder.disasm(image.mapped[address-image.base:address-image.base+1600], address):
            if instruction.mnemonic.startswith('ret'):
                return address, instruction.address + instruction.size - address
        raise AssertionError('Original serializer return boundary not found')

    regions = []
    for name in ('CLogicStateEvent', 'CPosOrientationTypeAndCategoryEvent@ELBase'):
        table, methods = image.tables[name]
        regions.append((table, 24))
        regions.extend((address, 8) for address in methods)
        regions.extend(body(methods[slot]) for slot in (1, 3))
    for name in ('CLogicEvent@ELBase', 'CPosAndOrientationEvent@ELBase'):
        regions.append(body(image.tables[name][1][3]))
    if road:
        table, methods = image.tables['CPlaceStreetEvent@ELBase']
        regions.append((table, 24))
        regions.extend((address, 8) for address in methods)
        regions.extend(body(methods[slot]) for slot in (1, 3))
    return regions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for label in native.PROFILES:
        parser.add_argument('--' + label.replace('_', '-'), type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--kind', choices=('storehouse', 'road'), default='storehouse')
    parser.add_argument('--guard-fixture', type=Path,
                        help='Optional MSVC x86 codec test fixture linked /BASE:0x24000000 /FIXED')
    args = parser.parse_args()
    output = args.output.resolve()
    work = Path(__file__).resolve().parents[2] / 'work'
    if not output.is_relative_to(work.resolve()):
        parser.error('Private oracle receipts must remain under work/')
    output.mkdir(parents=True, exist_ok=False)
    results = {}
    fixture = None
    if args.guard_fixture:
        import pefile
        fixture = pefile.PE(str(args.guard_fixture))
        if fixture.OPTIONAL_HEADER.ImageBase != 0x24000000:
            parser.error('Guard oracle requires the fixed-address offline fixture')
    road = args.kind == 'road'
    nested = 'CPlaceStreetEvent@ELBase' if road else 'CPosOrientationTypeAndCategoryEvent@ELBase'
    for label in native.PROFILES:
        image = native.Image(label, getattr(args, label), types=(
            'CLogicStateEvent', 'CPosOrientationTypeAndCategoryEvent@ELBase',
            'CLogicEvent@ELBase', 'CPosAndOrientationEvent@ELBase',
            *((nested,) if road else ())))
        count = 0
        for player in (1, 2):
            for asynchronous in (0, 1):
                wire = struct.pack('<I', 0xdda00d09) + native.payload(
                    image, 'CLogicStateEvent', True, asynchronous=asynchronous,
                    nested_event=nested, nested_player=player)
                nested_class = 0x0bf3c089 if road else 0x63a02a69
                subtype = 2 | ((11 if road else 4) << 16)
                fields = ((1, 0x472fc000, 0x47435000, 0x47300000, 0x47440000) if road
                          else (0x472fc000, 0x47435000, 0x80000000, 260, 14))
                expected = struct.pack('<IIIBIIBIIIIIIB',
                    0xdda00d09, 7 | (11 << 16), 42, asynchronous,
                    nested_class, subtype, player, *fields, player, 1)
                if len(wire) != 47 or wire != expected:
                    raise AssertionError('Original placement writer layout mismatch: ' + label)
                count += 1
        results[label] = {'original_writer_cases': count, 'wire_bytes': 47}
        if fixture is not None:
            from verify_codec_guards import check_image
            edition = list(native.PROFILES).index(label) + 1
            oracle = b'WePlayRoadCommandGuardOracle' if road else b'WePlayBuildCommandGuardOracle'
            results[label]['compiled_guard_checks'] = check_image(
                image, fixture, edition, oracle=oracle, methods=guard_regions(image, road=road))
        print(label, results[label], flush=True)
    result = {'kind': args.kind, 'profiles': results, 'game_started': False, 'actual_command_delivered': False}
    (output / 'result.private.json').write_text(json.dumps(result, indent=2))
    print('PASS original ' + args.kind + '-command writer layout in all three editions')


if __name__ == '__main__':
    main()
