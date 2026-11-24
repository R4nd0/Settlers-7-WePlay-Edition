"""Read the original GUI camera getters' demonstrated fields without calling Lua.

Uses an already-admitted unfinished lab case, query/read rights only, and exact
registration/code guards. External samples are NOT synchronized to a GPU frame.
"""
from __future__ import annotations

import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import math
from pathlib import Path
import struct
import time

from . import reconfigure as scope

PROFILES = {
    '4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f': {
        'edition': 'ubisoft-history', 'image_size': 0x10e7000, 'root_rva': 0xfa51e8,
        'methods': (
            {'name': 'GetCamTranslation', 'name_rva': 0xd508f8, 'slot_rva': 0xf8c2bc,
             'callback_rva': 0x45e610, 'field': 0x28,
             'guards': ((0x45e610, 108, 0xce89322e23903efd, (7,)),)},
            {'name': 'GetCamLookAt', 'name_rva': 0xd5092c, 'slot_rva': 0xf8c30c,
             'callback_rva': 0x45e890, 'field': 0x34,
             'guards': ((0x45e890, 108, 0xae3dd7ab743366c7, (7,)),)},
        ),
    },
    '76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab': {
        'edition': 'gold', 'image_size': 0xdc4000, 'root_rva': 0xcf83fc,
        'methods': (
            {'name': 'GetCamTranslation', 'name_rva': 0xb37cd0, 'slot_rva': 0xca0558,
             'callback_rva': 0x1e5ff0, 'field': 0x28,
             'guards': ((0x1e5ff0, 15, 0x53f0fe25fa3603c9, ()), (0x1e6000, 85, 0x10526aed83d8aaf3, (7,)))},
            {'name': 'GetCamLookAt', 'name_rva': 0xb37d24, 'slot_rva': 0xca05a8,
             'callback_rva': 0x1e6240, 'field': 0x34,
             'guards': ((0x1e6240, 15, 0x53f0fe25fa3603c9, ()), (0x1e6250, 85, 0x20c6297a55f54bd9, (7,)))},
        ),
    },
    '8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410': {
        'edition': 'steam-history', 'image_size': 0x1106000,
        # The static file's text is packed. Discover initialized registration
        # slots only in this exact .data span, then validate the complete known
        # getter instruction template before deriving any data address.
        'data_rva': 0xefa000, 'data_bytes': 0xb0f48,
        'methods': (
            {'name': 'GetCamTranslation', 'name_rva': 0xd4ee50, 'field': 0x28,
             'template_fnv': 0x87d79c21a075a94d},
            {'name': 'GetCamLookAt', 'name_rva': 0xd4ee8c, 'field': 0x34,
             'template_fnv': 0xc0cbe3624fe156b9},
        ),
    },
}


def fnv(data):
    value = 0xcbf29ce484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001b3) & 0xffffffffffffffff
    return value


def normalized_code(raw, base, relocations):
    data = bytearray(raw)
    for offset in relocations:
        if offset < 0 or offset + 4 > len(data):
            raise ValueError('Invalid camera code guard relocation.')
        word = struct.unpack_from('<I', data, offset)[0]
        struct.pack_into('<I', data, offset, (word - base + 0x400000) & 0xffffffff)
    return bytes(data)


def _pointer(value):
    if type(value) is not int or not 0x10000 <= value <= 0xffff0000 or value % 4:
        raise ValueError('Original camera pointer unavailable or invalid.')
    return value


def _word(read, address):
    return struct.unpack('<I', read(address, 4))[0]


def _steam_method(read, base, profile, method):
    if profile['data_bytes'] > 1024 * 1024:
        raise ValueError('Steam registration scan exceeds its fixed span.')
    needle = struct.pack('<I', base + method['name_rva'])
    candidates = []
    for offset in range(0, profile['data_bytes'], 65536):
        size = min(65536, profile['data_bytes'] - offset)
        data = read(base + profile['data_rva'] + offset, size)
        cursor = 0
        while True:
            index = data.find(needle, cursor)
            if index < 0:
                break
            cursor = index + 1
            slot = base + profile['data_rva'] + offset + index
            if slot % 4:
                continue
            callback = _word(read, slot + 4)
            if not base + 0x1000 <= callback <= base + 0xc42ef0 - 108:
                continue
            code = bytearray(read(callback, 108))
            root = struct.unpack_from('<I', code, 7)[0]
            calls = []
            for operand in (46, 68, 90):
                calls.append(callback + operand + 4 + struct.unpack_from('<i', code, operand)[0])
            if len(set(calls)) != 1 or not base + 0x1000 <= calls[0] < base + 0xc42ef0:
                continue
            for operand in (7, 46, 68, 90):
                code[operand:operand + 4] = bytes(4)
            if fnv(code) != method['template_fnv'] or not base <= root <= base + profile['image_size'] - 4:
                continue
            actual = dict(method, slot_rva=slot - base, callback_rva=callback - base,
                          root_rva=root - base, guards=((callback - base, 108,
                          fnv(normalized_code(read(callback, 108), base, (7,))), (7,)),))
            candidates.append(actual)
    if len(candidates) != 1:
        raise ValueError('Steam getter registration/template is unavailable or ambiguous.')
    return candidates[0]


def read_pose(read, base, profile):
    """Pure bounded reader: caller supplies RPM, no code is ever executed."""
    if type(base) is not int or not 0x10000 <= base <= 0xffffffff - profile['image_size']:
        raise ValueError('Invalid original image base.')
    methods = []
    hashes, registrations = [], []
    for specification in profile['methods']:
        method = specification if 'callback_rva' in specification else _steam_method(read, base, profile, specification)
        name = method['name'].encode('ascii') + b'\0'
        if read(base + method['name_rva'], len(name)) != name:
            raise ValueError('Original GUI camera name changed.')
        if (_word(read, base + method['slot_rva']) != base + method['name_rva']
                or _word(read, base + method['slot_rva'] + 4) != base + method['callback_rva']):
            raise ValueError('Original GUI camera registration changed.')
        registrations.append({'name': method['name'], 'name_slot_rva': method['slot_rva'],
            'callback_rva': method['callback_rva'], 'normalized_fnv64': f'{fnv(struct.pack("<2I", 0x400000 + method["name_rva"], 0x400000 + method["callback_rva"])):016x}'})
        for rva, size, expected, relocations in method['guards']:
            code = read(base + rva, size)
            actual = fnv(normalized_code(code, base, relocations))
            if actual != expected:
                raise ValueError('Original GUI camera callback/instruction guard changed.')
            hashes.append({'rva': rva, 'bytes': size, 'normalized_fnv64': f'{actual:016x}'})
        methods.append(method)
    roots = {method.get('root_rva', profile.get('root_rva')) for method in methods}
    if len(roots) != 1 or None in roots:
        raise ValueError('The two original camera getters disagree on their singleton.')
    if [method['field'] for method in methods] != [0x28, 0x34]:
        raise ValueError('Unsupported original camera component layout.')
    root_slot = base + roots.pop()
    def chain():
        first = _pointer(_word(read, root_slot))
        second = _pointer(_word(read, first + 8))
        camera = _pointer(_word(read, second + 0xd8))
        return first, second, camera
    original = chain()
    camera = original[2]
    raw = read(camera + 0x28, 24)
    if chain() != original or read(camera + 0x28, 24) != raw:
        raise ValueError('Camera changed during the external sample; retry a parked view.')
    values = struct.unpack('<6f', raw)
    if any(not math.isfinite(value) or abs(value) > 1e9 for value in values):
        raise ValueError('Original GUI camera coordinates are unavailable.')
    if sum((values[i] - values[i + 3]) ** 2 for i in range(3)) <= 1e-8:
        raise ValueError('Original GUI camera has no valid look direction.')
    return {'translation': list(values[:3]), 'look_at': list(values[3:]),
            'fov_degrees': None, 'source': 'original GUI.GetCamTranslation/GetCamLookAt registered bindings',
            'code_guards': hashes, 'registration_guards': registrations,
            'steam_mapping': 'runtime-template-derived' if 'data_rva' in profile else None,
            'same_frame_gpu_proof': False, 'visible_shader_correlation_validated': False}


def sample(case, dll):
    """Open only an already-admitted live lab child with query/read rights."""
    files = scope._case_files(case, dll)
    expected = files['owned']['sha256']
    if expected not in PROFILES:
        raise ValueError('Original GUI camera map unavailable for this executable.')
    api = scope.isolated_process.Windows()
    scope._bind(api)
    handle = api.check(api.k.OpenProcess(0x410, False, files['owned']['pid']))
    def read(address, size):
        if not 0x10000 <= address <= 0xffffffff - size or not 1 <= size <= 65536:
            raise ValueError('Camera RPM request exceeds its bounds.')
        data, done = C.create_string_buffer(size), C.c_size_t()
        api.check(api.k.ReadProcessMemory(handle, address, data, size, C.byref(done)))
        if done.value != size:
            raise RuntimeError('Incomplete original camera read.')
        return data.raw
    try:
        state = scope._live_state(api, handle, files['owned']['pid'], files['owned']['parent'])
        creation = scope.validate_live_state(state, files)
        peb, count = C.c_size_t(), W.ULONG()
        if api.nt.NtQueryInformationProcess(handle, 26, C.byref(peb), C.sizeof(peb), C.byref(count)) or not peb.value:
            raise RuntimeError('The original target must be x86.')
        base = _word(read, peb.value + 8)
        header = read(base, 4096)
        pe = struct.unpack_from('<I', header, 0x3c)[0]
        native = scope.isolated_process.PROFILES[expected]
        if (header[:2] != b'MZ' or not 64 <= pe < 3000 or header[pe:pe+6] != b'PE\0\0\x4c\x01'
                or struct.unpack_from('<I', header, pe + 8)[0] != native[1]
                or struct.unpack_from('<I', header, pe + 0x50)[0] != native[2]):
            raise ValueError('The loaded original camera image header changed.')
        started = time.monotonic_ns()
        result = read_pose(read, base, PROFILES[expected])
        if scope.validate_live_state(scope._live_state(api, handle, files['owned']['pid'], files['owned']['parent']), files) != creation:
            raise RuntimeError('Original process identity changed during the camera read.')
        return {'format': 's7-original-gui-camera-read-v1', 'read_only': True,
                'identity': {key: files['owned'][key] for key in ('pid', 'parent', 'sha256')},
                'edition': PROFILES[expected]['edition'], 'observer_started_monotonic_ns': started,
                'observer_finished_monotonic_ns': time.monotonic_ns(), **result}
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
        if output.exists():
            raise ValueError('Retain older camera observations and use a fresh output.')
        result = sample(args.case, args.dll)
        with output.open('x', encoding='utf-8') as stream:
            json.dump(result, stream, indent=2)
        print(json.dumps({'read_only': True, 'edition': result['edition'], 'same_frame_gpu_proof': False}))
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(2, f'Original camera observation refused: {error}\n')


if __name__ == '__main__':
    main()
