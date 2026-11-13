"""Audit nonempty original enclosing rules in bounded offline x86 emulation.

Original catalogue lookup, CString hashing/copy, victory suppression and sector
writers execute. Catalogue objects, CRT heap/copy and streams are synthetic.
This is not a live setup reader, shared world, NEO or save/cold-reload test.
"""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import pefile
from emulate_active_players import Image, PROFILES, LAYOUTS, PREPARATION_WRITERS, SETUP_ROOT_WRITERS, survey
from emulate_setup_root_writer import RULES, compare, reviewed
from setup_rule_vectors import METHODS
from verify_codec_guards import check_image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for label in PROFILES:
        parser.add_argument('--'+label.replace('_','-'), type=Path, required=True)
    parser.add_argument('--thunk-executable', type=Path, required=True)
    parser.add_argument('--expected-fixtures', type=Path, default=Path(__file__).resolve().parents[2]/
                        'tests/fixtures/gold-native-setup-rule-vectors.json')
    parser.add_argument('output', type=Path)
    args = parser.parse_args();root = Path(__file__).resolve().parents[2];out = args.output.resolve()
    if not out.is_relative_to((root/'work').resolve()) or out.exists() or not out.parent.is_dir():
        parser.error('Use a fresh output under repository work')
    raw = args.thunk_executable.read_bytes();fixture = pefile.PE(data=raw)
    if (fixture.FILE_HEADER.Machine != 0x14c or fixture.OPTIONAL_HEADER.ImageBase != 0x24000000
            or fixture.OPTIONAL_HEADER.SizeOfImage > 4*1024*1024):
        raise ValueError('Expected the bounded fixed x86 fixture')
    profiles, guards = {}, {}
    for edition, label in enumerate(PROFILES, 1):
        image = Image(label, getattr(args,label), types=())
        castle = {'gold':0x5273a0,'ubisoft':0x586b90,'steam_analysis':0x586650}[label]
        regions = ((SETUP_ROOT_WRITERS[label],0x90),(RULES[label],4096),
                   (PREPARATION_WRITERS[label],0x330 if label=='gold' else 0x280),
                   (LAYOUTS[label][0],0x3000),(castle,0x2000)) + METHODS[label]
        guards[label] = check_image(image,fixture,edition,oracle=b'WePlaySetupRootMethodsOracle',
                                    methods=regions,instruction_limit=2000000)
        cases = {}
        for version in (1,2):
            for rich in (False,True):
                case = f'v{version}-'+('populated' if rich else 'empty')+'-players'
                options = {'preparation':True,'setup_root':True,'rule_vectors':True}
                cases[case] = {**survey(image,version,rich,False,fixture,**options),
                              'binary':survey(image,version,rich,True,fixture,**options)}
        profiles[label] = cases
        print(json.dumps({'profile':label,'original_root_calls':8,'guard_checks':guards[label]}),flush=True)
        del image;gc.collect()
    compare(profiles)
    if (args.expected_fixtures.stat().st_size > 16384
            or json.loads(args.expected_fixtures.read_text(encoding='utf-8')) != reviewed(profiles)):
        raise RuntimeError('Nonempty rules differ from reviewed synthetic fixtures')
    receipt = {'format':'native-setup-rule-vectors-audit-v1',
        'fingerprints':{k:v[0] for k,v in PROFILES.items()},'fixture_sha256':hashlib.sha256(raw).hexdigest(),
        'original_root_calls':24,'original_whole_preparation_calls':24,'original_row_calls':600,
        'guard_checks':guards,'nonempty_victory_rules':2,'nonempty_sector_rules':2,'placements_per_sector':2,
        'original_catalogue_lookup_and_string_hash_executed':True,'original_type3_suppression_executed':True,
        'crt_allocations_per_call':61,'crt_heap_and_copy_modeled':True,'catalogues_and_stream_modeled':True,
        'root_seed_writeback_preserves_value':True,'native_inputs_stack_and_registers_preserved':True,
        'live_enclosing_setup_qualified':False,'native_reader_qualified':False,'shared_world_started':False,
        'gold_neo_qualified':False,'mixed_save_coldreload_qualified':False,'profiles':profiles}
    with out.open('x',encoding='utf-8') as stream:
        json.dump(receipt,stream,indent=2)
    print(json.dumps({k:v for k,v in receipt.items() if k!='profiles'}))


if __name__=='__main__':
    main()
