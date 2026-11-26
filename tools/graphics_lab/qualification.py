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
WATER_PROGRAM_SHA256 = '8a27791a9907f73d4f42c847bc74a0a25b57da48b81f8e9cb566eca59bfcf0d1'
WATER_RECIPE = 'river-depth-fresnel-v1-candidate'
WATER_BALANCED_PROGRAM_SHA256 = 'a8a591b346911622bfc9899fa28cbb022a42152df41ed822ef3cfff7c254e2f4'
WATER_BALANCED_RECIPE = 'river-depth-fresnel-v2-balanced'
WATER_SOURCES = {
    'Graphics/Shaders/Water_ATI_High.ShaderLib': 'f5c575f9015fefd2f33e4a7fdce00bcadc68b54590844b3c08d1c4e48df1efe2',
    'Graphics/Shaders/Water_ATIx_High.ShaderLib': '3a018f42a0de1a14338076beccedcb27395e345a02560610c1ed5fcf9ec6f0f0',
    'Graphics/Shaders/Water_NV_High.ShaderLib': '2173334cdbe9223fb412cf93e96b3178176039c11421b87dec8417da08e10788',
}
ARTIFACTS = {
    'result': 'result.private.json', 'checkpoint': 'checkpoint.private.json',
    'owned': 'owned.private.json', 'memory': 'memory.private.json',
    'hook': 'hook-state.private.json', 'hook_init': 'hook-initialized.private.json',
    'recovery': 'recovery-journal.private.json', 'profile_before': 'portable-before/snapshot.private.json',
    'hook_build': 'supersampling-hook-build.json',
    'save_role_proof': 'private-save-role-proof.private.json',
    'save_role_read': 'save-role-read.private.json',
    'comparison_configured': 'comparison-configured.private.json',
    'effects_configured': 'effects-configured.private.json',
    'effects_config': 'effects-config.bin',
    'water_config': 'water-config.bin',
    'water_configured': 'water-configured.private.json',
    'water_stage': 'water-stage.private.json',
    'water_program': f'LocalData/MaterialShaders/{WATER_RECIPE}/{WATER_PROGRAM_SHA256}.ps.bin',
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
EFFECTS_HOOK_SOURCES = HOOK_SOURCES | {
    'supersampling_hook_effects_contract.h', 'supersampling_hook_effects_draw.h',
    'supersampling_hook_effects_gpu.cpp', 'material_shader_effects.h', 'material_shader_effects.cpp',
}
COMPARISON_HOOK_SOURCES = EFFECTS_HOOK_SOURCES | {
    'supersampling_hook_comparison_draw.h', 'material_comparison.h', 'material_comparison.cpp',
}
MODERN_HOOK_SOURCES = COMPARISON_HOOK_SOURCES | {
    'native_effects_wire.h', 'material_shader_config_v2.h', 'material_shader_effects_contract_v2.h',
}
WATER_HOOK_SOURCES = MODERN_HOOK_SOURCES | {'water_shader_effects.h', 'water_shader_effects.cpp', 'native_material_draw.h'}
TERRAIN_MACRO_HOOK_SOURCES = WATER_HOOK_SOURCES | {'terrain_macro_guard.h'}
SHADOW_HOOK_SOURCES = TERRAIN_MACRO_HOOK_SOURCES | {'shadow2048_filter_policy.h', 'shadow_filter_effects.h', 'shadow_filter_effects.cpp'}
AMBIENT_HOOK_SOURCES = SHADOW_HOOK_SOURCES | {
    'up_draw_observer_protocol.h', 'ambient_occlusion_policy.h',
    'ambient_occlusion_effects.h', 'ambient_occlusion_effects.cpp',
}
HOOK_BINARIES = {'supersampling_hook_fixture.exe', 'WePlaySupersamplingLab.dll'}
EFFECTS_HOOK_BINARIES = HOOK_BINARIES | {'supersampling_effects_gpu.exe'}
EFFECTS_BUILD_ABI = {'initialize_v1': 1048, 'initialize_v2': 3132,
                     'observation': 952, 'effects': 4132, 'capture': 12}
MODERN_BUILD_ABI = {**EFFECTS_BUILD_ABI, 'effects_v2': 6180}
WATER_BUILD_ABI = {**MODERN_BUILD_ABI, 'water': 1076}


def _build_contract(build):
    """Select a frozen source schema without comparing historical files to HEAD."""
    if build.get('format') == 's7-supersampling-hook-build-v1':
        return HOOK_SOURCES, HOOK_BINARIES, set(build) == {'format', 'build_sha256', 'files'}
    if build.get('format') == 's7-native-graphics-build-v2':
        ambient_v2 = build.get('source_set') == 'effects-v8'
        ambient = build.get('source_set') in ('effects-v7', 'effects-v8')
        shadow = build.get('source_set') in ('effects-v6', 'effects-v7', 'effects-v8')
        terrain_macro = build.get('source_set') in ('effects-v5', 'effects-v6', 'effects-v7', 'effects-v8')
        water = build.get('source_set') in ('effects-v4', 'effects-v5', 'effects-v6', 'effects-v7', 'effects-v8')
        modern = build.get('source_set') in ('effects-v3', 'effects-v4', 'effects-v5', 'effects-v6', 'effects-v7', 'effects-v8')
        comparison = build.get('source_set') in ('effects-v2', 'effects-v3', 'effects-v4', 'effects-v5', 'effects-v6', 'effects-v7', 'effects-v8')
        keys = {'format', 'source_set', 'compiled_capabilities', 'abi', 'build_sha256', 'files'}
        if comparison:
            keys.add('diagnostic_abi')
        diagnostic_abi = ({'comparison': 12, 'shadow': 12, 'ambient': 12, 'ambient_v2': 12} if ambient_v2
                          else {'comparison': 12, 'shadow': 12, 'ambient': 12} if ambient
                          else {'comparison': 12, 'shadow': 12} if shadow else {'comparison': 12})
        valid = (build.get('source_set') in ('effects-v1', 'effects-v2', 'effects-v3', 'effects-v4', 'effects-v5', 'effects-v6', 'effects-v7', 'effects-v8') and set(build) == keys
                 and type(build.get('compiled_capabilities')) is int
                 and build['compiled_capabilities'] == 15
                 and build.get('abi') == (WATER_BUILD_ABI if water else MODERN_BUILD_ABI if modern else EFFECTS_BUILD_ABI)
                 and (not comparison or build.get('diagnostic_abi') == diagnostic_abi))
        sources = AMBIENT_HOOK_SOURCES if ambient else SHADOW_HOOK_SOURCES if shadow else TERRAIN_MACRO_HOOK_SOURCES if terrain_macro else WATER_HOOK_SOURCES if water else MODERN_HOOK_SOURCES if modern else COMPARISON_HOOK_SOURCES if comparison else EFFECTS_HOOK_SOURCES
        return sources, EFFECTS_HOOK_BINARIES, valid
    return set(), set(), False


GOLD_SAVE_ROLE_PROOF = {
    'format': 's7-gold-private-save-role-proof-v1', 'edition': 'gold',
    'executable_sha256': '76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab',
    'get_slot_range': {'callback_rva': 0x1a9ff0, 'callback_bytes': 62,
        'callback_sha256': 'cc0c66f1d4439c92b91f1d67fa19147cfe3dc130fc98413ae777d33a071eddd3',
        'first': 30, 'count': 50},
    'source_member': {'name': 'Script/local/file/saveGames.lua',
        'sha256': 'b24466aaa705baabacfbc8f8d57b007809ab8542e365238ea64ac96dd90a35b3'},
    'quick_slot_index': 1, 'auto_slot_index': 2,
    'rotating_autosave_relative': 'Profiles/Savegames/s31.s7s',
    'restoration_performed': False,
}
UBI_SAVE_ROLE_PROOF = {
    'format': 's7-history-private-save-role-proof-v1', 'edition': 'ubisoft-history',
    'executable_sha256': EDITIONS['ubisoft-history'],
    'registration': {'name': 'GetSaveGameSlotRangeSP', 'name_rva': 0xd344d0,
        'initializer_rva': 0x8bb0f, 'initializer_bytes': 20,
        'initializer_sha256': 'c1e65bdd8c3a9c28e0255cc9dd020c6d25fb910ec6bd62c20a1890453ecf3b36',
        'name_slot_rva': 0xf7ca2c, 'callback_rva': 0x13df90},
    'get_slot_range': {'callback_rva': 0x13df90, 'callback_bytes': 66,
        'callback_sha256': 'f202ca35c9cc6b96fd53127f9767bedeeaa1acf6aa583381dbed3c4a3638b7f7',
        'first_rva': 0xd5e538, 'count_rva': 0xd5e5e0, 'first': 30, 'count': 50},
    'source_member': GOLD_SAVE_ROLE_PROOF['source_member'].copy(),
    'quick_slot_index': 1, 'auto_slot_index': 2,
    'rotating_autosave_relative': 'THE SETTLERS 7 - History Edition/Savegames/s31.s7s',
    'restoration_performed': False,
}
STEAM_SLOT_OBSERVATION = {
    'callback_rva': 0x13d580, 'callback_bytes': 66,
    'normalized_callback_sha256': '099f02b2879eca2f806b463ea39c218060df0ad2a37c02a4eea8c41484aa366e',
    'template_fnv64': 'aeca9f257e29c819',
    'first_rva': 0xd5ca48, 'count_rva': 0xd5caf0, 'first': 30, 'count': 50,
    'lua_number_sink_rva': 0x4c670, 'lua_number_sink_bytes': 56,
    'lua_number_sink_sha256': 'd2d2459bbf1c58bad7ea5b1aca804ab3a8f90f4e48f0a5c0bbea03e21c2dfb33',
    'registration': {'name': 'GetSaveGameSlotRangeSP', 'name_rva': 0xd32a3c,
        'name_slot_rva': 0xf79b1c, 'callback_rva': 0x13d580,
        'normalized_sha256': '3c040c7c115b6a680c0a96e08c20cf817ce4fc09f694fc45e3a80dc334bf0413'},
}
STEAM_SLOT_READER_SHA256 = '376e871c1063c8991399082333a250e722593c7fe2f70357e9511ed5c28795e2'


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


RIVER_VERTEX_SHA256 = 'f190d66ee1ae43637341a76e5b644cc63bf3c2a5b5c94ea9286764e13f1c16c0'
RIVER_PIXEL_SHA256 = 'dd65ba0b9d79c7c4d9ad9904404388230556b194eedb8cbdfbd4fa99f44eee5b'


def _scene_uniforms(frame, requested, expected_catalog):
    """Validate raw draw/capture correlation, without inferring camera or exposure."""
    value = frame.get('scene_uniforms')
    if not isinstance(value, dict):
        return None
    if (value.get('source') != 'original_river_shader_uniforms'
            or type(value.get('available')) is not int or value['available'] != 1
            or type(value.get('context_valid')) is not int or value['context_valid'] != 7
            or not _positive(frame.get('frame')) or value.get('frame') != frame['frame']
            or not _positive(value.get('successful_draw'))
            or any(not _positive(value.get(key)) for key in ('vertex_id', 'pixel_id', 'vertex_aliases', 'pixel_aliases'))
            or value.get('vertex_sha256') != RIVER_VERTEX_SHA256
            or value.get('pixel_sha256') != RIVER_PIXEL_SHA256
            or not re.fullmatch('[0-9a-f]{64}', str(expected_catalog))
            or value.get('catalog_sha256') != expected_catalog
            or value.get('target') != requested or value.get('viewport') != requested
            or value.get('target_format') != 21
            or any(type(value.get(key)) is not int or value[key] != 0
                   for key in ('alpha_blend', 'alpha_test', 'srgb_write'))):
        return None
    for key, count in (('vs_constants_12_15', 16), ('vs16_eye', 3),
                       ('ps5_sun_direction', 3), ('ps7_sun_specular', 3)):
        values = value.get(key)
        if not isinstance(values, list) or len(values) != count or not all(_number(v) for v in values):
            return None
    return value


EFFECT_EPOCH_FIELDS = ('effects_requested', 'effects_configured', 'effects_active', 'effects_revision',
    'effects_unsafe', 'effects_drawn_revision', 'effects_drawn_active', 'effects_frame_has_geometry',
    'effects_faulted', 'effects_disabled_reason', 'effects_module_faulted',
    'effects_module_shader_failures', 'effects_module_set_failures',
    'effects_module_restore_failures', 'effects_module_exceptions')


def _effect_epoch(frame):
    """Record configuration of actual geometry; this does not prove any material changed."""
    if 'effects_config_version' in frame and (type(frame['effects_config_version']) is not int
            or frame['effects_config_version'] not in (0, 1, 2)):
        raise ValueError('Invalid native material configuration version.')
    if not any(key in frame for key in EFFECT_EPOCH_FIELDS):
        return None
    if any(type(frame.get(key)) is not int or not 0 <= frame[key] <= 0xffffffff
           for key in EFFECT_EPOCH_FIELDS):
        raise ValueError('Incomplete or invalid per-frame effect epoch.')
    result = {key: frame[key] for key in EFFECT_EPOCH_FIELDS}
    if 'effects_config_version' in frame:
        version = frame['effects_config_version']
        if type(version) is not int or version not in (0, 1, 2):
            raise ValueError('Invalid native material configuration version.')
        result['effects_config_version'] = version
    requested, active = result['effects_requested'], result['effects_active']
    if (requested & ~7 or active & ~requested or result['effects_drawn_active'] & ~requested
            or any(result[key] not in (0, 1) for key in ('effects_configured', 'effects_unsafe',
                'effects_frame_has_geometry', 'effects_faulted', 'effects_module_faulted'))):
        raise ValueError('Invalid material effect flags.')
    healthy = not any(result[key] for key in ('effects_unsafe', 'effects_faulted',
        'effects_disabled_reason', 'effects_module_faulted', 'effects_module_shader_failures',
        'effects_module_set_failures', 'effects_module_restore_failures', 'effects_module_exceptions'))
    result['drawn_configuration_consistent'] = bool(healthy and result['effects_frame_has_geometry'] == 1
        and result['effects_drawn_revision'] == result['effects_revision']
        and result['effects_drawn_active'] == active
        and (not requested or result['effects_configured'] == 1))
    result['individual_material_changes_proven'] = False
    return result


def _material_configuration(raw):
    """Inspect the immutable fixed transport; no path/program in it is opened."""
    if not isinstance(raw, bytes) or len(raw) not in (4132, 6180):
        raise ValueError('Unknown bounded material transport size.')
    size, version, flags, width, height = struct.unpack_from('<5I', raw)
    if size != len(raw) or (version, size) not in ((1, 4132), (2, 6180)) or flags & ~7:
        raise ValueError('Material size/version/flags disagree.')
    if not _size([width, height]) or width < 320 or height < 200 or width > 8192 or height > 8192 or width*height > 32*1024*1024:
        raise ValueError('Material dimensions exceed the native bound.')
    if not all(_number(value) for value in struct.unpack_from('<4f', raw, 20)):
        raise ValueError('Nonfinite material parameters.')
    for offset in range(36, len(raw), 1024):
        words = struct.unpack_from('<512H', raw, offset)
        if 0 not in words:
            raise ValueError('Unterminated fixed material path.')
        end = words.index(0)
        if any(words[end:]):
            raise ValueError('Nonzero material path tail.')
        raw[offset:offset+end*2].decode('utf-16-le')
    return {'bytes': size, 'version': version, 'flags': flags, 'render_size': [width, height]}


def _water_configuration(raw):
    if not isinstance(raw, bytes) or len(raw) != 1076:
        raise ValueError('Water transport requires 1076 bytes.')
    size, version, width, height, enabled = struct.unpack_from('<5I', raw)
    if (size != 1076 or version != 1 or enabled not in (0, 1) or not 800 <= width <= 3840
            or not 600 <= height <= 2160):
        raise ValueError('Invalid fixed WaterProgram configuration.')
    words = struct.unpack_from('<512H', raw, 20)
    if 0 not in words or any(words[words.index(0):]):
        raise ValueError('Invalid WaterProgram path array.')
    path = raw[20:20+words.index(0)*2].decode('utf-16-le')
    program_hash = raw[1044:].hex()
    variant, recipe = _water_variant(program_hash)
    return {'bytes': size, 'version': version, 'enabled': enabled, 'render_size': [width, height],
            'program_path': path, 'program_sha256': program_hash, 'variant': variant, 'recipe': recipe}


def _water_variant(program_hash):
    if program_hash == WATER_PROGRAM_SHA256:
        return 1, WATER_RECIPE
    if program_hash == WATER_BALANCED_PROGRAM_SHA256:
        return 2, WATER_BALANCED_RECIPE
    raise ValueError('Unpinned recipient-generated water program.')


def _water_stage_format(stage, variant):
    if stage.get('format') == 's7-recipient-water-stage-v1':
        return variant == 1
    return (stage.get('format') == 's7-recipient-water-variant-stage-v1'
            and type(stage.get('variant_id')) is int and stage['variant_id'] == variant
            and stage.get('variant') == {1: 'A', 2: 'B'}.get(variant))


def _water_program(raw):
    if (len(raw) != 3840
            or struct.unpack_from('<I', raw)[0] != 0xffff0300 or struct.unpack_from('<I', raw, len(raw)-4)[0] != 0xffff):
        raise ValueError('Recipient water shader identity/size/envelope changed.')
    program_hash = hashlib.sha256(raw).hexdigest()
    variant, recipe = _water_variant(program_hash)
    return {'bytes': len(raw), 'sha256': program_hash, 'variant': variant, 'recipe': recipe}


def _water_record(frame):
    value = frame.get('water')
    if value is None:
        return None, False, False
    if not isinstance(value, dict):
        raise ValueError('Water state must be a measured native object.')
    if 'variant' in value and (type(value['variant']) is not int or value['variant'] not in (0, 1, 2)):
        raise ValueError('Unknown measured water variant.')
    for key in ('frame', 'epoch', 'completed_applications', 'completed_restorations', 'last_applied_frame',
                'last_restored_frame', 'shader_creations', 'shader_failures', 'set_failures', 'restore_failures', 'exceptions'):
        if type(value.get(key)) is not int or not 0 <= value[key] <= 0xffffffffffffffff:
            raise ValueError('Invalid bounded water counter.')
    for key in ('requested', 'enabled', 'configured', 'faulted', 'unsafe', 'pending_original_refs'):
        if type(value.get(key)) is not int or value[key] not in (0, 1):
            raise ValueError('Invalid water status flag.')
    if value['frame'] != frame.get('frame') or not _positive(value['frame']) or not _positive(value['epoch']):
        raise ValueError('Water local frame plus origin does not match the actual capture.')
    healthy = not any(value[key] for key in ('faulted', 'unsafe', 'pending_original_refs', 'shader_failures',
                                           'set_failures', 'restore_failures', 'exceptions'))
    replacement = bool(healthy and value['requested'] == value['enabled'] == value['configured'] == 1
        and value['shader_creations'] > 0 and value['completed_applications'] > 0
        and value['completed_applications'] == value['completed_restorations']
        and value['last_applied_frame'] == value['last_restored_frame'] == value['frame'])
    control = bool(healthy and value['requested'] == value['enabled'] == 0
        and value['completed_applications'] == value['completed_restorations'] == 0)
    return value, replacement, control


def _comparison_record(frame):
    """Validate diagnostic raw PS80 state, never a global-pin-only proof."""
    value = frame.get('comparison')
    if value is None:
        return None, False
    if not isinstance(value, dict) or value.get('diagnostic_only') is not True:
        raise ValueError('Comparison state must be an explicit diagnostic record.')
    for key in ('frame', 'epoch', 'pin_committed_frame', 'completed_frame_applications',
                'completed_frame_restorations', 'last_applied_frame', 'last_restored_frame'):
        if type(value.get(key)) is not int or not 0 <= value[key] <= 0xffffffffffffffff:
            raise ValueError('Invalid bounded comparison frame/counter.')
    if value['frame'] != frame.get('frame') or not _positive(value['frame']) or not _positive(value['epoch']):
        raise ValueError('Stale or unconfigured comparison frame.')
    for key in ('requested', 'active', 'pinned', 'faulted', 'unsafe'):
        if type(value.get(key)) is not int or value[key] not in (0, 1):
            raise ValueError('Invalid comparison status flags.')
    if not value['requested'] and (value['active'] or value['pinned']):
        raise ValueError('Disabled comparison claims an active pin.')
    if value['pinned']:
        pin = value.get('pinned_ps80')
        if (not isinstance(pin, list) or len(pin) != 4 or not all(_number(v) for v in pin)
                or not 0 < value['pin_committed_frame'] <= value['frame']):
            raise ValueError('Comparison pin is incomplete or nonfinite.')
    elif value.get('pinned_ps80') is not None or value['pin_committed_frame']:
        raise ValueError('Unpinned comparison contains a claimed frozen value.')
    applied = bool(value['requested'] == value['active'] == value['pinned'] == 1
        and value['faulted'] == value['unsafe'] == 0
        and value['completed_frame_applications'] > 0
        and value['completed_frame_applications'] == value['completed_frame_restorations']
        and value['last_applied_frame'] == value['last_restored_frame'] == value['frame'])
    return value, applied


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


def _pe_slice(raw, rva, size):
    pe = struct.unpack_from('<I', raw, 0x3c)[0]
    if raw[:2] != b'MZ' or not 64 <= pe < 4096 or raw[pe:pe+6] != b'PE\0\0\x4c\x01':
        raise ValueError('Invalid original Gold image.')
    count, optional = struct.unpack_from('<H', raw, pe+6)[0], struct.unpack_from('<H', raw, pe+20)[0]
    if not 1 <= count <= 32 or optional < 224:
        raise ValueError('Invalid original Gold section table.')
    for i in range(count):
        start = pe + 24 + optional + i*40
        _, virtual, length, offset = struct.unpack_from('<4I', raw, start+8)
        if virtual <= rva and rva + size <= virtual + length:
            data = raw[offset+rva-virtual:offset+rva-virtual+size]
            if len(data) == size:
                return data
    raise ValueError('Original save-role callback is outside file-backed sections.')


def gold_save_role_proof(root):
    """Verify the narrow original Gold slot policy; read no save-body contents."""
    root = _plain(root)
    exe = _read(root / 'Data/Base/_Dbg/Bin/Release/Settlers7R.exe', 64*1024*1024)
    if hashlib.sha256(exe).hexdigest() != GOLD_SAVE_ROLE_PROOF['executable_sha256']:
        raise ValueError('Gold save-role proof requires the exact original executable.')
    source = GOLD_SAVE_ROLE_PROOF['get_slot_range']
    if hashlib.sha256(_pe_slice(exe, source['callback_rva'], source['callback_bytes'])).hexdigest() != source['callback_sha256']:
        raise ValueError('Original Gold save-range callback changed.')
    # The callback's demonstrated doubles are first=30/count=50; the exact
    # original Lua member identifies index1 quick/index2 auto and computes
    # slot=first+index-1. No source body is exported or stored in this report.
    import mmap
    from tools.material_shaders.inventory import bba, read_payload
    with _plain(root / 'Data/Base/BBA/Interface.bba').open('rb') as stream:
        with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as raw:
            archive = bba.Archive(raw, copy=False)
            member = read_payload(archive, GOLD_SAVE_ROLE_PROOF['source_member']['name'])
    if hashlib.sha256(member).hexdigest() != GOLD_SAVE_ROLE_PROOF['source_member']['sha256']:
        raise ValueError('Original Gold save-slot policy member changed.')
    return json.loads(json.dumps(GOLD_SAVE_ROLE_PROOF))


def ubisoft_save_role_proof(root):
    """Verify original History registration/callback/constants and original Lua."""
    root = _plain(root)
    exe = _read(root / 'Base/Bin/Settlers7R.exe', 64*1024*1024)
    proof = UBI_SAVE_ROLE_PROOF
    if hashlib.sha256(exe).hexdigest() != proof['executable_sha256']:
        raise ValueError('Ubisoft save-role proof requires the exact original executable.')
    registration, callback = proof['registration'], proof['get_slot_range']
    for item, prefix in ((registration, 'initializer'), (callback, 'callback')):
        if hashlib.sha256(_pe_slice(exe, item[prefix+'_rva'], item[prefix+'_bytes'])).hexdigest() != item[prefix+'_sha256']:
            raise ValueError('Original History save-range registration or callback changed.')
    if (_pe_slice(exe, registration['name_rva'], 23) != b'GetSaveGameSlotRangeSP\0'
            or struct.unpack('<d', _pe_slice(exe, callback['first_rva'], 8))[0] != 30
            or struct.unpack('<d', _pe_slice(exe, callback['count_rva'], 8))[0] != 50):
        raise ValueError('Original History save-range values changed.')
    import mmap
    from tools.material_shaders.inventory import bba, read_payload
    with _plain(root / 'Base/BBA/Interface.bba').open('rb') as stream:
        with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as raw:
            member = read_payload(bba.Archive(raw, copy=False), proof['source_member']['name'])
    if hashlib.sha256(member).hexdigest() != proof['source_member']['sha256']:
        raise ValueError('Original History save-slot policy member changed.')
    return json.loads(json.dumps(proof))


def _verify_steam_slot_read(observation, owned, case_directory):
    """Review retained original registration/code evidence, not an operator pass."""
    if (not isinstance(observation, dict) or not isinstance(owned, dict)
            or owned.get('sha256') != EDITIONS['steam-history']
            or not _positive(owned.get('pid')) or not _positive(owned.get('parent'))
            or observation.get('format') != 's7-original-steam-save-role-read-v1'
            or observation.get('edition') != 'steam-history'
            or observation.get('read_only') is not True
            or observation.get('automatic_classification_accepted') is not False
            or observation.get('restoration_performed') is not False
            or observation.get('identity') != {key: owned[key] for key in ('pid', 'parent', 'sha256')}
            or observation.get('runtime_mapping') != 'original registration plus complete verified instruction template'):
        raise ValueError('Original Steam save-role observation identity/provenance changed.')
    for key, expected in STEAM_SLOT_OBSERVATION.items():
        if observation.get(key) != expected:
            raise ValueError('Original Steam save-role registration/instructions/constants changed.')
    started, finished = (observation.get(key) for key in ('observer_started_monotonic_ns', 'observer_finished_monotonic_ns'))
    if not _positive(started) or not _positive(finished) or finished < started:
        raise ValueError('Invalid original Steam observation interval.')
    reference = observation.get('reader_source')
    if not isinstance(reference, dict) or reference.get('sha256') != STEAM_SLOT_READER_SHA256:
        raise ValueError('Unreviewed Steam save-role reader source.')
    path, _ = _bound(reference, ROOT, MAX_JSON)
    if path.parent != _plain(case_directory):
        raise ValueError('The retained Steam reader must belong to the same owned case.')


def steam_save_role_proof(root, observation_path, *, owned):
    """Require closed-case runtime code evidence and the exact original Lua policy."""
    root, observation_path = _plain(root), _plain(observation_path)
    terminal = json.loads(_read(observation_path.parent / 'result.private.json', MAX_JSON))
    if not isinstance(terminal, dict) or terminal.get('game_started') is not True or type(terminal.get('exit_code')) is not int:
        raise ValueError('Steam save-role policy requires a completed actual game case.')
    observation = json.loads(_read(observation_path, MAX_JSON))
    _verify_steam_slot_read(observation, owned, observation_path.parent)
    exe = _read(root / 'Base/Bin/Settlers7R.exe', 64*1024*1024)
    if hashlib.sha256(exe).hexdigest() != EDITIONS['steam-history']:
        raise ValueError('Steam save-role policy requires the exact original packed executable.')
    import mmap
    from tools.material_shaders.inventory import bba, read_payload
    with _plain(root / 'Base/BBA/Interface.bba').open('rb') as stream:
        with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as raw:
            member = read_payload(bba.Archive(raw, copy=False), GOLD_SAVE_ROLE_PROOF['source_member']['name'])
    if hashlib.sha256(member).hexdigest() != GOLD_SAVE_ROLE_PROOF['source_member']['sha256']:
        raise ValueError('Original Steam save-slot Lua policy member changed.')
    return {'format': 's7-steam-private-save-role-proof-v1', 'edition': 'steam-history',
        'executable_sha256': EDITIONS['steam-history'],
        'runtime_observation': _ref(observation_path), 'reader_source': observation['reader_source'],
        'get_slot_range': json.loads(json.dumps(STEAM_SLOT_OBSERVATION)),
        'source_member': GOLD_SAVE_ROLE_PROOF['source_member'].copy(),
        'quick_slot_index': 1, 'auto_slot_index': 2,
        'rotating_autosave_relative': UBI_SAVE_ROLE_PROOF['rotating_autosave_relative'],
        'restoration_performed': False}


def _save_classification(root, backup_root, before, allow_gold_rotating=False, rotating_relative=None):
    changes, rotating = [], []
    saves = {name: item for name, item in before.items() if Path(name).suffix.casefold() in ('.s7s', '.s7local')}
    for relative, item in saves.items():
        try:
            profile_root = _plain(root / 'LocalData/ProfileRoot')
            path = _plain(profile_root / relative)
            if not path.is_relative_to(profile_root):
                raise ValueError('Private save checkpoint escaped its owned profile.')
            raw = _read(path, MAX_IMAGE)
            if hashlib.sha256(raw).hexdigest() == item['sha256']:
                continue
        except (KeyError, OSError, ValueError):
            changes.append(relative)
            continue
        if allow_gold_rotating and relative == (rotating_relative or GOLD_SAVE_ROLE_PROOF['rotating_autosave_relative']):
            try:
                backup = _read(backup_root / relative, MAX_IMAGE)
                if (hashlib.sha256(backup).hexdigest() == item['sha256']
                        and ('size' not in item or item['size'] == len(backup))):
                    rotating.append({'relative': relative, 'before_image_identity_verified': True,
                                     'original_code_role_verified': True, 'restored': False})
                    continue
            except (OSError, ValueError):
                pass
        changes.append(relative)
    return {'all_original_private_saves_unchanged': not changes and not rotating,
            'manual_original_private_saves_unchanged': not changes,
            'private_rotating_autosave_changes': rotating,
            'unapproved_original_private_save_changes': changes}, bool(saves)


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
    if (directory / ARTIFACTS['water_config']).is_file():
        try:
            water = _water_configuration(_read(directory / ARTIFACTS['water_config'], 8192))
            program = directory / f"LocalData/MaterialShaders/{water['recipe']}/{water['program_sha256']}.ps.bin"
            if program.is_file():
                value['artifacts']['water_program'] = _ref(program)
        except (OSError, ValueError):
            pass  # Bind malformed config itself; aggregate reports its rejection.
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
            if name in ('effects_config', 'water_config', 'water_program'):
                parser = {'effects_config': _material_configuration, 'water_config': _water_configuration,
                          'water_program': _water_program}[name]
                data[name] = parser(_bound(reference, base, 8192)[1])
                continue
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
              and _positive(memory.get('samples')), 'unhealthy memory monitoring')
        check(type(memory.get('max_pressure')) is int and memory['max_pressure'] == 0,
              'memory pressure warning/limit recorded')
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
    save_preservation = None
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
            allow_rotating = False
            rotating_relative = None
            if 'save_role_proof' in data:
                try:
                    if value['edition'] == 'steam-history':
                        reference = value['artifacts'].get('save_role_read')
                        observation_path, _ = _bound(reference, base, MAX_JSON)
                        expected = steam_save_role_proof(Path(journal['root']), observation_path, owned=owned)
                    else:
                        builder = {'gold': gold_save_role_proof, 'ubisoft-history': ubisoft_save_role_proof}.get(value['edition'])
                        if builder is None:
                            raise ValueError('No independently verified slot-role policy for this edition.')
                        expected = builder(Path(journal['root']))
                    allow_rotating = json.dumps(data['save_role_proof'], sort_keys=True) == json.dumps(expected, sort_keys=True)
                    rotating_relative = expected['rotating_autosave_relative']
                    check(allow_rotating, 'private autosave role proof changed')
                except (OSError, ValueError):
                    rejected.append('private autosave role proof unavailable')
            profile_path, _ = _bound(value['artifacts']['profile_before'], base, MAX_JSON)
            save_preservation, has_saves = _save_classification(Path(journal['root']), profile_path.parent / 'portable', before, allow_rotating, rotating_relative)
            if not has_saves:
                missing.append('original private save checkpoint')
            check(not save_preservation['unapproved_original_private_save_changes'], 'original private save changed')
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
            attached_or_clean_detach = (_positive(hook.get('attached'))
                or (hook.get('attached') == 0 and hook.get('hook_detached') == 1
                    and hook.get('terminal_cleanup') == 1
                    and (_positive(hook.get('window_shutdowns'))
                         or (data.get('hook_build', {}).get('format') == 's7-native-graphics-build-v2'
                             and _build_contract(data['hook_build'])[2]
                             and _positive(hook.get('engine_deinit_calls'))
                             and hook.get('engine_deinit_returned') == hook['engine_deinit_calls']
                             and hook.get('engine_deinit_rejected') == 0
                             and hook.get('engine_deinit_cleanup') == 1
                             and hook.get('shutdown_stage') == 3))
                    and result.get('exit_code') == 0 and result.get('owned_timeout_or_stop') is False))
            check(attached_or_clean_detach, 'no active attachment or verified graceful terminal detach')
            for key in ('present_calls', 'output_present_calls', 'high_resolution_draws', 'indexed_draws'):
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
            build_sources, build_binaries, build_schema = _build_contract(build)
            check(build_schema and isinstance(build_files, dict)
                  and build_files.get('WePlaySupersamplingLab.dll') == initialized.get('dll_sha256')
                  and bool(re.fullmatch('[0-9a-f]{64}', str(build.get('build_sha256')))),
                  'hook initialization/build manifest mismatch')
            check(isinstance(build_files, dict) and build_schema
                  and build_sources | build_binaries == build_files.keys(),
                'incomplete native hook build source identities')
            check(isinstance(build_files, dict) and all(isinstance(pin, str) and re.fullmatch('[0-9a-f]{64}', pin)
                  for pin in build_files.values()), 'invalid native hook build source identity')
            if isinstance(build_files, dict):
                build_path, _ = _bound(value['artifacts']['hook_build'], base, MAX_JSON)
                for name in build_sources | build_binaries:
                    if name not in build_files:
                        continue
                    try:
                        raw = _read(build_path.parent / name, MAX_JSON if name in build_sources else MAX_IMAGE)
                        check(hashlib.sha256(raw).hexdigest() == build_files[name], 'compiled hook source/binary snapshot changed')
                    except (OSError, ValueError):
                        rejected.append('compiled hook source/binary snapshot unavailable')
    comparison_requested = checkpoint.get('comparison_uv_requested') is True
    if 'comparison_uv_requested' in checkpoint:
        check(type(checkpoint['comparison_uv_requested']) is bool, 'invalid comparison request in source checkpoint')
    comparison_build = (data.get('hook_build', {}).get('source_set') in ('effects-v2', 'effects-v3', 'effects-v4', 'effects-v5', 'effects-v6', 'effects-v7', 'effects-v8')
                        and _build_contract(data['hook_build'])[2])
    configured = data.get('comparison_configured', {})
    comparison_admitted = bool(comparison_requested and comparison_build and value['role'] == 'candidate'
        and configured.get('succeeded') is True and configured.get('worker_code_rx') is True
        and configured.get('original_entry_guarded') is True
        and configured.get('export') == 'WePlayNativeGraphicsConfigureComparison'
        and configured.get('dll_sha256') == data.get('hook_init', {}).get('dll_sha256')
        and re.fullmatch('[0-9a-f]{64}', str(data.get('hook_init', {}).get('observer_catalog_sha256'))))
    if comparison_requested:
        check(comparison_admitted, 'diagnostic comparison request/configuration binding missing')
    material_config = data.get('effects_config')
    material_receipt = data.get('effects_configured', {})
    source_build = data.get('hook_build', {})
    modern_build = source_build.get('source_set') in ('effects-v3', 'effects-v4', 'effects-v5', 'effects-v6', 'effects-v7', 'effects-v8') and _build_contract(source_build)[2]
    material_admitted = bool(material_config and value['role'] == 'candidate'
        and _build_contract(source_build)[2]
        and source_build.get('source_set') in ('effects-v1', 'effects-v2', 'effects-v3', 'effects-v4', 'effects-v5', 'effects-v6', 'effects-v7', 'effects-v8')
        and (material_config['version'] == 1 or modern_build)
        and material_config['render_size'] == requested
        and material_receipt.get('succeeded') is True and material_receipt.get('worker_code_rx') is True
        and material_receipt.get('original_entry_guarded') is True
        and material_receipt.get('export') == 'WePlayNativeGraphicsConfigureEffects'
        and material_receipt.get('dll_sha256') == data.get('hook_init', {}).get('dll_sha256')
        and re.fullmatch('[0-9a-f]{64}', str(material_receipt.get('dll_sha256')))
        and source_build.get('files', {}).get('WePlaySupersamplingLab.dll') == material_receipt.get('dll_sha256'))
    if material_config is not None:
        check(material_admitted, 'material configuration/source receipt binding missing')
    water_config, water_stage, water_receipt = (data.get(key, {}) for key in ('water_config', 'water_stage', 'water_configured'))
    water_requested = checkpoint.get('water_program_requested') is True
    if 'water_program_requested' in checkpoint:
        check(type(checkpoint['water_program_requested']) is bool, 'invalid separate-water request checkpoint')
    water_build = source_build.get('source_set') in ('effects-v4', 'effects-v5', 'effects-v6', 'effects-v7', 'effects-v8') and _build_contract(source_build)[2]
    water_admitted = False
    if water_requested or any(key in data for key in ('water_config', 'water_stage', 'water_configured', 'water_program')):
        try:
            case_path, _ = _bound(value['artifacts'].get('owned'), base, MAX_JSON)
            expected_program = _plain(case_path.parent / f"LocalData/MaterialShaders/{water_config['recipe']}/{water_config['program_sha256']}.ps.bin")
            program_path, _ = _bound(value['artifacts'].get('water_program'), base, 8192)
            stage_path, _ = _bound(value['artifacts'].get('water_stage'), base, MAX_JSON)
            water_admitted = bool(water_requested and water_build and value['role'] == 'candidate'
                and water_config.get('render_size') == requested and water_config.get('enabled') == 1
                and _plain(water_config.get('program_path', '')) == expected_program == program_path
                and stage_path.parent == case_path.parent
                and _plain(water_stage.get('program_path', '')) == expected_program
                and _water_stage_format(water_stage, water_config.get('variant'))
                and water_stage.get('edition') == ('gold' if value['edition'] == 'gold' else 'history')
                and water_stage.get('config_bytes') == 1076 and water_stage.get('render') == requested
                and water_stage.get('recipe') == water_config['recipe'] and water_stage.get('private_case') is True
                and water_stage.get('program_bytes') == 3840 and water_stage.get('program_sha256') == water_config['program_sha256']
                and water_stage.get('source_resources') == WATER_SOURCES
                and data.get('water_program', {}).get('sha256') == water_config['program_sha256']
                and data.get('water_program', {}).get('bytes') == 3840
                and water_stage.get('no_legacy_river_scaling_permitted') is True
                and water_receipt.get('succeeded') is True and water_receipt.get('worker_code_rx') is True
                and water_receipt.get('original_entry_guarded') is True
                and water_receipt.get('export') == 'WePlayNativeGraphicsConfigureWater'
                and water_receipt.get('dll_sha256') == data.get('hook_init', {}).get('dll_sha256')
                and re.fullmatch('[0-9a-f]{64}', str(water_receipt.get('dll_sha256')))
                and source_build.get('files', {}).get('WePlaySupersamplingLab.dll') == water_receipt.get('dll_sha256'))
        except (KeyError, OSError, ValueError):
            water_admitted = False
        check(water_admitted, 'separate water program/configuration/source receipt binding missing')
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
            uniforms = _scene_uniforms(frame, requested,
                data.get('hook_init', {}).get('observer_catalog_sha256'))
            effects_build = (data.get('hook_build', {}).get('format') == 's7-native-graphics-build-v2'
                             and _build_contract(data['hook_build'])[2])
            if frame.get('scene_uniforms') is not None and (uniforms is None or not effects_build):
                rejected.append('invalid or unsupported same-frame original River shader observation')
                uniforms = None
            epoch = _effect_epoch(frame)
            material_version = frame.get('effects_config_version')
            material_version_measured = type(material_version) is int and material_version in (1, 2)
            if material_version_measured and material_version == 2:
                check(modern_build and material_admitted and material_config['version'] == 2,
                      'native V2 material version lacks exact source/configuration binding')
            if material_config is not None:
                if material_version is None and material_config['version'] == 1:
                    material_version = 1  # Historical transport provenance, not a measured capture field.
                check(material_version == material_config['version'], 'native material version/configuration mismatch')
                if epoch is not None:
                    check(not epoch['effects_active'] & ~material_config['flags'], 'active materials exceed the bound prepared configuration')
            elif modern_build and epoch is not None and epoch['effects_requested']:
                rejected.append('modern material frame lacks immutable configuration/receipt')
            comparison, isolated = _comparison_record(frame)
            if comparison is not None:
                if not comparison_build or (comparison['requested'] and not comparison_admitted):
                    rejected.append('unsupported or unbound diagnostic comparison record')
                    isolated = False
                elif comparison['requested'] and not isolated:
                    rejected.append('comparison lacks exact-frame application/restoration proof')
            elif comparison_requested:
                missing.append('per-frame diagnostic comparison state')
            water, water_replacement, water_control = _water_record(frame)
            if water is not None:
                if not water_admitted:
                    rejected.append('unsupported or unbound separate-water state')
                    water_replacement = water_control = False
                elif not (water_replacement or water_control):
                    rejected.append('water frame lacks matching application/restoration or clean control proof')
                if water_replacement and ((water_config.get('variant') == 2 and water.get('variant') != 2)
                        or (water.get('variant') in (1, 2) and water.get('variant') != water_config.get('variant'))):
                    rejected.append('native water variant/configuration mismatch')
                    water_replacement = False
                if epoch is not None and epoch['effects_active'] & 1:
                    rejected.append('legacy River VS mutation overlaps separate WaterProgram')
                    water_replacement = water_control = False
            elif water_requested:
                missing.append('per-frame native separate-water state')
            water_only = bool(water_admitted and material_admitted and material_config['flags'] == 0
                and epoch is not None and epoch['effects_requested'] == 1 and epoch['effects_active'] == 0
                and (water_replacement or water_control))
            native_frames.append({'sequence': sequence, 'frame': number, 'gpu_buffer_pair_bound': bool(buffers),
                'native_scene_dimensions_recorded': bool(scene),
                'filtered_present_recorded': bool(filtered), 'original_present_recorded': bool(original),
                'vision_main_context_camera': camera, 'visible_camera_validated': False,
                'camera_provenance': 'original Vision main-context exports; visible scene tracking unvalidated',
                'scene_uniforms': uniforms,
                'material_effect_epoch': epoch,
                'material_config_version': material_version,
                'material_config_version_measured': material_version_measured,
                'material_configuration_bound': material_admitted,
                'water_shader_state': water,
                'water_selected_variant': water_config.get('variant'),
                'water_native_variant_measured': water.get('variant') if water is not None else None,
                'water_shader_replacement_recorded': bool(water_replacement and buffers and scene),
                'water_original_control_recorded': bool(water_control and buffers and scene),
                'water_only_configuration_recorded': bool(water_only and buffers and scene),
                'diagnostic_comparison': comparison,
                'draw_local_ps80_isolation_recorded': bool(isolated and buffers and scene and comparison_admitted),
                'same_frame_river_uniforms_recorded': bool(uniforms is not None and buffers and scene),
                'sun_uniform_provenance': 'raw original River PS5.xyz; no elevation or exposure inference' if uniforms else None,
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
            'material_configuration': ({**material_config, 'source_receipt_bound': material_admitted}
                                       if material_config is not None else None),
            'water_configuration': ({**water_config, 'source_receipt_bound': water_admitted}
                                    if water_config else None),
            'save_preservation': save_preservation,
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
