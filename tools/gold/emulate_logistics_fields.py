"""Audit native Gold/History logistics task fields in bounded offline x86.

Original task serializers execute against a modeled recording visitor and
reflection name. Nested nodes are skipped and containers are empty. This is
layout evidence, not a NEO port, wire/save compatibility or a gameplay test.
Requires pefile and Unicorn; game images stay private and are fingerprinted.
"""
import argparse
import gc
import json
from pathlib import Path
import struct

from emulate_event_payloads import Image, PROFILES

TASKS = ('CRequestTask@ELLog', 'CProductionTask@ELLog')
REFLECTION_IAT = {'gold': 0xebf574, 'ubisoft': 0x105160c,
                  'steam_analysis': 0x104f620}
S, STACK = 0x20000000, 0x30000000
OBJECT, VISITOR, TABLE, NAME, REFLECT, STOP = (
    S + offset for offset in (0x1000, 0x2000, 0x3000, 0x4000, 0x5000, 0x6000))


def survey(image, name):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
    from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_EBP,
        UC_X86_REG_ECX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_ESP, UC_X86_REG_EIP)
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(image.base, (image.size + 4095) & ~4095)
    u.mem_write(image.base, image.mapped)
    u.mem_map(0, 4096)  # Emulated SEH head only.
    u.mem_map(S, 0x10000)
    u.mem_map(STACK, 0x10000)

    def words(at, *values):
        u.mem_write(at, struct.pack('<' + 'I' * len(values), *values))

    def word(at):
        return struct.unpack('<I', u.mem_read(at, 4))[0]

    def text(at):
        if not (image.base <= at <= image.base + image.size - 128
                or S <= at <= S + 0x10000 - 128):
            raise RuntimeError('Foreign field name')
        raw = bytes(u.mem_read(at, 128))
        end = raw.find(b'\0')
        if end < 0 or any(not 32 <= b < 127 for b in raw[:end]):
            raise RuntimeError('Unbounded/non-ASCII field name')
        return raw[:end].decode('ascii')

    table, methods = image.tables[name]
    words(OBJECT, table)
    words(VISITOR, TABLE, 0)  # Original serializers take their write branch.
    if image.label != 'gold' and name == TASKS[0]:
        # A zero pointer is not a valid empty HE tree. Owned head nodes have
        # self links and IsNil set; no tree entries or allocator are modeled.
        for offset, head in ((0x98, OBJECT + 0x300), (0xa0, OBJECT + 0x320)):
            words(OBJECT + offset, head, 0)
            words(head, head, head, head)
            u.mem_write(head + 13, b'\x01')
    u.mem_write(NAME, b'class SyntheticTask\0')
    for index in range(39):
        words(TABLE + index * 4, S + 0x7000 + index * 16)
    words(REFLECTION_IAT[image.label], REFLECT)
    fields = []
    completed = False

    def returned(count, value=0):
        sp = u.reg_read(UC_X86_REG_ESP)
        u.reg_write(UC_X86_REG_EAX, value)
        u.reg_write(UC_X86_REG_EIP, word(sp))
        u.reg_write(UC_X86_REG_ESP, sp + 4 + count)

    def code(uc, at, size, data):
        nonlocal completed
        if at == STOP:
            completed = True
            uc.emu_stop()
        elif at == REFLECT or at == image.name_helper:
            returned(4 if image.label == 'gold' else 0, NAME)
        elif at == image.compare_helper:
            returned(0, 1)
        elif S + 0x7000 <= at < S + 0x7000 + 39 * 16 and (at - S - 0x7000) % 16 == 0:
            index = (at - S - 0x7000) // 16
            sp = uc.reg_read(UC_X86_REG_ESP)
            if uc.reg_read(UC_X86_REG_ECX) != VISITOR:
                raise RuntimeError('Foreign visitor')
            if index == 1:
                fields.append({'kind': 'node', 'name': text(word(sp + 4))})
                returned(16, 0)  # Deliberately skip nested node bodies.
            elif index in (2, 4):
                returned(0)
            elif index == 3:
                if word(sp + 8) != 0:
                    raise RuntimeError('Nonempty container outside fixture')
                fields.append({'kind': 'container', 'name': text(word(sp + 4)), 'count': 0})
                returned(12, 0)
            elif index in (0x34 // 4, 0x3c // 4, 0x44 // 4, 0x58 // 4):
                pointer = word(sp + 4)
                width = {0x34: 4, 0x3c: 8, 0x44: 4, 0x58: 1}[index * 4]
                if OBJECT <= pointer <= OBJECT + 0x300 - width:
                    offset = pointer - OBJECT
                elif STACK <= pointer <= STACK + 0x10000 - width:
                    offset = None  # A temporary, not an inferred object offset.
                else:
                    raise RuntimeError('Foreign field pointer')
                fields.append({'kind': 'field', 'visitor_slot': index * 4,
                               'name': text(word(sp + 8)), 'offset': offset})
                returned(12)
            else:
                raise RuntimeError('Unexpected visitor method')
        elif not image.base <= at < image.base + image.size:
            raise RuntimeError('Foreign code')

    def write(uc, access, at, size, value, data):
        if not (at == 0 and size == 4 or STACK <= at <= at + size <= STACK + 0x10000
                or OBJECT <= at <= at + size <= OBJECT + 0x300):
            raise RuntimeError('Foreign write')

    u.hook_add(UC_HOOK_CODE, code)
    u.hook_add(UC_HOOK_MEM_WRITE, write)
    sp = STACK + 0xf000
    words(sp, STOP, VISITOR, 0)
    kept = {UC_X86_REG_EBX: 0x12345678, UC_X86_REG_EBP: 0x23456789,
            UC_X86_REG_ESI: 0x34567890, UC_X86_REG_EDI: 0x456789ab}
    for reg, value in {**kept, UC_X86_REG_ESP: sp, UC_X86_REG_ECX: OBJECT}.items():
        u.reg_write(reg, value)
    before = bytes(u.mem_read(OBJECT, 0x400))
    u.emu_start(methods[3], STOP + 1, count=200000, timeout=3_000_000)
    if (not completed or u.reg_read(UC_X86_REG_EIP) != STOP
            or u.reg_read(UC_X86_REG_ESP) != sp + 12 or word(0) != 0
            or any(u.reg_read(k) != v for k, v in kept.items())
            or bytes(u.mem_read(OBJECT, 0x400)) != before):
        raise RuntimeError('Incomplete serializer or changed ABI/input state')
    return fields


def compare(profiles):
    """Match field tags/types in order; never infer a constant object shift."""
    if set(profiles) != set(PROFILES):
        raise ValueError('All three analysis profiles are required')
    result = {}
    for name in TASKS:
        gold = profiles['gold'][name]
        for label in ('ubisoft', 'steam_analysis'):
            other = profiles[label][name]
            signature = lambda rows: [{k: v for k, v in row.items() if k != 'offset'}
                                      for row in rows]
            if signature(other) != signature(gold):
                raise ValueError('Serializer field schema mismatch: ' + label + '/' + name)
            if label == 'steam_analysis' and other != profiles['ubisoft'][name]:
                raise ValueError('History task layouts differ')
        result[name] = [{'name': row['name'], 'visitor_slot': row['visitor_slot'],
                         'gold_offset': row['offset'],
                         'history_offset': profiles['ubisoft'][name][index]['offset']}
                        for index, row in enumerate(gold) if row['kind'] == 'field']
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for label in PROFILES:
        parser.add_argument('--' + label.replace('_', '-'), type=Path, required=True)
    parser.add_argument('--expected-fixtures', type=Path,
                        default=Path(__file__).resolve().parents[2] /
                        'tests/fixtures/gold-native-logistics-fields.json')
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    out = args.output.resolve()
    if (not out.is_relative_to((root / 'work').resolve()) or out.exists()
            or not out.parent.is_dir()):
        parser.error('Use a fresh output under repository work')
    profiles = {}
    for label in PROFILES:
        image = Image(label, getattr(args, label), types=TASKS)
        profiles[label] = {name: survey(image, name) for name in TASKS}
        print(json.dumps({'profile': label, 'original_task_serializers_completed': len(TASKS)}),
              flush=True)
        del image
        gc.collect()
    mapping = compare(profiles)
    if (args.expected_fixtures.stat().st_size > 65536
            or json.loads(args.expected_fixtures.read_text(encoding='utf-8')) != mapping):
        raise RuntimeError('Task field mapping differs from the reviewed fixtures')
    receipt = {'format': 'native-logistics-field-audit-v1',
               'fingerprints': {label: pin[0] for label, pin in PROFILES.items()},
               'native_executions': len(TASKS) * len(PROFILES),
               'recording_visitor_and_reflection_modeled': True,
               'nested_nodes_traversed': False, 'nonempty_containers_tested': False,
               'game_process_started': False, 'neo_port_qualified': False,
               'crossplay_qualified': False, 'field_mapping': mapping,
               'profiles': profiles}
    with out.open('x') as stream:
        json.dump(receipt, stream, indent=2)
    print(json.dumps({k: v for k, v in receipt.items() if k not in ('profiles', 'field_mapping')}))


if __name__ == '__main__':
    main()
