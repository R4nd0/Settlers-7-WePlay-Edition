"""Execute original bounded turn-batch readers headlessly, with synthetic storage.

No native heap, game process, factory, local sender or network is executed.
Checks scalar preservation and original nested constructors across all editions.
"""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import struct

import pefile
import emulate_event_payloads as native
from verify_codec_guards import check_image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for label in native.PROFILES:
        parser.add_argument('--'+label.replace('_', '-'), type=Path, required=True)
    parser.add_argument('--thunk-executable', type=Path, required=True)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if not output.is_relative_to((root/'work').resolve()) or output.exists() or not output.parent.is_dir():
        parser.error('Use a fresh output under repository work')
    raw = args.thunk_executable.read_bytes()
    fixture = pefile.PE(data=raw)
    if (fixture.FILE_HEADER.Machine != 0x14c or fixture.OPTIONAL_HEADER.ImageBase != 0x24000000
            or fixture.OPTIONAL_HEADER.SizeOfImage > 4*1024*1024):
        raise ValueError('Expected bounded fixed x86 guard fixture')
    writers = {}
    guards = {}
    for edition, label in enumerate(native.PROFILES, 1):
        image = native.Image(label, getattr(args, label))
        regions = []
        for name, length in (('CEndOfTurnBatchEvent', 507 if label == 'gold' else 83),
                             ('CEndOfTurnEvent', 233 if label == 'gold' else 160)):
            table, methods = image.tables[name]
            regions.extend(((table, 24), *((method, 8) for method in methods),
                            (methods[1], 6 if label == 'gold' else 16), (methods[3], length)))
        guards[label] = check_image(image, fixture, edition, oracle=b'WePlayTurnBatchMethodsOracle', methods=regions)
        writers[label] = [native.payload(image, 'CEndOfTurnBatchEvent', nested, scalar, asynchronous)
                          for nested in (False, True) for scalar in (0, 0xfffffff0)
                          for asynchronous in (0, 1)]
        del image
        gc.collect()
    if any(values != writers['gold'] for values in writers.values()):
        raise RuntimeError('Original batch writers differ across editions')
    readers = malformed = bounds = 0
    for label in native.PROFILES:
        image = native.Image(label, getattr(args, label))
        for values in writers.values():
            for wire in values:
                count = struct.unpack_from('<I', wire, 13)[0]
                decoded = native.payload(image, 'CEndOfTurnBatchEvent', read_wire=wire, read_batch=True)
                expected_base = (*struct.unpack_from('<II', wire), wire[8], struct.unpack_from('<I', wire, 9)[0])
                if decoded['base'] != expected_base or len(decoded['nested']) != count:
                    raise RuntimeError('Original reader base fields/count differ')
                for index, actual in enumerate(decoded['nested']):
                    at = 17+29*index
                    expected = (*struct.unpack_from('<II', wire, at), wire[at+8],
                                *struct.unpack_from('<IIIII', wire, at+9))
                    if actual != expected:
                        raise RuntimeError('Original nested reader changed scalar/checksum bits')
                readers += 1
                for bad in (wire[:-1], wire+b'\0'):
                    try:
                        native.payload(image, 'CEndOfTurnBatchEvent', read_wire=bad, read_batch=True)
                    except native.InputBoundError:
                        malformed += 1
                    else:
                        raise RuntimeError('Malformed batch stream accepted')
        seed = writers['gold'][-1]
        for count in (1, 64):
            wire = seed[:13]+struct.pack('<I', count)+seed[17:46]*count
            decoded = native.payload(image, 'CEndOfTurnBatchEvent', read_wire=wire, read_batch=True)
            if len(decoded['nested']) != count or len(set(decoded['nested'])) != 1:
                raise RuntimeError('Original vector boundary differs')
            bounds += 1
        for bad in (seed[:13]+struct.pack('<I', 65), seed[:13]+struct.pack('<I', 0xffffffff), seed[:16]):
            try:
                native.payload(image, 'CEndOfTurnBatchEvent', read_wire=bad, read_batch=True)
            except native.InputBoundError:
                bounds += 1
            else:
                raise RuntimeError('Unbounded batch count/header accepted')
        print(json.dumps({'profile': label, 'original_readers_passed': True}), flush=True)
        del image
        gc.collect()
    result = {'format': 'native-turn-batch-reader-verification-v1',
              'original_writer_cases': sum(map(len, writers.values())),
              'cross_profile_reader_cases': readers, 'malformed_input_rejections': malformed,
              'vector_boundary_checks': bounds, 'maximum_nested_events': 64,
              'compiled_method_guard_checks': guards,
              'guard_fixture_sha256': hashlib.sha256(raw).hexdigest(),
              'original_vector_resize_constructors_and_readers_executed': True,
              'native_heap_or_factory_or_sender_executed': False,
              'game_process_started': False, 'synchronized_turns_qualified': False}
    with output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2)
    print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
