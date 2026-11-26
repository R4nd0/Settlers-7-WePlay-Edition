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


def validate_mode(manifest, material_version, catalog=None, catalog_sha256=None):
    if type(material_version) is not int or material_version not in (1, 3):
        raise ValueError('Select explicit material version 1 or 3.')
    if material_version == 3:
        if catalog is not None or catalog_sha256 is not None:
            raise ValueError('V3 authored fixture is isolated from diagnostic comparison/catalog profiles.')
        if manifest.get('source_set') != 'effects-v9' or manifest.get('abi', {}).get('effects_v3') != 7204:
            raise ValueError('Explicit source38 effects-v9 / effects_v3:7204 build required.')


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


def run(build, runtime, output, *, catalog=None, catalog_sha256=None, material_version=1):
    if os.name != 'nt':
        raise ValueError('Windows is required.')
    build, runtime, output = map(probe.plain, (build, runtime, output))
    work = probe.plain(probe.REPOSITORY / 'work')
    if not build.is_relative_to(work) or not output.is_relative_to(work) or output == work or output.exists():
        raise ValueError('Use an owned work/ build and a new work/ output directory.')
    manifest = hook_probe.verify_build(build)
    if manifest.get('format') != 's7-native-graphics-build-v2':
        raise ValueError('The explicit effects build is required.')
    validate_mode(manifest, material_version, catalog, catalog_sha256)
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
    if material_version == 3:
        command.append('--v3')
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
    parser.add_argument('--material-version', type=int, choices=(1, 3), default=1)
    args = parser.parse_args()
    try:
        print(json.dumps(run(args.build, args.dxvk, args.output, catalog=args.catalog,
                             catalog_sha256=args.catalog_sha256, material_version=args.material_version), indent=2))
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
