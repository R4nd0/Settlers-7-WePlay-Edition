"""Bounded authored-program GPU transaction test; never starts a game."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

if __package__:
    from . import hook_probe, probe
else:
    import hook_probe
    import probe


def validate_mode(manifest, material_version, catalog=None, catalog_sha256=None, water_palette=False, shader_overlay=False):
    if type(water_palette) is not bool or type(shader_overlay) is not bool or shader_overlay and not water_palette:
        raise ValueError('Shader overlay requires the explicit water-palette fixture.')
    if water_palette:
        if (type(material_version) is not int or material_version != 1 or catalog is not None or catalog_sha256 is not None
                or manifest.get('source_set') not in ('effects-v11', 'effects-v12') or manifest.get('abi', {}).get('water') != 1076
                or manifest.get('abi', {}).get('water_palette') != 3096
                or shader_overlay and (manifest.get('source_set') != 'effects-v12' or manifest.get('diagnostic_abi', {}).get('overlay') != 12)):
            raise ValueError('Isolated source43 water-palette3096 build required; old1076 must remain available.')
        return
    if type(material_version) is not int or material_version not in (1, 3, 4):
        raise ValueError('Select explicit material version 1, 3 or 4.')
    if material_version in (3, 4):
        if catalog is not None or catalog_sha256 is not None:
            raise ValueError('V3 authored fixture is isolated from diagnostic comparison/catalog profiles.')
        if material_version == 3 and (manifest.get('source_set') not in ('effects-v9', 'effects-v10', 'effects-v11', 'effects-v12') or manifest.get('abi', {}).get('effects_v3') != 7204):
            raise ValueError('Explicit source38 effects-v9 / effects_v3:7204 build required.')
        if material_version == 4 and (manifest.get('source_set') not in ('effects-v10', 'effects-v11', 'effects-v12') or manifest.get('abi', {}).get('effects_v4') != 4120):
            raise ValueError('Explicit source40 effects-v10 / effects_v4:4120 build required.')


def validate_v3_receipt(receipt, epoch):
    fields = ('authored_programs', 'public_off_on_off', 'pixel_replacement_verified', 'same_frame_capture',
              'failed_draw_restored', 'reset_recreated_program', 'warm_program_reused')
    if (receipt.get('format') != 's7-material-effects-v3-gpu-v1' or receipt.get('game_tested') is not False
            or receipt.get('wire_bytes') != 7204 or any(receipt.get(name) is not True for name in fields)
            or type(receipt.get('leaf_applied')) is not int or receipt['leaf_applied'] < 10
            or receipt.get('restored_draws') != receipt['leaf_applied']):
        raise RuntimeError('Incomplete explicit V3 activation/rollback evidence.')
    zero_fields = ('status', 'effects_faulted', 'effects_unsafe', 'effects_module_faulted',
                   'effects_module_shader_failures', 'effects_module_set_failures', 'effects_module_restore_failures')
    if (epoch.get('format') != 's7-native-frame-capture-v1' or epoch.get('sequence') != 31
            or epoch.get('complete') is not True or any(epoch.get(name) != 0 for name in zero_fields)
            or epoch.get('effects_config_version') != 3 or epoch.get('effects_active') != 4
            or epoch.get('effects_drawn_active') != 4 or epoch.get('effects_frame_has_geometry') != 1
            or type(epoch.get('effects_revision')) is not int or epoch['effects_revision'] < 1
            or epoch.get('effects_drawn_revision') != epoch['effects_revision']
            or type(epoch.get('frame')) is not int or epoch['frame'] < 1
            or epoch.get('source') != [1280, 720] or epoch.get('output') != [640, 360]
            or epoch.get('client') != [640, 360] or epoch.get('viewport') != [1280, 720]
            or epoch.get('selected_target') != [1280, 720]
            or epoch.get('presentation_path') != 'filtered_output' or epoch.get('fallback_used') is not False
            or epoch.get('filtered_api_succeeded') is not True
            or epoch.get('filtered_present_hresult') != 0 or epoch.get('final_present_hresult') != 0):
        raise RuntimeError('V3 capture31 did not prove fresh successful filtered geometry of its exact epoch.')


def validate_v4_receipt(receipt, captures):
    flags = ('authored_programs', 'three_program_colors', 'first_match_creates_three', 'safe_live_selection',
             'off_retains_warm_palette', 'same_frame_captures', 'stale_epoch_deferred', 'failed_original_restored',
             'reset_recreated_palette')
    hashes = receipt.get('program_sha256')
    if (receipt.get('format') != 's7-material-effects-v4-gpu-v1' or receipt.get('game_tested') is not False
            or receipt.get('wire_bytes') != 4120 or any(receipt.get(key) is not True for key in flags)
            or not isinstance(hashes, list) or len(hashes) != 3
            or any(not isinstance(value, str) or len(value) != 64 or any(c not in '0123456789abcdef' for c in value) for value in hashes)
            or len(set(hashes)) != 3):
        raise RuntimeError('Incomplete authored three-program palette proof.')
    applied, restored = receipt.get('variant_applications'), receipt.get('variant_restorations')
    if (not isinstance(applied, list) or len(applied) != 3 or applied != restored
            or any(type(value) is not int or value <= 0 for value in applied) or len(captures) != 4):
        raise RuntimeError('Each actual palette program must be applied and restored.')
    previous_revision = 0
    for index, epoch in enumerate(captures):
        off = index == 3
        selected = 2 if off else index
        sequence = 60 if off else 50 + index
        module = epoch.get('leaf_palette') or {}
        if (epoch.get('format') != 's7-native-frame-capture-v1' or epoch.get('complete') is not True or epoch.get('status') != 0
                or epoch.get('sequence') != sequence or epoch.get('effects_config_version') != 4
                or epoch.get('effects_selected_variant') != selected
                or epoch.get('effects_active') != (0 if off else 4) or epoch.get('effects_drawn_active') != (0 if off else 4)
                or epoch.get('effects_drawn_variant') != (None if off else selected)
                or type(epoch.get('frame')) is not int or epoch['frame'] <= 0 or epoch.get('effects_drawn_frame') != epoch['frame']
                or type(epoch.get('effects_revision')) is not int or epoch['effects_revision'] <= previous_revision
                or epoch.get('effects_drawn_revision') != epoch['effects_revision'] or epoch.get('effects_frame_has_geometry') != 1
                or any(epoch.get(key) != 0 for key in ('effects_faulted', 'effects_unsafe', 'effects_module_faulted',
                           'effects_module_shader_failures', 'effects_module_set_failures', 'effects_module_restore_failures', 'effects_variant_frame_failed_draws'))
                or epoch.get('presentation_path') != 'filtered_output' or epoch.get('fallback_used') is not False
                or epoch.get('filtered_api_succeeded') is not True or epoch.get('filtered_present_hresult') != 0
                or epoch.get('source') != [1280, 720] or epoch.get('output') != [640, 360] or epoch.get('client') != [640, 360]
                or module.get('selected_variant') != selected or module.get('drawn_variant') != (None if off else selected)
                or module.get('drawn_frame') != epoch['frame'] or module.get('configured_program_sha256') != hashes[selected]
                or module.get('drawn_program_sha256') != (None if off else hashes[selected])
                or type(module.get('module_epoch')) is not int or module['module_epoch'] <= 0
                or any(module.get(key) != 0 for key in ('faulted', 'shader_failures', 'set_failures', 'restore_failures',
                           'exceptions', 'active_transactions', 'active_original_refs'))):
            raise RuntimeError('Palette capture does not bind the actual program to its fresh successful frame.')
        frame_applied = epoch.get('effects_variant_frame_applications')
        if (type(frame_applied) is not int or (frame_applied != 0 if off else frame_applied < 1)
                or epoch.get('effects_variant_frame_restorations') != frame_applied
                or module.get('frame_applications') != frame_applied or module.get('frame_restorations') != frame_applied
                or module.get('frame_failed_draws') != 0):
            raise RuntimeError('Palette frame applications/restorations are incomplete.')
        for name in ('variant_applications', 'variant_restorations', 'variant_successful_draws', 'variant_failed_draws', 'program_creations'):
            values = module.get(name)
            if not isinstance(values, list) or len(values) != 3 or any(type(v) is not int or v < 0 for v in values):
                raise RuntimeError('Bounded per-program evidence is missing.')
        if (module['variant_applications'] != module['variant_restorations']
                or any(v <= 0 for v in module['program_creations'])
                or not off and (module['variant_applications'][selected] <= 0 or module['variant_successful_draws'][selected] <= 0
                                or any(module['variant_failed_draws']))):
            raise RuntimeError('Actual selected-program activation/rollback counters do not qualify.')
        previous_revision = epoch['effects_revision']


def validate_water_palette_receipt(receipt, captures):
    flags = ('authored_programs', 'public_typed_queue', 'immediate_capture_deferred_safely', 'three_program_colors',
             'first_draw_creates_three', 'off_selection_cache_reused', 'failed_original_restored', 'actual_reset',
             'legacy_VS68_69_unchanged')
    hashes = receipt.get('program_sha256')
    if (receipt.get('format') != 's7-water-palette-gpu-v1' or receipt.get('game_tested') is not False
            or receipt.get('wire_bytes') != 3096 or any(receipt.get(key) is not True for key in flags)
            or type(receipt.get('frame_origin')) is not int or receipt['frame_origin'] < 4
            or not isinstance(hashes, list) or len(hashes) != 3
            or any(not isinstance(v, str) or len(v) != 64 or any(c not in '0123456789abcdef' for c in v) for v in hashes)
            or len(set(hashes)) != 3
            or len(captures) != 5):
        raise RuntimeError('Incomplete authored water-palette proof.')
    previous_epoch = 0
    for position, epoch in enumerate(captures):
        off = position in (0, 4)
        selected = 0 if position == 0 else 2 if position == 4 else position - 1
        seq = 70 if position == 0 else 100 if position == 4 else 79 + position
        value = epoch.get('water_palette') or {}
        legacy = epoch.get('water') or {}
        if (epoch.get('format') != 's7-native-frame-capture-v1' or epoch.get('sequence') != seq
                or epoch.get('complete') is not True or epoch.get('status') != 0
                or epoch.get('effects_active') != 0 or epoch.get('effects_config_version') != 1
                or epoch.get('effects_configured') != 1 or epoch.get('effects_frame_has_geometry') != 1
                or epoch.get('effects_faulted') != 0 or epoch.get('effects_unsafe') != 0
                or epoch.get('presentation_path') != 'filtered_output' or epoch.get('fallback_used') is not False
                or epoch.get('filtered_api_succeeded') is not True or epoch.get('filtered_present_hresult') != 0
                or epoch.get('source') != [1280, 720] or epoch.get('output') != [640, 360] or epoch.get('client') != [640, 360]
                or type(epoch.get('frame')) is not int or epoch['frame'] <= receipt['frame_origin']
                or value.get('config_version') != 2 or value.get('selected_variant') != selected
                or value.get('drawn_variant') != (None if off else selected) or value.get('frame') != epoch['frame']
                or value.get('configured_program_sha256') != hashes[selected]
                or value.get('drawn_program_sha256') != (None if off else hashes[selected])
                or type(value.get('epoch')) is not int or value['epoch'] <= previous_epoch
                or legacy.get('frame') != epoch['frame'] or legacy.get('epoch') != value['epoch']
                or legacy.get('configured') != 1 or legacy.get('requested') != (0 if off else 1)
                or legacy.get('enabled') != (0 if off else 1)
                or any(value.get(key) != 0 for key in ('faulted', 'unsafe', 'shader_failures', 'set_failures',
                    'restore_failures', 'exceptions', 'active_transactions', 'pending_original_refs', 'cached_refs', 'completed_failed_draws'))):
            raise RuntimeError('Water capture did not prove the actual selected shader/epoch/frame.')
        apps = value.get('completed_applications')
        if (type(apps) is not int or (apps != 0 if off else apps < 1)
                or value.get('completed_restorations') != apps or value.get('completed_successful_draws') != apps
                or legacy.get('completed_applications') != apps or legacy.get('completed_restorations') != apps):
            raise RuntimeError('Water capture lacks exact successful/restored applications.')
        for name in ('variant_applications', 'variant_restorations', 'variant_successful_draws', 'variant_failed_draws', 'program_creations'):
            items = value.get(name)
            if not isinstance(items, list) or len(items) != 3 or any(type(v) is not int or v < 0 for v in items):
                raise RuntimeError('Water per-program evidence is missing.')
        if (value['variant_applications'] != value['variant_restorations'] or not off and
                (value['variant_successful_draws'][selected] < 1 or any(v < 1 for v in value['program_creations'])
                 or any(value['variant_failed_draws']))):
            raise RuntimeError('Water per-program activation/rollback proof failed.')
        previous_epoch = value['epoch']


def validate_overlay_receipt(receipt):
    overlay = receipt.get('shader_overlay')
    flags = ('outside_control_unchanged', 'original_state_restored', 'safe_boundary_only',
             'physical_input_before_scaling', 'actual_selected_program_pixels', 'reset_closed_cleanly')
    if (not isinstance(overlay, dict) or overlay.get('wire_bytes') != 12
            or any(overlay.get(key) is not True for key in flags)
            or type(overlay.get('unsafe')) is not int or overlay['unsafe'] != 0
            or type(overlay.get('actual_wndproc_choices')) is not int or overlay['actual_wndproc_choices'] != 4):
        raise RuntimeError('Overlay lacks actual safe native window-input and rollback proof.')
    for key, minimum in (('physical_panel_pixels', 20001), ('font_pixels', 11), ('overlay_draws', 8), ('input_events', 8)):
        if type(overlay.get(key)) is not int or overlay[key] < minimum:
            raise RuntimeError('Overlay lacks actual physical pixels, text or four native input choices.')
    epochs = overlay.get('epochs')
    if (not isinstance(epochs, list) or len(epochs) != 4 or any(type(v) is not int or v < 1 for v in epochs)
            or any(b <= a for a, b in zip(epochs, epochs[1:]))):
        raise RuntimeError('All four overlay choices must advance actual configuration epochs.')


def run(build, runtime, output, *, catalog=None, catalog_sha256=None, material_version=1, water_palette=False, shader_overlay=False):
    if os.name != 'nt':
        raise ValueError('Windows is required.')
    build, runtime, output = map(probe.plain, (build, runtime, output))
    work = probe.plain(probe.REPOSITORY / 'work')
    if not build.is_relative_to(work) or not output.is_relative_to(work) or output == work or output.exists():
        raise ValueError('Use an owned work/ build and a new work/ output directory.')
    manifest = hook_probe.verify_build(build)
    if manifest.get('format') != 's7-native-graphics-build-v2':
        raise ValueError('The explicit effects build is required.')
    validate_mode(manifest, material_version, catalog, catalog_sha256, water_palette, shader_overlay)
    if probe.sha256(runtime) != probe.DXVK_SHA256:
        raise ValueError('Pinned DXVK 3.1.1 x86 is required.')
    memory = ctypes.create_string_buffer(64)
    struct.pack_into('<I', memory, 0, 64)
    available = ctypes.WinDLL('kernel32', use_last_error=True).GlobalMemoryStatusEx(ctypes.byref(memory))
    if not available or struct.unpack_from('<Q', memory, 16)[0] < 1024**3:
        raise RuntimeError('Insufficient physical headroom for the small synthetic GPU host (1 GiB minimum).')
    output.mkdir()
    owned_runtime = output / 'd3d9.dll'
    shutil.copyfile(runtime, owned_runtime)
    if probe.sha256(owned_runtime) != probe.DXVK_SHA256:
        raise ValueError('Copied runtime identity changed.')
    environment = os.environ.copy()
    for key in tuple(environment):
        if key.upper().startswith(('DXVK_', 'D3D9_', 'VK_', 'RESHADE_')):
            environment.pop(key)
    environment['DXVK_LOG_PATH'] = str(output)
    command = [str(build / 'supersampling_effects_gpu.exe'), str(owned_runtime), str(output / 'frames')]
    if material_version in (3, 4):
        command.append(f'--v{material_version}')
    if water_palette:
        command.append('--water-palette')
    if shader_overlay:
        command.append('--shader-overlay')
    compare = catalog is not None or catalog_sha256 is not None
    if compare:
        if catalog is None or catalog_sha256 is None:
            raise ValueError('Both catalog and SHA256 are required for diagnostic comparison.')
        catalog = probe.plain(catalog)
        if not catalog.is_relative_to(work) or probe.sha256(catalog) != catalog_sha256:
            raise ValueError('Observer catalog identity changed.')
        command.extend((str(catalog), catalog_sha256))
    try:
        completed = subprocess.run(command, cwd=output, env=environment, capture_output=True,
                                   timeout=45, creationflags=subprocess.CREATE_NO_WINDOW)
    except subprocess.TimeoutExpired:
        (output / 'failure.json').write_text(json.dumps({'reason': 'owned_effects_fixture_timeout_45s'}), encoding='utf-8')
        raise RuntimeError('Owned GPU transaction fixture exceeded 45 seconds.') from None
    (output / 'stdout.txt').write_bytes(completed.stdout)
    (output / 'stderr.txt').write_bytes(completed.stderr)
    if completed.returncode:
        raise RuntimeError(f'Authored GPU effects fixture failed ({completed.returncode}); see {output}.')
    receipt = json.loads(completed.stdout)
    if water_palette:
        sequences = (70, 80, 81, 82, 100)
        captures = [json.loads((output / f'frames/capture-{seq}.json').read_text('utf-8')) for seq in sequences]
        validate_water_palette_receipt(receipt, captures)
        if shader_overlay:
            validate_overlay_receipt(receipt)
        for sequence, epoch in zip(sequences, captures):
            for kind in ('source', 'output'):
                if probe.sha256(output / f'frames/capture-{sequence}-{kind}.bmp') != epoch.get(f'{kind}_bmp_sha256'):
                    raise RuntimeError('Water captured buffer hash changed.')
        if (output / 'frames/capture-70-source.bmp').read_bytes() != (output / 'frames/capture-100-source.bmp').read_bytes():
            raise RuntimeError('Original water OFF pixels were not restored exactly.')
        receipt.update(renderer_sha256=probe.DXVK_SHA256, build=manifest)
        (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
        return receipt
    if material_version == 4:
        captures = [json.loads((output / f'frames/capture-{sequence}.json').read_text('utf-8')) for sequence in (50, 51, 52, 60)]
        validate_v4_receipt(receipt, captures)
        for sequence, epoch in zip((50, 51, 52, 60), captures):
            for kind in ('source', 'output'):
                if probe.sha256(output / f'frames/capture-{sequence}-{kind}.bmp') != epoch.get(f'{kind}_bmp_sha256'):
                    raise RuntimeError('Palette captured buffer hash changed.')
        receipt['renderer_sha256'] = probe.DXVK_SHA256
        receipt['build'] = manifest
        (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
        return receipt
    if material_version == 3:
        epoch = json.loads((output / 'frames/capture-31.json').read_text('utf-8'))
        validate_v3_receipt(receipt, epoch)
        for kind in ('source', 'output'):
            if probe.sha256(output / f'frames/capture-31-{kind}.bmp') != epoch.get(f'{kind}_bmp_sha256'):
                raise RuntimeError('V3 captured buffer hash changed.')
        receipt['renderer_sha256'] = probe.DXVK_SHA256
        receipt['build'] = manifest
        (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
        return receipt
    fields = ('authored_programs', 'real_vertex_pixel_creation', 'river_gpu_pixels', 'leaf_gpu_replacement_pixels',
              'bitwise_state_restored', 'failed_original_restored', 'same_process_disable_enable', 'reset_reenable',
              'fresh_draw_epoch_required', 'module_fault_disables_to_original', 'warm_generated_program_cache_reused')
    if (receipt.get('format') != 's7-material-effects-gpu-v1' or receipt.get('game_tested') is not False
            or any(receipt.get(name) is not True for name in fields)):
        raise RuntimeError('Incomplete GPU activation/rollback evidence.')
    epoch = json.loads((output / 'frames/capture-1.json').read_text('utf-8'))
    if (epoch.get('effects_frame_has_geometry') != 1 or epoch.get('effects_faulted') != 0
            or epoch.get('effects_drawn_revision') != epoch.get('effects_revision')
            or epoch.get('effects_drawn_active') != epoch.get('effects_active')):
        raise RuntimeError('Captured effects epoch was not latched to fresh successful geometry.')
    if compare:
        captured = json.loads((output / 'frames/capture-10.json').read_text('utf-8'))
        state = captured.get('comparison') or {}
        if (receipt.get('comparison_gpu_enabled') is not True or receipt.get('comparison_saturated_wrong_hash_programs') != 129
                or state.get('frame') != captured.get('frame')
                or state.get('requested') != 1 or state.get('active') != 1 or state.get('pinned') != 1
                or state.get('faulted') != 0 or state.get('unsafe') != 0
                or not state.get('completed_frame_applications')
                or state.get('completed_frame_applications') != state.get('completed_frame_restorations')):
            raise RuntimeError('Comparison pin was not applied and restored in the captured GPU frame.')
    receipt['renderer_sha256'] = probe.DXVK_SHA256
    receipt['build'] = manifest
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--dxvk', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--catalog', type=Path)
    parser.add_argument('--catalog-sha256')
    parser.add_argument('--material-version', type=int, choices=(1, 3, 4), default=1)
    parser.add_argument('--water-palette', action='store_true')
    parser.add_argument('--shader-overlay', action='store_true')
    args = parser.parse_args()
    try:
        print(json.dumps(run(args.build, args.dxvk, args.output, catalog=args.catalog,
                             catalog_sha256=args.catalog_sha256, material_version=args.material_version,
                             water_palette=args.water_palette, shader_overlay=args.shader_overlay), indent=2))
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
