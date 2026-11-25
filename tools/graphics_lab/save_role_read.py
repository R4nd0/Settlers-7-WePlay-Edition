"""Read packed Steam's original save-range registration in an owned live case.

Query/read only: no Lua/native calls, thread creation, process control or saves.
The receipt is staged provenance, not an automatic save-classification waiver.
"""
from __future__ import annotations

import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
from pathlib import Path
import struct
import time

from . import camera_read, reconfigure as scope

STEAM_SHA = '8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410'
IMAGE_SIZE = 0x1106000
NAME_RVA = 0xd32a3c
DATA_RVA, DATA_BYTES = 0xefa000, 0xb0f48
RDATA_RVA, RDATA_BYTES = 0xc4f000, 0x2aa01a
CALLBACK_TEMPLATE_FNV = 0xaeca9f257e29c819
LUA_SINK_SHA = 'd2d2459bbf1c58bad7ea5b1aca804ab3a8f90f4e48f0a5c0bbea03e21c2dfb33'
NAME = b'GetSaveGameSlotRangeSP\0'


def _word(read, address):
    return struct.unpack('<I', read(address, 4))[0]


def _callback(read, base, callback):
    if not base + 0x1000 <= callback <= base + 0xc42ef0 - 66:
        raise ValueError('Original save-range callback is outside guarded executable text.')
    code = read(callback, 66)
    template = bytearray(code)
    for offset in (10, 26, 34, 50):
        template[offset:offset+4] = bytes(4)
    if camera_read.fnv(template) != CALLBACK_TEMPLATE_FNV:
        raise ValueError('Original save-range callback instruction template changed.')
    addresses = [struct.unpack_from('<I', code, offset)[0] for offset in (10, 34)]
    if any(not base + RDATA_RVA <= address <= base + RDATA_RVA + RDATA_BYTES - 8
           or address % 8 for address in addresses):
        raise ValueError('Save-range constants are outside original read-only data.')
    constants = [read(address, 8) for address in addresses]
    values = [struct.unpack('<d', value)[0] for value in constants]
    if values != [30.0, 50.0]:
        raise ValueError('Original save-range first/count are not the verified policy.')
    sinks = [callback + offset + 4 + struct.unpack_from('<i', code, offset)[0] for offset in (26, 50)]
    if len(set(sinks)) != 1 or not base + 0x1000 <= sinks[0] <= base + 0xc42ef0 - 56:
        raise ValueError('The two original Lua number pushes do not share a valid sink.')
    sink = read(sinks[0], 56)
    if hashlib.sha256(sink).hexdigest() != LUA_SINK_SHA:
        raise ValueError('Original Lua-number insertion instruction guard changed.')
    if read(callback, 66) != code or any(read(address, 8) != value for address, value in zip(addresses, constants)):
        raise ValueError('Original save-range code/constants changed during observation.')
    normalized = camera_read.normalized_code(code, base, (10, 34))
    return {'callback_rva': callback-base, 'callback_bytes': 66,
            'normalized_callback_sha256': hashlib.sha256(normalized).hexdigest(),
            'template_fnv64': f'{CALLBACK_TEMPLATE_FNV:016x}',
            'first_rva': addresses[0]-base, 'count_rva': addresses[1]-base,
            'first': 30, 'count': 50, 'lua_number_sink_rva': sinks[0]-base,
            'lua_number_sink_bytes': 56, 'lua_number_sink_sha256': LUA_SINK_SHA}


def read_policy(read, base):
    """Pure bounded RPM consumer, including exact original unpacked code shape."""
    if type(base) is not int or not 0x10000 <= base <= 0xffffffff - IMAGE_SIZE:
        raise ValueError('Invalid original Steam image base.')
    if read(base + NAME_RVA, len(NAME)) != NAME:
        raise ValueError('Original save-range registration name changed.')
    needle, candidates = struct.pack('<I', base + NAME_RVA), []
    for start in range(0, DATA_BYTES, 65536):
        data = read(base + DATA_RVA + start, min(65536, DATA_BYTES-start))
        cursor = 0
        while True:
            found = data.find(needle, cursor)
            if found < 0:
                break
            cursor = found + 1
            slot = base + DATA_RVA + start + found
            if slot % 4:
                continue
            callback = _word(read, slot+4)
            try:
                result = _callback(read, base, callback)
            except ValueError:
                continue
            if read(slot, 8) != struct.pack('<2I', base+NAME_RVA, callback):
                raise ValueError('Original save-range registration changed during observation.')
            result['registration'] = {'name': NAME[:-1].decode('ascii'), 'name_rva': NAME_RVA,
                'name_slot_rva': slot-base, 'callback_rva': callback-base,
                'normalized_sha256': hashlib.sha256(struct.pack('<2I', 0x400000+NAME_RVA, 0x400000+callback-base)).hexdigest()}
            candidates.append(result)
    if len(candidates) != 1:
        raise ValueError('A unique exact original Steam save-range binding is required.')
    return candidates[0]


def sample(case, dll):
    files = scope._case_files(case, dll)
    if files['owned']['sha256'] != STEAM_SHA:
        raise ValueError('This packed-registration reader only admits original Steam History.')
    api = scope.isolated_process.Windows()
    scope._bind(api)
    handle = api.check(api.k.OpenProcess(0x410, False, files['owned']['pid']))
    def read(address, size):
        if not 0x10000 <= address <= 0xffffffff-size or not 1 <= size <= 65536:
            raise ValueError('Save-range RPM request exceeds its bounds.')
        data, done = C.create_string_buffer(size), C.c_size_t()
        api.check(api.k.ReadProcessMemory(handle, address, data, size, C.byref(done)))
        if done.value != size:
            raise RuntimeError('Incomplete original save-role read.')
        return data.raw
    try:
        creation = scope.validate_live_state(scope._live_state(api, handle, files['owned']['pid'], files['owned']['parent']), files)
        peb, count = C.c_size_t(), W.ULONG()
        if api.nt.NtQueryInformationProcess(handle, 26, C.byref(peb), C.sizeof(peb), C.byref(count)) or not peb.value:
            raise RuntimeError('The original target must be x86.')
        base = _word(read, peb.value+8)
        header = read(base, 4096)
        pe = struct.unpack_from('<I', header, 0x3c)[0]
        native = scope.isolated_process.PROFILES[STEAM_SHA]
        if (header[:2] != b'MZ' or not 64 <= pe < 3000 or header[pe:pe+6] != b'PE\0\0\x4c\x01'
                or struct.unpack_from('<I', header, pe+8)[0] != native[1]
                or struct.unpack_from('<I', header, pe+0x50)[0] != native[2]):
            raise ValueError('The loaded original Steam image header changed.')
        started = time.monotonic_ns()
        result = read_policy(read, base)
        if scope.validate_live_state(scope._live_state(api, handle, files['owned']['pid'], files['owned']['parent']), files) != creation:
            raise RuntimeError('Owned process identity changed during save-role observation.')
        return {'format': 's7-original-steam-save-role-read-v1', 'read_only': True,
                'identity': {key: files['owned'][key] for key in ('pid', 'parent', 'sha256')},
                'edition': 'steam-history', 'observer_started_monotonic_ns': started,
                'observer_finished_monotonic_ns': time.monotonic_ns(),
                'runtime_mapping': 'original registration plus complete verified instruction template',
                'automatic_classification_accepted': False, 'restoration_performed': False, **result}
    finally:
        api.k.CloseHandle(handle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', type=Path, required=True)
    parser.add_argument('--dll', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        output = scope._lab(args.output)
        if output.parent != scope._lab(args.case) or output.exists():
            raise ValueError('Use a new private observation directly inside the existing owned case.')
        source = Path(__file__).read_bytes()
        result = sample(args.case, args.dll)
        if Path(__file__).read_bytes() != source:
            raise ValueError('Save-role reader source changed during observation.')
        checkpoint = output.with_suffix('.reader.py')
        with checkpoint.open('xb') as stream:
            stream.write(source)
        result['reader_source'] = {'path': str(checkpoint), 'sha256': hashlib.sha256(source).hexdigest()}
        with output.open('x', encoding='utf-8') as stream:
            json.dump(result, stream, indent=2)
        print(json.dumps({'read_only': True, 'edition': result['edition'], 'automatic_classification_accepted': False}))
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(2, f'Original Steam save-role observation refused: {error}\n')


if __name__ == '__main__':
    main()
