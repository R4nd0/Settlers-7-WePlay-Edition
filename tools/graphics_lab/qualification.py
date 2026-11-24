"""Aggregate private native graphics evidence; never certify visuals or releases.

Hashes establish file consistency, not independent runtime attestation. Operator
notes and synthetic runs cannot satisfy native-game measurement requirements.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import zlib

ROOT = Path(__file__).resolve().parents[2]
FORMAT = 's7-graphics-qualification-v1'
DXVK = '265888c31ca78dffa290c39cb7e50bfb02762590e41927906e46fb32f01497fa'
EDITIONS = {
    'ubisoft-history': '4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f',
    'steam-history': '8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410',
    'gold': '76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab',
}
ARTIFACTS = {
    'result': 'result.private.json', 'checkpoint': 'checkpoint.private.json',
    'owned': 'owned.private.json', 'memory': 'memory.private.json',
    'hook': 'hook-state.private.json', 'hook_init': 'hook-initialized.private.json',
    'recovery': 'recovery-journal.private.json', 'profile_before': 'portable-before/snapshot.private.json',
    'hook_build': 'supersampling-hook-build.json',
}
MAX_JSON = 1024 * 1024
MAX_IMAGE = 128 * 1024 * 1024
HOOK_SOURCES = {
    'supersampling_hook.cpp', 'supersampling_hook.h', 'supersampling_hook_config.h',
    'supersampling_hook_capture.h', 'supersampling_hook_capture.cpp', 'supersampling_hook.def',
    'supersampling_hook_fixture.cpp', 'supersampling_probe.cpp', 'material_shader_observer.h',
    'material_shader_observer.cpp', 'graphics_observation.h', 'graphics_observation.cpp',
    'atmosphere_clock.h', 'atmosphere_fp.h',
}


def _plain(path):
    path = Path(path).absolute()
    for part in (path, *path.parents):
        if part.is_symlink() or (hasattr(part, 'is_junction') and part.is_junction()):
            raise ValueError('Qualification does not follow links or junctions.')
    path = path.resolve()
    work = (ROOT / 'work').resolve()
    if path == work or not path.is_relative_to(work):
        raise ValueError('Private evidence must stay below repository work/.')
    return path


def _read(path, limit):
    path = _plain(path)
    with path.open('rb') as stream:
        raw = stream.read(limit + 1)
    if len(raw) > limit:
        raise ValueError('Evidence file exceeds its read bound.')
    return raw


def _ref(path):
    path = _plain(path)
    return {'path': str(path), 'sha256': hashlib.sha256(_read(path, MAX_IMAGE)).hexdigest()}


def _bound(reference, base, limit):
    if (not isinstance(reference, dict) or set(reference) != {'path', 'sha256'}
            or not isinstance(reference['path'], str)
            or not isinstance(reference['sha256'], str)
            or not re.fullmatch('[0-9a-f]{64}', reference['sha256'])):
        raise ValueError('Evidence references require an exact path and SHA256.')
    path = _plain(base / reference['path'])
    raw = _read(path, limit)
    if hashlib.sha256(raw).hexdigest() != reference['sha256']:
        raise ValueError('Evidence SHA256 changed.')
    return path, raw


def _json(reference, base):
    return json.loads(_bound(reference, base, MAX_JSON)[1])


def _number(value):
    try:
        return type(value) in (int, float) and math.isfinite(value)
    except OverflowError:
        return False


def _size(value):
    return (isinstance(value, list) and len(value) == 2
            and all(type(v) is int and 1 <= v <= 16384 for v in value))


def _positive(value):
    return type(value) is int and value > 0


def _camera(value):
    if isinstance(value, dict) and set(value) == {'world_matrix', 'fov_native', 'clip_native'}:
        matrix, fov, clip = (value[k] for k in ('world_matrix', 'fov_native', 'clip_native'))
        return (isinstance(matrix, list) and len(matrix) == 16 and all(_number(v) for v in matrix)
                and isinstance(fov, list) and len(fov) == 2 and all(_number(v) and 0 < v < 180 for v in fov)
                and isinstance(clip, list) and len(clip) == 2 and all(_number(v) for v in clip)
                and 0 < clip[0] < clip[1])
    return (isinstance(value, dict) and set(value) == {'translation', 'look_at', 'fov_degrees'}
            and all(isinstance(value[k], list) and len(value[k]) == 3
                    and all(_number(v) for v in value[k]) for k in ('translation', 'look_at'))
            and _number(value['fov_degrees']) and 0 < value['fov_degrees'] < 180)


def _image_size(raw):
    if raw.startswith(b'\x89PNG\r\n\x1a\n'):
        if len(raw) < 33 or raw[8:16] != b'\0\0\0\rIHDR':
            raise ValueError('Invalid PNG header.')
        if zlib.crc32(raw[12:29]) & 0xffffffff != struct.unpack('>I', raw[29:33])[0]:
            raise ValueError('PNG header CRC mismatch.')
        size = list(struct.unpack('>2I', raw[16:24]))
        at, chunks, seen_data, ended = 8, 0, False, False
        while at + 12 <= len(raw):
            length = struct.unpack_from('>I', raw, at)[0]
            end = at + 12 + length
            chunks += 1
            if end > len(raw) or chunks > 10000:
                raise ValueError('Truncated or excessive PNG chunks.')
            kind = raw[at+4:at+8]
            if zlib.crc32(raw[at+4:end-4]) & 0xffffffff != struct.unpack('>I', raw[end-4:end])[0]:
                raise ValueError('PNG chunk CRC mismatch.')
            seen_data |= kind == b'IDAT' and length > 0
            at = end
            if kind == b'IEND':
                ended = length == 0 and at == len(raw)
                break
        if not seen_data or not ended:
            raise ValueError('PNG capture has no complete image data.')
    elif raw.startswith(b'BM') and len(raw) >= 54:
        width, height = struct.unpack_from('<ii', raw, 18)
        size = [width, abs(height)]
        offset = struct.unpack_from('<I', raw, 10)[0]
        planes, bits, compression = struct.unpack_from('<HHI', raw, 26)
        stride = ((width * bits + 31) // 32) * 4
        if planes != 1 or bits not in (24, 32) or compression or offset < 54 or offset + stride * abs(height) > len(raw):
            raise ValueError('Unsupported or truncated BMP capture.')
    else:
        raise ValueError('Captures must be PNG or Windows BMP files.')
    if not _size(size):
        raise ValueError('Capture dimensions exceed the analysis bound.')
    return size


def record_case(directory, *, case_id, edition, role='candidate', kind='actual-game',
                captures=(), observation=None, hook_build=None, native_captures=()):
    """Bind existing files, without inventing measurement or visual assertions.

    captures is an iterable of (stage, image_path), stage source/output.
    observation is an optional private native-reader JSON path.
    """
    directory = _plain(directory)
    value = {'id': case_id, 'edition': edition, 'role': role, 'kind': kind,
             'artifacts': {name: _ref(directory / relative) for name, relative in ARTIFACTS.items()
                           if (directory / relative).is_file()},
             'captures': [{'stage': stage, 'file': _ref(path)} for stage, path in captures]}
    if observation is not None:
        value['observation'] = _ref(observation)
    if hook_build is not None:
        value['artifacts']['hook_build'] = _ref(hook_build)
    if native_captures:
        value['native_captures'] = []
        for path in native_captures:
            path = _plain(path)
            value['native_captures'].append(_ref(path))
            metadata = json.loads(_read(path, MAX_JSON))
            sequence = metadata.get('sequence') if isinstance(metadata, dict) else None
            if _positive(sequence) and sequence <= 1000000:
                for stage in ('source', 'output'):
                    image = path.parent / f'capture-{sequence}-{stage}.bmp'
                    if image.is_file():
                        value['captures'].append({'stage': stage, 'file': _ref(image)})
    return value


def _case(value, base):
    if (not isinstance(value, dict) or not isinstance(value.get('id'), str)
            or not re.fullmatch('[a-zA-Z0-9_-]{1,64}', value['id'])
            or value.get('edition') not in EDITIONS or value.get('role') not in ('control', 'candidate')
            or value.get('kind') not in ('actual-game', 'synthetic')
            or not isinstance(value.get('artifacts'), dict) or not isinstance(value.get('captures'), list)
            or len(value['captures']) > 32 or not isinstance(value.get('native_captures', []), list)
            or len(value.get('native_captures', [])) > 16):
        raise ValueError('Invalid graphics case record.')
    missing, rejected, data = [], [], {}
    for name in ('result', 'checkpoint', 'owned', 'memory', 'recovery', 'profile_before'):
        if name not in value['artifacts']:
            missing.append(name)
    for name, reference in value['artifacts'].items():
        if name not in ARTIFACTS:
            raise ValueError('Unknown graphics artifact role.')
        try:
            item = _json(reference, base)
            if not isinstance(item, dict):
                raise ValueError('Artifact must be a JSON object.')
            data[name] = item
        except (OSError, ValueError) as error:
            rejected.append(f'{name}: {type(error).__name__}')
    def check(condition, label):
        if not condition:
            rejected.append(label)
    result, owned, checkpoint = (data.get(k, {}) for k in ('result', 'owned', 'checkpoint'))
    memory = data.get('memory', {})
    if result:
        for key in ('game_started', 'shared_profiles_unchanged', 'game_binaries_unchanged', 'private_options_restored'):
            check(result.get(key) is True, key)
        check(type(result.get('exit_code')) is int and result['exit_code'] == 0, 'not a graceful zero exit')
        check(result.get('owned_timeout_or_stop') is False, 'watchdog/forced stop')
        check(result.get('error_type') is None and result.get('cleanup_errors') == [], 'run or cleanup failure')
        check(result.get('memory') == memory, 'memory file/result mismatch')
    if memory:
        check(memory.get('monitor_failed') is False and memory.get('recovery_required') is False
              and type(memory.get('max_pressure')) is int and memory['max_pressure'] == 0
              and _positive(memory.get('samples')), 'unhealthy memory monitoring')
    exe = EDITIONS[value['edition']]
    if owned:
        check(owned.get('sha256') == exe and owned.get('native_initialized') is True
              and _positive(owned.get('pid')) and _positive(owned.get('parent'))
              and owned.get('kernel_isolation') == 7, 'original edition/owned startup identity')
    binaries = checkpoint.get('files', {})
    if checkpoint:
        check(checkpoint.get('reshade_requested') is False, 'ReShade requested')
        check(isinstance(binaries, dict) and len(binaries) == 5, 'incomplete binary checkpoint')
        if not isinstance(binaries, dict):
            binaries = {}
        check(any(name.endswith('/Settlers7R.exe') and pin == exe for name, pin in binaries.items()), 'checkpoint edition identity')
        check(any(name.endswith('/d3d9.dll') and pin == DXVK for name, pin in binaries.items()), 'checkpoint DXVK identity')
        sources = checkpoint.get('sources', {})
        if not isinstance(sources, dict) or not sources:
            missing.append('frozen controller source checkpoint')
        else:
            path, _ = _bound(value['artifacts']['checkpoint'], base, MAX_JSON)
            for relative, pin in sources.items():
                try:
                    if not isinstance(relative, str) or Path(relative).is_absolute() or '..' in Path(relative).parts:
                        raise ValueError('Unsafe frozen source path.')
                    frozen = _read(path.parent / 'source' / relative, MAX_JSON)
                    check(hashlib.sha256(frozen).hexdigest() == pin, 'frozen source changed')
                except (OSError, ValueError):
                    rejected.append('missing/unsafe frozen source')
    journal = data.get('recovery', {})
    if journal:
        check(journal.get('format') == 's7-graphics-lab-recovery-v1' and journal.get('state') == 'restored', 'recovery journal incomplete')
        check(journal.get('critical') == binaries, 'recovery/binary checkpoint mismatch')
        try:
            root = _plain(journal['root'])
            for relative, pin in binaries.items():
                if not isinstance(relative, str) or Path(relative).is_absolute() or '..' in Path(relative).parts:
                    raise ValueError('Unsafe binary path.')
                check(hashlib.sha256(_read(root / relative, 64 * 1024 * 1024)).hexdigest() == pin,
                      'current guarded binary changed')
            check(str(_plain(owned['path'])) == str(root / next(name for name in binaries if name.endswith('/Settlers7R.exe'))),
                  'owned executable path differs from recovery root')
        except (KeyError, StopIteration, OSError, ValueError):
            rejected.append('guarded binary files unavailable/unsafe')
        try:
            before = data.get('profile_before', {}).get('portable', {}).get('files', {})
        except AttributeError:
            before = None
        if not isinstance(before, dict):
            rejected.append('invalid original profile snapshot')
        else:
            saves = {name: item for name, item in before.items() if Path(name).suffix.casefold() in ('.s7s', '.s7local')}
            if not saves:
                missing.append('original private save checkpoint')
            for relative, item in saves.items():
                try:
                    path = _plain(Path(journal['root']) / 'LocalData/ProfileRoot' / relative)
                    raw = _read(path, MAX_IMAGE)
                    check(hashlib.sha256(raw).hexdigest() == item['sha256'], 'original private save changed')
                except (KeyError, OSError, ValueError):
                    rejected.append('original private save unavailable')
    requested = checkpoint.get('requested_render_size')
    hook = data.get('hook', {})
    if value['role'] == 'candidate':
        for name in ('hook', 'hook_init', 'hook_build'):
            if name not in value['artifacts']:
                missing.append(name)
        if hook:
            check(hook.get('format') == 's7-supersampling-hook-v1' and hook.get('lab_only') is True, 'hook format')
            check(_size(requested), 'invalid requested render dimensions')
            for key in ('source', 'selected_target', 'viewport'):
                check(hook.get(key) == requested, f'{key} was not the requested high-resolution scene')
            output = hook.get('output')
            check(_size(output) and output == hook.get('client'), 'output/client mismatch')
            if _size(output) and _size(requested):
                check(output[0] < requested[0] and output[1] < requested[1]
                      and output[0] * requested[1] == output[1] * requested[0], 'not same-aspect native downsampling')
            for key in ('attached', 'present_calls', 'output_present_calls', 'high_resolution_draws', 'indexed_draws'):
                check(_positive(hook.get(key)), f'no native {key}')
            if type(hook.get('output_present_calls')) is int and type(hook.get('present_calls')) is int:
                check(hook['output_present_calls'] <= hook['present_calls'], 'invalid presentation counters')
                check(type(hook.get('fallback_calls')) is int and 0 <= hook['fallback_calls'] <= hook['present_calls'],
                      'invalid fallback counter')
            check(hook.get('last_failure') == 0 and hook.get('resize_failure') == 0, 'final native hook failure')
        initialized = data.get('hook_init', {})
        if initialized:
            check(initialized.get('initialized') is True and initialized.get('original_entry_guarded') is True
                  and initialized.get('worker_code_rx') is True
                  and bool(re.fullmatch('[0-9a-f]{64}', str(initialized.get('dll_sha256')))), 'hook initialization/source binding missing')
        build = data.get('hook_build', {})
        if build:
            build_files = build.get('files', {})
            check(build.get('format') == 's7-supersampling-hook-build-v1' and isinstance(build_files, dict)
                  and build_files.get('WePlaySupersamplingLab.dll') == initialized.get('dll_sha256')
                  and bool(re.fullmatch('[0-9a-f]{64}', str(build.get('build_sha256')))),
                  'hook initialization/build manifest mismatch')
            check(isinstance(build_files, dict) and HOOK_SOURCES | {'supersampling_hook_fixture.exe', 'WePlaySupersamplingLab.dll'} <= build_files.keys(),
                'incomplete native hook build source identities')
            check(isinstance(build_files, dict) and all(isinstance(pin, str) and re.fullmatch('[0-9a-f]{64}', pin)
                  for pin in build_files.values()), 'invalid native hook build source identity')
            if isinstance(build_files, dict):
                build_path, _ = _bound(value['artifacts']['hook_build'], base, MAX_JSON)
                for name in HOOK_SOURCES | {'WePlaySupersamplingLab.dll', 'supersampling_hook_fixture.exe'}:
                    if name not in build_files:
                        continue
                    try:
                        raw = _read(build_path.parent / name, MAX_JSON if name in HOOK_SOURCES else MAX_IMAGE)
                        check(hashlib.sha256(raw).hexdigest() == build_files[name], 'compiled hook source/binary snapshot changed')
                    except (OSError, ValueError):
                        rejected.append('compiled hook source/binary snapshot unavailable')
    captures = {}
    capture_hashes = {'source': set(), 'output': set()}
    for capture in value['captures']:
        if not isinstance(capture, dict) or set(capture) != {'stage', 'file'} or capture['stage'] not in ('source', 'output'):
            raise ValueError('Captures require source/output stages and a bound image file.')
        try:
            _, raw = _bound(capture['file'], base, MAX_IMAGE)
            size = _image_size(raw)
            expected = (requested if capture['stage'] == 'source' or value['role'] == 'control'
                        else hook.get('output', requested))
            check(size == expected, 'capture dimensions do not match their native stage')
            captures[capture['stage']] = captures.get(capture['stage'], 0) + 1
            capture_hashes[capture['stage']].add(capture['file']['sha256'])
        except (OSError, ValueError):
            rejected.append('capture unavailable, changed or invalid')
    if not captures.get('output'):
        missing.append('native-sized output image')
    if value['role'] == 'candidate' and not captures.get('source'):
        missing.append('native high-resolution source image')
    native_frames = []
    for reference in value.get('native_captures', []):
        try:
            frame = _json(reference, base)
            if not isinstance(frame, dict) or frame.get('format') != 's7-native-frame-capture-v1':
                raise ValueError('Unexpected native capture format.')
            edition = {'ubisoft-history': 1, 'steam-history': 2, 'gold': 3}[value['edition']]
            sequence, number = frame.get('sequence'), frame.get('frame')
            buffers = (_positive(sequence) and _positive(number) and frame.get('edition') == edition
                and frame.get('complete') is True and type(frame.get('status')) is int and frame['status'] == 0
                and frame.get('desktop_capture') is False and frame.get('exclude_from_pacing') is True
                and frame.get('source') == requested
                and frame.get('output') == (requested if value['role'] == 'control' else hook.get('output'))
                and all(frame.get(stage + '_bmp_sha256') in capture_hashes[stage] for stage in ('source', 'output'))
                and _number(frame.get('capture_stall_ms')) and frame['capture_stall_ms'] >= 0)
            if not buffers:
                rejected.append('native same-frame GPU buffer pair is incomplete or mismatched')
            def succeeded(hr):
                return type(hr) is int and 0 <= hr <= 0xffffffff and not hr & 0x80000000
            final = frame.get('final_present_hresult', frame.get('present_hresult'))
            check(frame.get('present_hresult') == final, 'native final presentation fields disagree')
            scene = (frame.get('client') == frame.get('output') and frame.get('viewport') == requested
                and frame.get('selected_target') == requested and _positive(frame.get('high_resolution_draws'))
                and _positive(frame.get('indexed_draws')))
            if not scene:
                missing.append('per-frame physical client/scene/viewport observation')
            filtered = (buffers and scene and frame.get('presentation_path') == 'filtered_output'
                and frame.get('fallback_used') is False and frame.get('filtered_api_succeeded') is True
                and frame.get('attempted_path_hresult') == frame.get('filtered_present_hresult')
                and succeeded(frame.get('filtered_present_hresult')) and succeeded(final)
                and succeeded(frame.get('readback_hresult')))
            original = (buffers and scene and value['role'] == 'control' and frame.get('presentation_path') == 'original'
                and frame.get('fallback_used') is False and frame.get('source') == frame.get('output') and succeeded(final))
            if not (filtered or original):
                missing.append('per-frame successful filtered/original presentation without fallback')
            available = frame.get('observation_available')
            camera = None
            if type(available) is int and available & 3 == 3 and frame.get('observation_status') == 0:
                candidate_camera = {'world_matrix': frame.get('camera_world'), 'fov_native': frame.get('fov_native'),
                                    'clip_native': frame.get('clip_native')}
                if _camera(candidate_camera):
                    camera = candidate_camera
            native_frames.append({'sequence': sequence, 'frame': number, 'gpu_buffer_pair_bound': bool(buffers),
                'native_scene_dimensions_recorded': bool(scene),
                'filtered_present_recorded': bool(filtered), 'original_present_recorded': bool(original),
                'vision_main_context_camera': camera, 'visible_camera_validated': False,
                'camera_provenance': 'original Vision main-context exports; visible scene tracking unvalidated',
                'lod_scale': frame.get('lod_scale') if type(available) is int and available & 4 else None,
                'sun_azimuth': frame.get('sun_azimuth') if type(available) is int and available & 8 else None,
                'sun_elevation': frame.get('sun_elevation'), 'exposure': frame.get('exposure'),
                'simulation_turn': frame.get('simulation_turn'), 'capture_stall_ms': frame.get('capture_stall_ms'),
                'exclude_from_pacing': True})
        except (OSError, ValueError):
            rejected.append('native capture record unavailable, changed or invalid')
    observation = None
    if 'observation' in value:
        try:
            observation = _json(value['observation'], base)
            identity = observation.get('identity', {})
            check(identity == {key: owned.get(key) for key in ('pid', 'parent', 'sha256')}, 'observation/owned identity mismatch')
            if observation.get('format') != 's7-native-scene-observation-v1' or observation.get('origin') != 'guarded-runtime-read':
                missing.append('guarded native scene observations; notes are not measurements')
                observation = None
            else:
                _, source = _bound(observation['reader_source'], base, MAX_JSON)
                check(bool(source), 'empty frozen observation reader')
                check(observation.get('renderer_sha256') == DXVK, 'observation renderer identity mismatch')
                check(observation.get('render_size') == requested
                      and observation.get('output_size') == hook.get('output', requested), 'native observation dimensions mismatch')
                frames = observation.get('frames')
                if not isinstance(frames, list):
                    missing.append('bounded native scene frame series')
                    frames = []
                for frame in frames:
                    if (not isinstance(frame, dict) or any(frame.get(stage + '_image_sha256') not in capture_hashes[stage]
                            for stage in ('source', 'output'))):
                        missing.append('scene-frame hashes bound to source/output captures')
        except (KeyError, AttributeError, OSError, ValueError):
            rejected.append('invalid guarded observation/source binding')
            observation = None
    else:
        missing.append('guarded camera/save/sun/cadence/LOD observations')
    runtime_consistent = value['kind'] == 'actual-game' and not rejected and all(
        name in data for name in ('result', 'owned', 'checkpoint', 'memory', 'recovery', 'profile_before'))
    return {'id': value['id'], 'edition': value['edition'], 'role': value['role'], 'kind': value['kind'],
            'runtime_records_consistent': runtime_consistent,
            'native_ssaa_records_consistent': runtime_consistent and value['role'] == 'candidate'
                and all(name in data for name in ('hook', 'hook_init', 'hook_build')),
            'missing': sorted(set(missing)), 'rejected': sorted(set(rejected)), 'captures': captures,
            'native_counters': {key: hook.get(key) for key in ('present_calls', 'output_present_calls', 'fallback_calls',
                'high_resolution_draws', 'indexed_draws', 'swapchain_creations')},
            'memory_metrics': {key: memory.get(key) for key in ('samples', 'peak_private_mib', 'min_free_mib',
                'min_largest_mib', 'min_host_physical_free_mib', 'min_host_commit_free_mib', 'peak_scan_ms')},
            'render_size': requested, 'output_size': requested if value['role'] == 'control' else hook.get('output', requested),
            'native_frame_pairs': native_frames,
            '_observation': observation}


def _pair(a, b):
    failures = []
    if (a['edition'] != b['edition'] or a['role'] != 'control' or b['role'] != 'candidate'
            or not a['runtime_records_consistent'] or not b['native_ssaa_records_consistent']):
        failures.append('requires consistent original-game control/candidate of the same edition')
    left, right = a['_observation'], b['_observation']
    if not isinstance(left, dict) or not isinstance(right, dict):
        failures.append('guarded observations unavailable')
    else:
        if a['output_size'] != b['output_size']:
            failures.append('unmatched physical output size')
        for key in ('camera', 'save_sha256', 'native_lod_scale', 'simulation_pace_ms'):
            if left.get(key) is None or left.get(key) != right.get(key):
                failures.append(f'unmatched {key}')
        if not _camera(left.get('camera')) or not _camera(right.get('camera')):
            failures.append('camera was not a complete numeric native view')
        if not isinstance(left.get('save_sha256'), str) or not re.fullmatch('[0-9a-f]{64}', left['save_sha256']):
            failures.append('save identity was not measured')
        if left.get('simulation_pace_ms') != 100:
            failures.append('not original normal simulation pace')
        if not _number(left.get('native_lod_scale')) or left['native_lod_scale'] <= 0:
            failures.append('native LOD scale was not measured')
        frames_a, frames_b = left.get('frames'), right.get('frames')
        if not isinstance(frames_a, list) or not isinstance(frames_b, list) or not 3 <= len(frames_a) <= 300 or len(frames_a) != len(frames_b):
            failures.append('unpaired bounded capture cadence')
        else:
            previous = [-1.0, -1.0]
            previous_turn = -1
            for fa, fb in zip(frames_a, frames_b):
                if not isinstance(fa, dict) or not isinstance(fb, dict):
                    failures.append('invalid scene frame'); break
                if type(fa.get('turn')) is not int or fa.get('turn') != fb.get('turn') or fa['turn'] <= previous_turn:
                    failures.append('unmatched native turn')
                if type(fa.get('turn')) is int:
                    previous_turn = fa['turn']
                keys = ('elapsed_seconds', 'sun_azimuth_degrees', 'sun_elevation_degrees', 'exposure')
                if not all(_number(f.get(k)) for f in (fa, fb) for k in keys):
                    failures.append('nonfinite/unmeasured scene frame'); break
                if any(f['elapsed_seconds'] < 0 or not 0 <= f['sun_azimuth_degrees'] <= 360
                       or not -90 <= f['sun_elevation_degrees'] <= 90 or not 0 <= f['exposure'] <= 100 for f in (fa, fb)):
                    failures.append('scene frame outside native units/ranges')
                for i, f in enumerate((fa, fb)):
                    if f['elapsed_seconds'] <= previous[i]:
                        failures.append('non-increasing capture cadence')
                    previous[i] = f['elapsed_seconds']
                angle = abs(fa['sun_azimuth_degrees'] - fb['sun_azimuth_degrees']) % 360
                if (abs(fa['elapsed_seconds'] - fb['elapsed_seconds']) > .05 or min(angle, 360-angle) > .1
                        or abs(fa['sun_elevation_degrees'] - fb['sun_elevation_degrees']) > .1
                        or abs(fa['exposure'] - fb['exposure']) > .001):
                    failures.append('unmatched sun/exposure/cadence')
    if a['missing'] or b['missing']:
        failures.append('case capture/measurement evidence is incomplete')
    # Matching numeric context data does not establish which camera S7's custom
    # renderer uses. Bind and review an actual pan/zoom tracking trace first.
    metadata_comparable = not failures
    failures.append('visible scene camera tracking remains unvalidated')
    return {'control': a['id'], 'candidate': b['id'], 'metadata_fields_comparable': metadata_comparable,
            'measurements_comparable': False, 'visible_camera_validated': False,
            'missing_or_rejected': sorted(set(failures)), 'visual_qualified': False}


def aggregate(path):
    path = _plain(path)
    value = json.loads(_read(path, MAX_JSON))
    if (not isinstance(value, dict) or set(value) != {'format', 'cases', 'pairs'} or value['format'] != FORMAT
            or not isinstance(value['cases'], list) or not 1 <= len(value['cases']) <= 64
            or not isinstance(value['pairs'], list) or len(value['pairs']) > 64):
        raise ValueError('Invalid bounded qualification manifest.')
    cases = [_case(case, path.parent) for case in value['cases']]
    by_id = {case['id']: case for case in cases}
    if len(by_id) != len(cases):
        raise ValueError('Qualification case IDs must be unique.')
    pairs = []
    for item in value['pairs']:
        if not isinstance(item, dict) or set(item) != {'control', 'candidate'} or any(item[k] not in by_id for k in item):
            raise ValueError('Pair references must name known case IDs.')
        pairs.append(_pair(by_id[item['control']], by_id[item['candidate']]))
    coverage = {edition: {
        'consistent_native_runs': [c['id'] for c in cases if c['edition'] == edition and c['runtime_records_consistent']],
        'consistent_ssaa_runs': [c['id'] for c in cases if c['edition'] == edition and c['native_ssaa_records_consistent']],
        'same_frame_filtered_capture_runs': [c['id'] for c in cases if c['edition'] == edition
            and c['native_ssaa_records_consistent'] and any(f['filtered_present_recorded'] for f in c['native_frame_pairs'])],
        'comparable_capture_pairs': [p['candidate'] for p in pairs if p['measurements_comparable'] and by_id[p['candidate']]['edition'] == edition],
        'visual_and_input_review_required': True,
        'actual_lifecycle_and_fallback_review_required': True,
    } for edition in EDITIONS}
    for case in cases:
        case.pop('_observation')
    return {'format': FORMAT, 'release_qualified': False, 'visual_qualified': False,
            'scope': 'hash-bound private record consistency and native measurement comparability only',
            'limits': ['File hashes cannot independently attest that a reader executed or that an image depicts the claimed stage.',
                       'Operator notes and synthetic fixtures never establish original-game coverage.',
                       'Counters are draw/presentation calls, not GPU time, FPS or visual quality.',
                       'Material binding, native shadow comparison, launcher lifecycle, save/reload, cross-edition and visual/input review remain separate gates.'],
            'cases': cases, 'pairs': pairs, 'edition_coverage': coverage}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        rendered = json.dumps(aggregate(args.manifest), indent=2, allow_nan=False) + '\n'
        if args.output:
            with _plain(args.output).open('x', encoding='utf-8') as stream:
                stream.write(rendered)
        else:
            print(rendered, end='')
    except (OSError, ValueError) as error:
        parser.exit(2, f'Qualification aggregation refused: {error}\n')


if __name__ == '__main__':
    main()
