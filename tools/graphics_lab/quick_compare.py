"""Seven native captures in one already-loaded, explicitly admitted palette case.

No game launch, PID/export selector, UI automation or benchmark is exposed.
Raw captures stay private; PNG galleries preserve their RGB pixels exactly.
"""
from __future__ import annotations

import argparse
import hashlib
import html
import json
import os
from pathlib import Path
import re
import struct
import time
import zlib

from . import reconfigure as guard
from tools.material_shaders import compare

MAX_JSON = 1024 * 1024
CAPTURE_SIZES = ([3840, 2160], [1920, 1080])
VARIANTS = ('A', 'B', 'C')
PROGRAM_LABELS = ('A · Wrap 0,25', 'B · Wrap 0,50', 'C · Wrap 0,75')
RECIPES = ('leaf-palette-A-v5', 'leaf-palette-B-v6', 'leaf-palette-C-v7')
PROGRAMS = (
    '1b825358f46c39b04b1d540738f4be5e862bfe6627b8545ab092c471d259477c',
    '284d5c84ee6700039ee818fe7f063b2b90536beac45e261a5b3e0cf8a6d1746a',
    'b869fe375b76509f60a48d34bde83565d28c0416aca85f27c8a445045cb6a51f',
)
WATER_NAMES = ('accepted_B', 'clear_bank', 'calm_gloss')
WATER_LABELS = ('A · abgenommenes Wasser B', 'B · Clear Bank', 'C · Calm Gloss')
WATER_RECIPES = ('river-depth-fresnel-v2-balanced', 'river-palette-clear-bank-v3', 'river-palette-calm-gloss-v4')
WATER_PROGRAMS = (
    'a8a591b346911622bfc9899fa28cbb022a42152df41ed822ef3cfff7c254e2f4',
    '0bf632f2862eb635c25131e575b4c2d5c35ba9fa73cb3aa01591f9afaecd543c',
    '5d027025eb77c23d902c8ccdbc4cddde70064f387e0c34bd4039ee1cf52c6c70',
)
PROFILES = ('leaf-palette', 'water-palette')
PLAN = ((0, 0), (0, 4), (0, 0), (1, 4), (1, 0), (2, 4), (2, 0))
FAULTS = ('effects_unsafe', 'effects_faulted', 'effects_disabled_reason',
          'effects_module_faulted', 'effects_module_shader_failures',
          'effects_module_set_failures', 'effects_module_restore_failures', 'effects_module_exceptions')


def read_json(path):
    return guard._json(path)


def write_new(path, data):
    with path.open('x', encoding='utf8') as stream:
        json.dump(data, stream, indent=2, allow_nan=False)
        stream.write('\n')


def memory_healthy(value, files, now_ns):
    stamp = value.get('sampled_monotonic_ns')
    if (value.get('format') != 's7-memory-live-v1' or value.get('pid') != files['owned']['pid']
            or value.get('owned_record_sha256') != files['record_sha256']
            or type(stamp) is not int or not 0 <= now_ns - stamp <= 5_000_000_000):
        raise ValueError('Missing, stale or foreign owner memory heartbeat.')
    state = value.get('guardian')
    if (not isinstance(state, dict) or state.get('monitor_failed') is not False
            or state.get('recovery_required') is not False or type(state.get('max_pressure')) is not int
            or state['max_pressure'] != 0 or type(state.get('samples')) is not int or state['samples'] <= 0):
        raise ValueError('The unchanged memory guardian is not healthy and pressure-free.')
    return value


def palette_pins(case, profile='leaf-palette'):
    """Fixed three-program stage, never accept caller-supplied shader paths/code."""
    if profile == 'water-palette':
        return water_palette_pins(case)
    if profile != 'leaf-palette':
        raise ValueError('Unknown fixed comparison profile.')
    checkpoint = read_json(case / 'checkpoint.private.json')
    if checkpoint.get('leaf_palette_requested') is not True:
        raise ValueError('The owner did not explicitly initialize a Leaf palette case.')
    if any(checkpoint.get(key, False) is not False for key in ('reshade_requested', 'detailed_shadows_requested',
        'comparison_uv_requested', 'water_program_requested', 'shadow_filter_requested', 'ambient_occlusion_requested',
        'terrain_macro_requested', 'leaf_wrapped_requested', 'leaf_hemisphere_requested')):
        raise ValueError('The quick palette comparison requires other material/diagnostic variants to be disabled.')
    raw = guard._read(case / 'effects-config.bin', 4120)
    if len(raw) != 4120 or struct.unpack_from('<2I', raw) != (4120, 4):
        raise ValueError('The case lacks its fixed version4 palette config.')
    _, _, flags, width, height, selected = struct.unpack_from('<6I', raw)
    if flags not in (0, 4) or selected not in (0, 1, 2) or (width, height) != (3840, 2160):
        raise ValueError('Invalid fixed palette geometry/selection.')
    # The fixed recipient stager binds all three programme identities.
    stage = read_json(case / 'material-stage.private.json')
    programs = stage.get('palette_variants')
    if (stage.get('format') != 's7-recipient-material-stage-v4' or stage.get('config_version') != 4
            or not isinstance(programs, list) or len(programs) != 3
            or guard._lab(stage.get('artifact_root', '')) != case):
        raise ValueError('The immutable three-program palette receipt is missing.')
    pins = []
    for index, row in enumerate(programs):
        if (not isinstance(row, dict) or row.get('index') != index or row.get('name') != VARIANTS[index]
                or row.get('sha256') != PROGRAMS[index] or row.get('bytes') != 1372 or row.get('recipe') != RECIPES[index]):
            raise ValueError('Invalid fixed palette program identity.')
        path_field = raw[24 + index * 1024:1048 + index * 1024].decode('utf-16-le')
        path, separator, tail = path_field.partition('\0')
        if not separator or tail.strip('\0') or Path(path).name.casefold() != PROGRAMS[index] + '.ps.bin':
            raise ValueError('The initialized config does not name the fixed palette program.')
        artifact = guard._lab(path)
        expected = case / 'LocalData' / 'MaterialShaders' / RECIPES[index] / (PROGRAMS[index] + '.ps.bin')
        if artifact != expected or hashlib.sha256(guard._read(artifact, 1372)).hexdigest() != PROGRAMS[index]:
            raise ValueError('The owned recipient fixed program changed or escaped LocalData.')
        pins.append({'variant': index, 'name': VARIANTS[index], 'label': PROGRAM_LABELS[index], 'sha256': row['sha256']})
    if len({p['sha256'] for p in pins}) != 3:
        raise ValueError('A palette requires three different shader programs.')
    return pins


def water_palette_pins(case):
    checkpoint = read_json(case / 'checkpoint.private.json')
    if checkpoint.get('water_palette_requested') is not True:
        raise ValueError('The owner did not explicitly initialize a Water palette case.')
    if any(checkpoint.get(key, False) is not False for key in ('reshade_requested', 'detailed_shadows_requested',
        'comparison_uv_requested', 'shadow_filter_requested', 'ambient_occlusion_requested', 'terrain_macro_requested',
        'leaf_wrapped_requested', 'leaf_hemisphere_requested', 'leaf_palette_requested')):
        raise ValueError('Water palette comparison requires other material/diagnostic variants disabled.')
    raw = guard._read(case / 'water-palette-config.bin', 3096)
    if len(raw) != 3096:
        raise ValueError('The fixed Water palette config is missing.')
    size, version, width, height, enabled, selected = struct.unpack_from('<6I', raw)
    if (size, version, width, height) != (3096, 2, 3840, 2160) or enabled not in (0, 1) or selected not in (0, 1, 2):
        raise ValueError('Invalid fixed Water palette header.')
    stage = read_json(case / 'water-palette-stage.private.json')
    programs = stage.get('palette_variants')
    if (stage.get('format') != 's7-recipient-water-palette-stage-v2'
            or stage.get('config_version') != 2 or guard._lab(stage.get('artifact_root', '')) != case
            or not isinstance(programs, list) or len(programs) != 3):
        raise ValueError('Missing immutable three-program Water stage.')
    pins = []
    for index, row in enumerate(programs):
        if (not isinstance(row, dict) or row.get('index') != index or row.get('name') != WATER_NAMES[index]
                or row.get('sha256') != WATER_PROGRAMS[index] or row.get('bytes') != 3840
                or row.get('recipe') != WATER_RECIPES[index]):
            raise ValueError('Water palette programme selection/hash disagrees with fixed policy.')
        field = raw[24 + index * 1024:1048 + index * 1024].decode('utf-16-le')
        path, separator, tail = field.partition('\0')
        if not separator or tail.strip('\0'):
            raise ValueError('Water palette path must have a zero-filled fixed field.')
        artifact = guard._lab(path)
        expected = case / 'LocalData' / 'MaterialShaders' / WATER_RECIPES[index] / (WATER_PROGRAMS[index] + '.ps.bin')
        if artifact != expected or hashlib.sha256(guard._read(artifact, 3840)).hexdigest() != WATER_PROGRAMS[index]:
            raise ValueError('Fixed Water programme escaped its owned stage or changed.')
        pins.append({'variant': index, 'name': WATER_NAMES[index], 'label': WATER_LABELS[index],
                     'sha256': WATER_PROGRAMS[index]})
    return pins


def capture_proof(meta, variant, enabled, program_sha256, catalog_sha256, profile='leaf-palette'):
    if profile == 'water-palette':
        return water_capture_proof(meta, variant, enabled, program_sha256, catalog_sha256)
    if profile != 'leaf-palette':
        raise ValueError('Unknown fixed comparison profile.')
    if (meta.get('format') != 's7-native-frame-capture-v1' or meta.get('complete') is not True
            or (meta.get('source'), meta.get('output')) != CAPTURE_SIZES
            or meta.get('status') != 0 or meta.get('fallback_used') is not False
            or meta.get('filtered_api_succeeded') is not True
            or meta.get('presentation_path') != 'filtered_output'
            or any(meta.get(key) != 0 for key in ('present_hresult', 'filtered_present_hresult', 'readback_hresult'))):
        raise ValueError('Native capture or filtered presentation did not complete successfully.')
    if (meta.get('effects_config_version') != 4 or meta.get('effects_configured') != 1
            or meta.get('effects_active') != enabled or meta.get('effects_drawn_active') != enabled
            or meta.get('effects_revision') != meta.get('effects_drawn_revision')
            or type(meta.get('effects_revision')) is not int or meta['effects_revision'] <= 0
            or meta.get('effects_frame_has_geometry') != 1 or any(meta.get(key) != 0 for key in FAULTS)):
        raise ValueError('Palette epoch, activation or fault evidence disagrees with the drawn frame.')
    palette = meta.get('leaf_palette')
    if (not isinstance(palette, dict) or palette.get('selected_variant') != variant
            or palette.get('configured_program_sha256') != program_sha256
            or meta.get('effects_selected_variant') != variant
            or meta.get('effects_drawn_frame') != meta.get('frame')
            or type(palette.get('module_epoch')) is not int or palette['module_epoch'] <= 0
            or any(palette.get(key) != 0 for key in ('faulted', 'shader_failures', 'set_failures',
                'restore_failures', 'exceptions', 'active_transactions', 'active_original_refs'))):
        raise ValueError('The captured frame does not prove the selected shader program.')
    if enabled:
        applications = meta.get('effects_variant_frame_applications')
        if (meta.get('effects_drawn_variant') != variant or palette.get('drawn_variant') != variant
                or palette.get('drawn_program_sha256') != program_sha256
                or palette.get('drawn_frame') != meta.get('frame')
                or type(applications) is not int or applications <= 0
                or meta.get('effects_variant_frame_restorations') != applications
                or meta.get('effects_variant_frame_failed_draws') != 0):
            raise ValueError('No positive balanced successful palette draws in this exact frame.')
        for key in ('variant_applications', 'variant_restorations', 'variant_successful_draws', 'variant_failed_draws', 'program_creations'):
            values = palette.get(key)
            if not isinstance(values, list) or len(values) != 3 or any(type(value) is not int or value < 0 for value in values):
                raise ValueError('Missing raw per-program counters.')
        if (palette['variant_applications'][variant] <= 0
                or palette['variant_restorations'][variant] != palette['variant_applications'][variant]
                or palette['variant_successful_draws'][variant] <= 0 or palette['variant_failed_draws'][variant] != 0
                or any(value <= 0 for value in palette['program_creations'])):
            raise ValueError('Selected programme counters do not prove real successful draws.')
    elif (meta.get('effects_drawn_variant') is not None or palette.get('drawn_variant') is not None
            or palette.get('drawn_program_sha256') is not None):
        raise ValueError('An AUS frame must not claim a modified Leaf shader draw.')
    if any(not re.fullmatch('[0-9a-f]{64}', str(meta.get(stage + '_bmp_sha256', ''))) for stage in ('source', 'output')):
        raise ValueError('The native capture lacks complete image hashes.')
    scene = compare._uniforms(meta, meta.get('source_bmp_sha256'), 'source')
    if scene is None or scene.get('catalog_sha256') != catalog_sha256:
        raise ValueError('Missing same-frame original camera/light catalog provenance.')
    return meta


def water_capture_proof(meta, variant, enabled, program_sha256, catalog_sha256):
    if (meta.get('format') != 's7-native-frame-capture-v1' or meta.get('complete') is not True
            or (meta.get('source'), meta.get('output')) != CAPTURE_SIZES or meta.get('status') != 0
            or meta.get('fallback_used') is not False or meta.get('filtered_api_succeeded') is not True
            or meta.get('presentation_path') != 'filtered_output'
            or any(meta.get(key) != 0 for key in ('present_hresult', 'filtered_present_hresult', 'readback_hresult'))):
        raise ValueError('Native Water capture/presentation failed.')
    if (meta.get('effects_active') != 0 or meta.get('effects_drawn_active') != 0
            or any(meta.get(key) != 0 for key in FAULTS)):
        raise ValueError('Another material effect or renderer fault confounds the Water comparison.')
    water, state = meta.get('water'), meta.get('water_palette')
    if (not isinstance(water, dict) or water.get('enabled') != enabled or water.get('requested') != enabled
            or water.get('configured') != 1 or water.get('frame') != meta.get('frame')
            or not isinstance(state, dict) or state.get('config_version') != 2
            or type(meta.get('frame')) is not int or meta['frame'] <= 0
            or state.get('selected_variant') != variant or state.get('configured_program_sha256') != program_sha256
            or state.get('frame') != meta.get('frame') or type(state.get('epoch')) is not int or state['epoch'] <= 0
            or water.get('epoch') != state['epoch']
            or any(state.get(key) != 0 for key in ('faulted', 'unsafe', 'shader_failures', 'set_failures',
                'restore_failures', 'exceptions', 'active_transactions', 'pending_original_refs', 'cached_refs'))):
        raise ValueError('The exact Water selection, frame, epoch or fault proof is missing.')
    if enabled:
        count = state.get('completed_applications')
        successes = state.get('completed_successful_draws')
        if (state.get('drawn_variant') != variant or state.get('drawn_program_sha256') != program_sha256
                or type(count) is not int or count <= 0 or state.get('completed_restorations') != count
                or type(successes) is not int or successes != count or state.get('completed_failed_draws') != 0):
            raise ValueError('No balanced successful Water programme draws in this exact captured frame.')
        for key in ('variant_applications', 'variant_restorations', 'variant_successful_draws', 'variant_failed_draws', 'program_creations'):
            values = state.get(key)
            if not isinstance(values, list) or len(values) != 3 or any(type(value) is not int or value < 0 for value in values):
                raise ValueError('Missing raw three-programme Water counters.')
        if (state['variant_applications'][variant] <= 0
                or state['variant_applications'][variant] != state['variant_restorations'][variant]
                or state['variant_successful_draws'][variant] <= 0 or state['variant_failed_draws'][variant] != 0
                or any(value <= 0 for value in state['program_creations'])):
            raise ValueError('Water programme counters do not corroborate the selection.')
    elif (state.get('drawn_variant') is not None or state.get('drawn_program_sha256') is not None
            or any(state.get(key) != 0 for key in ('completed_applications', 'completed_restorations',
                'completed_successful_draws', 'completed_failed_draws'))):
        raise ValueError('An AUS frame must not claim replacement Water programme draws.')
    if any(not re.fullmatch('[0-9a-f]{64}', str(meta.get(stage + '_bmp_sha256', ''))) for stage in ('source', 'output')):
        raise ValueError('Water capture lacks its exact image hashes.')
    scene = compare._uniforms(meta, meta.get('source_bmp_sha256'), 'source')
    if scene is None or scene.get('catalog_sha256') != catalog_sha256:
        raise ValueError('Missing original same-frame Water camera/light provenance.')
    return meta


def pair_proof(left, middle, right):
    keys = ('vs_constants_12_15', 'vs16_eye', 'ps5_sun_direction', 'ps7_sun_specular')
    if any(left['scene_uniforms'][key] != sample['scene_uniforms'][key]
           for sample in (middle, right) for key in keys):
        raise ValueError('Camera or raw lighting changed within the AUS/AN/AUS triplet.')
    if any(left[key] != sample[key] for sample in (middle, right) for key in ('source', 'output', 'edition')):
        raise ValueError('Render/output dimensions or edition changed within the triplet.')
    return {'captured_raw_camera_light_exact': True, 'exposure_measured': False,
            'animation_frozen': False, 'quality_assessed': False}


class CaseSession:
    """Queue only fixed case commands; retain and revalidate the original owner."""
    def __init__(self, case, dll, *, profile='leaf-palette'):
        if profile not in PROFILES:
            raise ValueError('Unknown fixed comparison profile.')
        self.profile = profile
        self.files = guard._case_files(case, dll)
        self.case = self.files['case']
        self.pins = palette_pins(self.case, profile)
        self.owner = guard._CaseOwner(self.files)
        self.sequence = 0
        self.records = []
        try:
            command = self.case / 'native-command.json'
            if command.exists():
                previous = read_json(command)
                self.sequence = previous.get('sequence', -1)
                if type(self.sequence) is not int or not 0 < self.sequence < 999900:
                    raise ValueError('Invalid or exhausted case command sequence.')
                if not (self.case / f'command-{self.sequence}.private.json').exists():
                    raise ValueError('Another case command is still outstanding.')
            for index, path in enumerate(self.case.glob('command-*.private.json')):
                if index >= 512:
                    raise ValueError('The existing case command inventory exceeds its bound.')
                match = re.fullmatch(r'command-([0-9]{1,7})\.private\.json', path.name)
                if match:
                    self.sequence = max(self.sequence, int(match[1]))
            self.check()
        except BaseException:
            self.close()
            raise

    def check(self):
        if (self.case / 'result.private.json').exists() or (self.case / 'close-needed.private.json').exists():
            raise ValueError('The owned case ended or its measurement budget expired.')
        return memory_healthy(read_json(self.case / 'memory-live.private.json'), self.files, time.monotonic_ns())

    def _queue(self, operation, deadline, *, variant=None, enabled=None, cleanup=False):
        if time.monotonic() >= deadline:
            raise TimeoutError('The bounded batch deadline expired.')
        if not cleanup:
            self.check()
        self.owner.owned()
        if (self.case / 'result.private.json').exists():
            raise ValueError('The original case has already closed.')
        self.sequence += 1
        if self.sequence > 1_000_000:
            raise ValueError('Case command sequence exhausted.')
        request = {'operation': operation, 'sequence': self.sequence}
        if operation == 'variant':
            request.update(variant=variant, enabled=enabled)
        temp = self.case / f'quick-command-{self.sequence}.tmp'
        write_new(temp, request)
        os.replace(temp, self.case / 'native-command.json')
        self.records.append({'request': request, 'queued_monotonic_ns': time.monotonic_ns()})
        target = self.case / f'command-{self.sequence}.private.json'
        while time.monotonic() < deadline:
            if (self.case / 'result.private.json').exists():
                raise ValueError('The original case closed before its command was acknowledged.')
            if not cleanup:
                self.check()
            if target.exists():
                receipt = read_json(target)
                expected = (('WePlayNativeGraphicsConfigureWater' if getattr(self, 'profile', 'leaf-palette') == 'water-palette'
                             else 'WePlayNativeGraphicsConfigureEffects') if operation == 'variant'
                            else 'WePlayNativeGraphicsRequestCapture')
                if (receipt.get('succeeded') is not True or receipt.get('original_entry_guarded') is not True
                        or receipt.get('worker_code_rx') is not True or receipt.get('export') != expected
                        or receipt.get('dll_sha256') != self.files['build']['files'][guard.load_hook.DLL_NAME]):
                    raise ValueError('The guarded owner command did not succeed in the exact initialized DLL.')
                self.records[-1]['receipt'] = receipt
                return self.sequence
            failure = self.case / 'command-error.private.json'
            if failure.exists() and read_json(failure).get('sequence') == self.sequence:
                raise ValueError('The original owner refused this case command.')
            time.sleep(.05)
        raise TimeoutError('The original owner did not acknowledge within the batch deadline.')

    def variant(self, index, enabled, deadline, *, cleanup=False):
        profile = getattr(self, 'profile', 'leaf-palette')
        mask = 1 if profile == 'water-palette' else 4
        if index not in (0, 1, 2) or enabled not in (0, mask):
            raise ValueError('Only the three fixed programmes and their profile AUS/AN mask are supported.')
        sequence = self._queue('variant', deadline, variant=index, enabled=enabled, cleanup=cleanup)
        if profile == 'water-palette' and not cleanup:
            # The native capture gate waits for the queued water epoch. Its
            # exact captured programme/frame proof is checked before switching again.
            return sequence
        # A worker acknowledgement only queues native work; wait for real drawn state.
        while time.monotonic() < deadline:
            if not cleanup:
                self.check()
            if (self.case / 'result.private.json').exists():
                raise ValueError('The original case closed before the queued variant was drawn.')
            try:
                state = read_json(self.case / 'hook-state.private.json')
            except (ValueError, OSError):
                time.sleep(.05)
                continue
            if any(state.get(key) != 0 for key in FAULTS):
                raise ValueError('The native renderer reported a material fault.')
            if profile == 'water-palette':
                water = state.get('water')
                if (isinstance(water, dict) and water.get('pending') == 0 and water.get('config_version') == 2
                        and water.get('configured') == 1 and water.get('selected_variant') == index
                        and water.get('requested') == 0 and water.get('enabled') == 0
                        and type(water.get('epoch')) is int and water['epoch'] > 0
                        and all(water.get(key) == 0 for key in ('faulted', 'unsafe', 'set_failures',
                            'restore_failures', 'exceptions', 'pending_original_refs'))):
                    self.records[-1]['applied_off_state'] = state
                    return sequence
                time.sleep(.05)
                continue
            ready = (state.get('effects_pending') == 0 and state.get('effects_config_version') == 4
                and state.get('effects_selected_variant') == index and state.get('effects_active') == enabled
                and state.get('effects_drawn_active') == enabled
                and type(state.get('effects_revision')) is int and state['effects_revision'] > 0
                and state.get('effects_revision') == state.get('effects_drawn_revision'))
            if ready and (not enabled or state.get('effects_drawn_variant') == index):
                self.records[-1]['drawn_state'] = state
                return sequence
            time.sleep(.05)
        raise TimeoutError('The native variant did not reach a matching actual drawn epoch.')

    def capture(self, deadline):
        sequence = self._queue('capture', deadline)
        path = self.case / f'capture-{sequence}.json'
        while time.monotonic() < deadline:
            self.check()
            if path.exists():
                try:
                    meta = read_json(path)
                except (ValueError, OSError):
                    time.sleep(.05)
                    continue
                if meta.get('sequence') == sequence and meta.get('complete') is True:
                    return sequence, meta
            time.sleep(.05)
        raise TimeoutError('Native capture was not ready within the batch deadline.')

    def close(self):
        self.owner.close()


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)


def png(raw, roi=None):
    image = compare.bitmap(raw)
    x, y, width, height = roi or (0, 0, image.width, image.height)
    if min(x, y) < 0 or min(width, height) <= 0 or x + width > image.width or y + height > image.height:
        raise ValueError('The exact-pixel crop is outside the native capture.')
    rows = bytearray()
    for index in range(y, y + height):
        row = image.row(index)
        part = row[x * image.pixel_bytes:(x + width) * image.pixel_bytes]
        rgb = bytearray(width * 3)
        rgb[0::3] = part[2::image.pixel_bytes]
        rgb[1::3] = part[1::image.pixel_bytes]
        rgb[2::3] = part[0::image.pixel_bytes]
        rows.append(0)
        rows.extend(rgb)
    compressed = zlib.compress(rows, 3)
    if zlib.decompress(compressed) != rows:
        raise ValueError('PNG lossless verification failed.')
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', compressed) + chunk(b'IEND', b'')


def gallery(output, report, frames):
    """After timed capture batch: full source/output and fixed center crops, no resampling."""
    conversion_started = time.monotonic()
    cards = []
    artifacts = []
    for ordinal, frame in enumerate(frames):
        images = []
        for stage in ('source', 'output'):
            raw = guard._read(Path(frame['files'][stage]), compare.MAX_IMAGE)
            if hashlib.sha256(raw).hexdigest() != frame['meta'][stage + '_bmp_sha256']:
                raise ValueError('Native image hash changed before gallery conversion.')
            image = compare.bitmap(raw)
            if [image.width, image.height] != frame['meta'][stage]:
                raise ValueError('Image dimensions disagree with the native capture receipt.')
            width, height = min(640, image.width), min(360, image.height)
            roi = [(image.width - width) // 2, (image.height - height) // 2, width, height]
            for suffix, crop in (('voll', None), ('ausschnitt', roi)):
                name = f'{ordinal + 1}-{stage}-{suffix}.png'
                encoded = png(raw, crop)
                with (output / name).open('xb') as stream:
                    stream.write(encoded)
                artifacts.append({'path': name, 'sha256': hashlib.sha256(encoded).hexdigest(),
                    'native_bmp_sha256': frame['meta'][stage + '_bmp_sha256'], 'roi': crop,
                    'lossless_rgb_roundtrip_verified': True, 'resampled': False})
                images.append(f'<a href="{name}">{stage} {suffix}</a>')
            images.append(f'<img src="{ordinal + 1}-{stage}-ausschnitt.png" alt="{stage}: verlustfreier Ausschnitt">')
        cards.append(f'<article><h2>{html.escape(frame["label"])}</h2>'
                     f'<a href="{ordinal + 1}-output-voll.png"><img src="{ordinal + 1}-output-voll.png" alt="Vollständige Ausgabeaufnahme"></a>'
                     f'<p>{" · ".join(images[:2])}</p>'
                     + ''.join(images[2:]) + '</article>')
    table = ''.join(f'<tr><td>{html.escape(pin.get("label", pin["name"]))}</td><td><code>{pin["sha256"]}</code></td></tr>'
                    for pin in report.get('programs', []))
    heading = 'Drei Wasser-Shader – ein Szenenlauf' if report.get('profile') == 'water-palette' else 'Drei Shader – ein Szenenlauf'
    reference = ('<p>A verwendet das bereits abgenommene Wasser B. B und C sind neue Vergleichskandidaten. '
                 'Die Wasseranimation wird nicht eingefroren; die AUS-Rückkontrollen bleiben sichtbar.</p>'
                 if report.get('profile') == 'water-palette' else '')
    text = (f'<!doctype html><html lang="de"><meta charset="utf-8"><title>{heading}</title>'
            '<style>body{background:#111820;color:#eee;font:16px system-ui;margin:28px}article{border-top:1px solid #52606d;padding:20px 0}'
            'a{color:#9cd7ff}img{display:block;max-width:100%;margin:12px 0;image-rendering:auto}code{overflow-wrap:anywhere}</style>'
            f'<h1>{heading}</h1>{reference}<p>AUS / A / AUS / B / AUS / C / AUS. '
            'Die PNGs bewahren die RGB-Werte der nativen Aufnahmen. Vollansichten sind verlinkt; Ausschnitte werden nicht umgerechnet.</p>'
            f'<p>Batch einschließlich AUS-Abschluss: {report.get("batch_seconds", 0):.2f} s. Vergleich vollständig: {report.get("comparison_complete") is True}. '
            f'Abschließendes AUS bestätigt: {report.get("final_off_acknowledged") is True}. '
            'Dies ist keine automatische Qualitäts- oder Leistungsabnahme. Nicht gemessene Animation und Belichtung bleiben offen.</p>'
            '<p><a href="report.private.json">Prüfbericht</a> · <a href="gallery.private.json">Bildherkunft</a></p>'
            '<table><tr><th>Programm</th><th>SHA256</th></tr>' + table + '</table>' + ''.join(cards) + '</html>')
    with (output / 'Vergleich.html').open('x', encoding='utf8') as stream:
        stream.write(text)
    write_new(output / 'gallery.private.json', {'format':'s7-quick-gallery-v1', 'artifacts':artifacts,
        'conversion_seconds':time.monotonic() - conversion_started, 'conversion_in_batch_timing':False,
        'quality_assessed':False, 'performance_assessed':False})


def run(case, dll, output, *, seconds=45, profile='leaf-palette', session_factory=CaseSession, clock=time.monotonic):
    if profile not in PROFILES:
        raise ValueError('Unknown fixed comparison profile.')
    if type(seconds) is not int or not 30 <= seconds <= 60:
        raise ValueError('Use one bounded30–60second batch.')
    output = guard._lab(output)
    if output.exists():
        raise ValueError('Use a new private work/ output; preserve earlier comparisons.')
    started = clock()
    session = session_factory(case, dll) if profile == 'leaf-palette' else session_factory(case, dll, profile=profile)
    frames = []
    output_created = False
    report = {'format': f's7-quick-{profile}-v1', 'profile': profile,
              'comparison_complete': False, 'quality_assessed': False, 'performance_assessed': False, 'runtime_qualified': False,
              'load_preparation_in_batch': False, 'final_off_acknowledged': False, 'final_off_captured': False,
              'batch_limit_seconds': seconds, 'batch_seconds': 0, 'frames': frames, 'triplets': [], 'warmup': []}
    try:
        pins = session.pins if hasattr(session, 'pins') else palette_pins(session.case, profile)
        report['programs'] = pins
        initialized = read_json(session.case / 'hook-initialized.private.json')
        catalog = initialized.get('observer_catalog_sha256')
        if not re.fullmatch('[0-9a-f]{64}', str(catalog)):
            raise ValueError('The original same-frame observer catalog is not bound.')
        output.mkdir()
        output_created = True
        deadline = started + seconds
        capture_deadline = deadline
        mask = 1 if profile == 'water-palette' else 4
        if profile == 'leaf-palette':
            for index in range(3):
                session.variant(index, mask, capture_deadline)
                report['warmup'].append({'variant': index, 'owner_acknowledged': True,
                                         'matching_drawn_epoch_observed': True})
        else:
            report['warmup_mode'] = 'three_programs_created_on_first_safe_river_draw_each_ON_proven_by_capture'
        for index, (variant, requested) in enumerate(PLAN):
            enabled = mask if requested else 0
            if clock() >= capture_deadline:
                raise TimeoutError('Capture budget ended; only the bounded AUS cleanup remains.')
            session.variant(variant, enabled, capture_deadline)
            sequence, meta = session.capture(capture_deadline)
            capture_proof(meta, variant, enabled, pins[variant]['sha256'], catalog, profile)
            stale = False
            if frames and profile == 'water-palette':
                stale = meta['water_palette']['epoch'] <= frames[-1]['meta']['water_palette']['epoch']
            elif frames:
                stale = (meta['effects_revision'] <= frames[-1]['meta']['effects_revision']
                    or meta['leaf_palette']['module_epoch'] < frames[-1]['meta']['leaf_palette']['module_epoch'])
            if stale:
                raise ValueError('The captured host epoch is stale or the loaded palette module restarted.')
            label = 'AUS' if not enabled else VARIANTS[variant]
            files = {stage: str(session.case / f'capture-{sequence}-{stage}.bmp') for stage in ('source', 'output')}
            for stage, path in files.items():
                raw = guard._read(Path(path), compare.MAX_IMAGE)
                image = compare.bitmap(raw)
                if (hashlib.sha256(raw).hexdigest() != meta[stage + '_bmp_sha256']
                        or [image.width, image.height] != meta[stage]):
                    raise ValueError('Native image hash or dimensions disagree with the captured receipt.')
            frame = {'label': f'{index + 1}: {label}', 'variant': variant, 'enabled': enabled,
                     'sequence': sequence, 'meta': meta, 'files': files, 'captured_monotonic': clock()}
            frames.append(frame)
            write_new(output / f'{index + 1}-capture.private.json', frame)
            if index in (2, 4, 6):
                report['triplets'].append({'variant': frames[index - 1]['variant'],
                    **pair_proof(*(frames[i]['meta'] for i in (index - 2, index - 1, index)))})
        report['comparison_complete'] = True
        report['final_off_captured'] = True
    except (OSError, ValueError, RuntimeError, TimeoutError) as error:
        report['error'] = {'type': type(error).__name__, 'message': str(error)[:512]}
    finally:
        try:
            cleanup_started = clock()
            session.variant(0, 0, cleanup_started + 5, cleanup=True)
            report['final_off_acknowledged'] = True
        except (OSError, ValueError, RuntimeError, TimeoutError) as error:
            report['final_off_error'] = {'type': type(error).__name__, 'message': str(error)[:512]}
        try:
            session.check()
            report['final_heartbeat_verified'] = True
        except (OSError, ValueError, RuntimeError) as error:
            report['comparison_complete'] = False
            report['final_heartbeat_verified'] = False
            report['final_health_error'] = {'type': type(error).__name__, 'message': str(error)[:512]}
        report['commands'] = session.records
        session.close()
        report['batch_seconds'] = clock() - started
        report['capture_batch_seconds'] = cleanup_started - started
        report['cleanup_seconds'] = clock() - cleanup_started
        if output_created:
            write_new(output / 'report.private.json', report)
    if output_created:
        try:
            gallery(output, report, frames)
        except (OSError, ValueError) as error:
            write_new(output / 'gallery-error.private.json', {'type': type(error).__name__, 'message': str(error)[:512]})
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', type=Path, required=True)
    parser.add_argument('--dll', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--profile', choices=PROFILES, required=True)
    parser.add_argument('--seconds', type=int, default=45)
    args = parser.parse_args()
    try:
        result = run(args.case, args.dll, args.output, seconds=args.seconds, profile=args.profile)
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(2, f'Quick comparison refused: {error}\n')
    print(json.dumps({key:result.get(key)for key in ('comparison_complete','final_off_acknowledged',
        'final_off_captured','final_heartbeat_verified','batch_seconds','capture_batch_seconds','cleanup_seconds',
        'error','final_off_error','final_health_error')}, indent=2))
    return 0 if result['comparison_complete'] and result['final_off_acknowledged'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
