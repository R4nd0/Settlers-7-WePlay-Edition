"""Audit original complete player preparation writers without starting a game.

Visits every native row and both trailing position lists. Owned synthetic data
tests ABI/serialization only, not live setup ownership, native reading, content
agreement, world start, NEO or save/reload interoperability.
"""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import pefile

from emulate_active_players import Image, PROFILES, LAYOUTS, PREPARATION_WRITERS, survey
from verify_codec_guards import check_image

CASES = {f'v{version}-{kind}' for version in (1, 8) for kind in ('empty', 'populated', 'long-allies')}


def compare(profiles):
    if set(profiles) != set(PROFILES) or any(set(value) != CASES for value in profiles.values()):
        raise ValueError('All three profiles and six preparation cases are required')
    for case in CASES:
        reference = profiles['gold'][case]
        signature = lambda fields: [{k: v for k, v in field.items() if k != 'offset'} for field in fields]
        for label in ('ubisoft', 'steam_analysis'):
            other = profiles[label][case]
            if signature(reference['fields']) != signature(other['fields']) or reference['binary'] != other['binary']:
                raise RuntimeError('Original whole preparation differs: '+label+'/'+case)
            if label == 'steam_analysis' and other != profiles['ubisoft'][case]:
                raise RuntimeError('History preparation layouts differ')


def reviewed(profiles):
    return {case: {'binary': value['binary'],
                   'schema_sha256': hashlib.sha256(json.dumps(
                       [{k: v for k, v in field.items() if k != 'offset'} for field in value['fields']],
                       sort_keys=True, separators=(',', ':')).encode()).hexdigest(),
                   'field_events': len(value['fields'])}
            for case, value in profiles['gold'].items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for label in PROFILES:
        parser.add_argument('--'+label.replace('_', '-'), type=Path, required=True)
    parser.add_argument('--thunk-executable', type=Path, required=True)
    parser.add_argument('--expected-fixtures', type=Path, default=Path(__file__).resolve().parents[2]/
                        'tests/fixtures/gold-native-preparation-writer.json')
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
        whole, size = PREPARATION_WRITERS[label], 0x330 if label == 'gold' else 0x280
        castle = {'gold': 0x5273a0, 'ubisoft': 0x586b90, 'steam_analysis': 0x586650}[label]
        guards[label] = check_image(image, fixture, edition, oracle=b'WePlayPreparationMethodsOracle',
            methods=((whole, size), (LAYOUTS[label][0], 0x3000), (castle, 0x2000)), instruction_limit=2000000)
        cases = {}
        for version in (1, 8):
            for kind in ('empty', 'populated', 'long-allies'):
                rich = kind != 'empty'
                case = f'v{version}-'+kind
                options = {'preparation': True, 'long_allies': kind == 'long-allies'}
                cases[case] = {**survey(image, version, rich, False, fixture, **options),
                              'binary': survey(image, version, rich, True, fixture, **options)}
        profiles[label] = cases
        print(json.dumps({'profile': label, 'original_whole_calls': 12, 'original_row_calls': 300,
                          'guard_checks': guards[label]}), flush=True)
        del image
        gc.collect()
    compare(profiles)
    if args.expected_fixtures.stat().st_size > 16384 or json.loads(args.expected_fixtures.read_text(encoding='utf-8')) != reviewed(profiles):
        raise RuntimeError('Whole preparation differs from reviewed synthetic fixtures')
    receipt = {'format': 'native-whole-preparation-writer-audit-v1',
        'fingerprints': {k: v[0] for k, v in PROFILES.items()},
        'original_whole_calls': 36, 'original_row_calls': 900, 'rows_per_call': 25,
        'position_lists_per_call': 2, 'compiled_thunks_executed': True,
        'fixture_sha256': hashlib.sha256(raw).hexdigest(), 'guard_checks': guards,
        'original_binary_visitors_executed': True, 'heap_and_stream_modeled': True,
        'live_format_version_qualified': False, 'enclosing_rules_or_setup_root_captured': False,
        'native_reader_tested': False, 'game_process_started': False,
        'shared_world_started': False, 'gold_neo_qualified': False,
        'mixed_save_coldreload_qualified': False, 'crossplay_qualified': False, 'profiles': profiles}
    with out.open('x', encoding='utf-8') as stream:
        json.dump(receipt, stream, indent=2)
    print(json.dumps({k: v for k, v in receipt.items() if k != 'profiles'}))


if __name__ == '__main__':
    main()
