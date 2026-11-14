"""Verify original desync-turn serializers and compiled guards in offline x86.

Synthetic storage only; no game, native factory, heap, sender or network runs.
Native detection turns and sentinels must survive unchanged across editions.
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

EVENT = 'CDesyncDetectionTurnEvent'


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
    writers, guards = {}, {}
    for edition, label in enumerate(native.PROFILES, 1):
        image = native.Image(label, getattr(args, label), types=(EVENT,))
        table, methods = image.tables[EVENT]
        regions = [(table, 24), *((method, 8) for method in methods),
                   (methods[1], 6 if label == 'gold' else 16),
                   (methods[3], 148 if label == 'gold' else 52)]
        guards[label] = check_image(image, fixture, edition,
                                   oracle=b'WePlayDesyncTurnMethodsOracle', methods=regions)
        values = []
        for turn in (0, 1, 0xffffffff):
            for asynchronous in (0, 1):
                wire = native.payload(image, EVENT, scalar=(turn-0x10) & 0xffffffff,
                                      asynchronous=asynchronous)
                expected = struct.pack('<IIBI', 0x11112222, 0x33334444, asynchronous, turn)
                if wire != expected:
                    raise RuntimeError('Original writer changed the native scalar or base fields')
                values.append(wire)
        writers[label] = values
        del image
        gc.collect()
    if any(values != writers['gold'] for values in writers.values()):
        raise RuntimeError('Original desync-turn writers differ across editions')
    readers = malformed = 0
    for label in native.PROFILES:
        image = native.Image(label, getattr(args, label), types=(EVENT,))
        for values in writers.values():
            for wire in values:
                decoded = native.payload(image, EVENT, read_wire=wire)
                expected = (*struct.unpack_from('<II', wire), wire[8],
                            struct.unpack_from('<I', wire, 9)[0], 0, 0, 0, 0)
                if decoded != expected:
                    raise RuntimeError('Original reader changed native turn/base or unrelated fields')
                readers += 1
                for bad in (wire[:-1], wire+b'\0'):
                    try:
                        native.payload(image, EVENT, read_wire=bad)
                    except native.InputBoundError:
                        malformed += 1
                    else:
                        raise RuntimeError('Malformed desync-turn stream accepted')
        print(json.dumps({'profile': label, 'original_readers_passed': True}), flush=True)
        del image
        gc.collect()
    result = {'format': 'native-desync-turn-reader-verification-v1',
              'original_writer_cases': sum(map(len, writers.values())),
              'cross_profile_reader_cases': readers, 'malformed_input_rejections': malformed,
              'compiled_method_guard_checks': guards,
              'guard_fixture_sha256': hashlib.sha256(raw).hexdigest(),
              'native_factory_or_heap_or_sender_executed': False,
              'game_process_started': False, 'synchronized_turns_qualified': False}
    with output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2)
    print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
