"""Check native end-of-turn readers against all three original payload writers.

Bounded offline emulation only. The stream and reflection helpers are synthetic;
native factories, live transports, command dispatch and crossplay are not tested.
Input-bound rejections belong to the synthetic stream, not the native parser.
"""
import argparse
import json
from pathlib import Path

from emulate_event_payloads import EVENTS, PROFILES, Image, InputBoundError, payload


def compare_receivers(images):
    fixtures = json.loads((Path(__file__).resolve().parents[2]/
        'tests/fixtures/gold-native-event-payloads.json').read_text(encoding='utf-8'))['fixtures'][:2]
    cases, rejections = [], 0
    for fixture in fixtures:
        if fixture['event'] != EVENTS[0] or fixture['nested']:
            raise ValueError('Expected the mapped scalar end-of-turn fixtures')
        scalar, asynchronous = fixture['scalar'], fixture['asynchronous']
        expected = (0x11112222, 0x33334444, asynchronous,
                    *((scalar+offset) & 0xffffffff for offset in range(0x10, 0x24, 4)))
        wires = {source.label: payload(source, EVENTS[0], scalar=scalar, asynchronous=asynchronous)
                 for source in images}
        if any(wire != bytes.fromhex(fixture['hex']) for wire in wires.values()):
            raise RuntimeError('Original writer fixture changed')
        for source, wire in wires.items():
            for target in images:
                observed = payload(target, EVENTS[0], read_wire=wire)
                if observed != expected:
                    raise RuntimeError('Cross-edition native reader field mismatch')
                cases.append({'source': source, 'target': target.label, 'scalar': scalar,
                              'asynchronous': asynchronous, 'bytes_consumed': len(wire),
                              'all_eight_fields_equal': True})
        # Identical payload bytes need only one malformed-input sweep per reader.
        wire = next(iter(wires.values()))
        for target in images:
            for malformed in [wire[:end] for end in range(len(wire))] + [wire+b'\0']:
                try:
                    payload(target, EVENTS[0], read_wire=malformed)
                except InputBoundError:
                    rejections += 1
                else:
                    raise RuntimeError('Synthetic input boundary accepted an invalid length')
    return {'format': 'native-end-of-turn-receiver-oracle-v1',
            'fingerprints': {item.label: item.pin for item in images},
            'original_writer_executions': len(fixtures)*len(images),
            'successful_original_reader_executions': len(cases),
            'synthetic_input_boundary_rejections': rejections,
            'native_event_visitors_and_readers': True,
            'stack_nonvolatile_and_output_bounds_checked': True,
            'synthetic_input_stream_and_reflection_helpers': True,
            'native_factory_executed': False, 'actual_game_test': False,
            'crossplay_qualified': False, 'cases': cases}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for label in PROFILES:
        parser.add_argument('--'+label.replace('_', '-'), required=True, type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    work = Path(__file__).resolve().parents[2]/'work'
    if not output.is_relative_to(work.resolve()) or output.exists() or not output.parent.is_dir():
        parser.error('Use a new output file in an existing repository work directory')
    result = compare_receivers([Image(label, getattr(args, label)) for label in PROFILES])
    with output.open('x', encoding='utf-8') as stream:
        stream.write(json.dumps(result, indent=2)+'\n')
    print(json.dumps({key: value for key, value in result.items() if key not in ('cases', 'fingerprints')}))


if __name__ == '__main__':
    main()
