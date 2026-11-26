"""Run the COM presentation-hook fixture, independently of any game process."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

if __package__:
    from . import probe
else:
    import probe

NATIVE20_SOURCES = ('supersampling_hook.cpp', 'supersampling_hook.h', 'supersampling_hook_config.h',
           'supersampling_hook_capture.h', 'supersampling_hook_capture.cpp', 'supersampling_hook.def',
           'supersampling_hook_fixture.cpp', 'supersampling_probe.cpp', 'material_shader_observer.h',
           'material_shader_observer.cpp', 'graphics_observation.h', 'graphics_observation.cpp',
           'atmosphere_clock.h', 'atmosphere_fp.h')
EFFECTS_V1_SOURCES = NATIVE20_SOURCES + ('supersampling_hook_effects_contract.h', 'supersampling_hook_effects_draw.h',
                            'supersampling_hook_effects_gpu.cpp', 'material_shader_effects.h', 'material_shader_effects.cpp')
SOURCES = EFFECTS_V1_SOURCES + ('supersampling_hook_comparison_draw.h', 'material_comparison.h', 'material_comparison.cpp')
MODERN_SOURCES = SOURCES + ('native_effects_wire.h','material_shader_config_v2.h','material_shader_effects_contract_v2.h')
WATER_SOURCES = MODERN_SOURCES + ('water_shader_effects.h','water_shader_effects.cpp','native_material_draw.h')
MACRO_SOURCES = WATER_SOURCES + ('terrain_macro_guard.h',)
SHADOW_SOURCES = MACRO_SOURCES + ('shadow2048_filter_policy.h','shadow_filter_effects.h','shadow_filter_effects.cpp')
AMBIENT_SOURCES = SHADOW_SOURCES + ('up_draw_observer_protocol.h','ambient_occlusion_policy.h','ambient_occlusion_effects.h','ambient_occlusion_effects.cpp')
LEAF_PIXEL_SOURCES = AMBIENT_SOURCES + ('material_shader_config_v3.h','material_shader_effects_contract_v3.h')
PALETTE_SOURCES = LEAF_PIXEL_SOURCES + ('material_shader_config_v4.h','material_shader_effects_contract_v4.h')
WATER_PALETTE_SOURCES = PALETTE_SOURCES + ('water_shader_config_palette.h','water_shader_palette_contract.h','native_water_wire.h')
OVERLAY_SOURCES = WATER_PALETTE_SOURCES + ('shader_debug_overlay.h','shader_debug_overlay.cpp')
NATIVE20_BINARIES = ('WePlaySupersamplingLab.dll', 'supersampling_hook_fixture.exe')
BINARIES = NATIVE20_BINARIES + ('supersampling_effects_gpu.exe',)
BUILD_ABI = {'initialize_v1': 1048, 'initialize_v2': 3132, 'observation': 952, 'effects': 4132, 'capture': 12}
BUILD_ABI_V2 = {**BUILD_ABI,'effects_v2':6180}
BUILD_ABI_WATER = {**BUILD_ABI_V2,'water':1076}
BUILD_ABI_V3 = {**BUILD_ABI_WATER,'effects_v3':7204}
BUILD_ABI_V4 = {**BUILD_ABI_V3,'effects_v4':4120}
BUILD_ABI_WATER_PALETTE = {**BUILD_ABI_V4,'water_palette':3096}


def startup_receipt_valid(receipt):
    """Current fixture proof; retained historical receipts are not requalified."""
    return (type(receipt.get('reset_successes')) is int and receipt['reset_successes'] == 6
            and receipt.get('startup_clipped_size_preserved_before_geometry') is True
            and receipt.get('startup_input_unscaled') is True)


def verify_build(directory):
    directory = probe.plain(directory)
    manifest_path = probe.plain(directory / 'supersampling-hook-build.json')
    if manifest_path.stat().st_size > 8192:
        raise ValueError('Unbounded hook build manifest.')
    manifest = json.loads(manifest_path.read_text('utf-8'))
    current = manifest.get('format') == 's7-native-graphics-build-v2'
    if not current and manifest.get('format') != 's7-supersampling-hook-build-v1':
        raise ValueError('Unknown native build schema.')
    source_set = manifest.get('source_set')
    if current and source_set not in ('effects-v1', 'effects-v2','effects-v3','effects-v4','effects-v5','effects-v6','effects-v7','effects-v8','effects-v9','effects-v10','effects-v11','effects-v12'):
        raise ValueError('Unknown native source set.')
    sources = (OVERLAY_SOURCES if source_set=='effects-v12' else WATER_PALETTE_SOURCES if source_set=='effects-v11' else PALETTE_SOURCES if source_set=='effects-v10' else LEAF_PIXEL_SOURCES if source_set=='effects-v9' else AMBIENT_SOURCES if source_set in ('effects-v7','effects-v8') else SHADOW_SOURCES if source_set=='effects-v6' else MACRO_SOURCES if source_set=='effects-v5' else WATER_SOURCES if source_set=='effects-v4' else MODERN_SOURCES if source_set=='effects-v3' else SOURCES if source_set == 'effects-v2' else EFFECTS_V1_SOURCES) if current else NATIVE20_SOURCES
    binaries = BINARIES if current else NATIVE20_BINARIES
    expected = {
        'format': manifest['format'],
        'build_sha256': probe.sha256(probe.REPOSITORY / 'native/supersampling_hook_build.ps1'),
        'files': {name: probe.sha256(probe.REPOSITORY / 'native' / name) for name in sources},
    }
    if current:
        expected.update({'source_set': source_set, 'compiled_capabilities': 15, 'abi': BUILD_ABI_WATER_PALETTE if source_set in ('effects-v11','effects-v12') else BUILD_ABI_V4 if source_set=='effects-v10' else BUILD_ABI_V3 if source_set=='effects-v9' else BUILD_ABI_WATER if source_set in ('effects-v4','effects-v5','effects-v6','effects-v7','effects-v8') else BUILD_ABI_V2 if source_set=='effects-v3' else BUILD_ABI})
        if source_set in ('effects-v2','effects-v3','effects-v4','effects-v5','effects-v6','effects-v7','effects-v8','effects-v9','effects-v10','effects-v11','effects-v12'):
            expected['diagnostic_abi'] = {**({'overlay':12} if source_set=='effects-v12' else {}),'comparison':12,**({'shadow':12} if source_set in ('effects-v6','effects-v7','effects-v8','effects-v9','effects-v10','effects-v11','effects-v12') else {}),**({'ambient':12} if source_set in ('effects-v7','effects-v8','effects-v9','effects-v10','effects-v11','effects-v12') else {}),**({'ambient_v2':12} if source_set in ('effects-v8','effects-v9','effects-v10','effects-v11','effects-v12') else {})}
    expected['files'].update({name: probe.sha256(probe.plain(directory / name)) for name in binaries})
    if manifest != expected:
        raise ValueError('Stale or changed hook build; rebuild from current source.')
    return expected


def run(directory, runtime, output, *, catalog=None, catalog_sha256=None, shutdown='held-parent'):
    if os.name != 'nt':
        raise ValueError('The GPU hook fixture requires Windows.')
    directory, runtime, output = map(probe.plain, (directory, runtime, output))
    work = probe.plain(probe.REPOSITORY / 'work')
    if not directory.is_relative_to(work) or not output.is_relative_to(work) or output == work or output.exists():
        raise ValueError('Use a work/ build and a new output directory inside work/.')
    build = verify_build(directory)
    if probe.sha256(runtime) != probe.DXVK_SHA256:
        raise ValueError('Pinned DXVK 3.1.1 x86 is required.')
    output.mkdir()
    own_runtime = output / 'd3d9.dll'
    shutil.copyfile(runtime, own_runtime)
    if probe.sha256(own_runtime) != probe.DXVK_SHA256:
        raise ValueError('Copied runtime identity changed.')
    environment = os.environ.copy()
    for key in tuple(environment):
        if key.upper().startswith(('DXVK_', 'D3D9_', 'VK_', 'RESHADE_')):
            environment.pop(key)
    environment['DXVK_LOG_PATH'] = str(output)
    command = [str(directory / 'supersampling_hook_fixture.exe'), str(own_runtime), str(output / 'frames')]
    if catalog is not None or catalog_sha256 is not None:
        if catalog is None or catalog_sha256 is None:
            raise ValueError('Both catalog path and SHA256 are required for version-2 observation.')
        catalog = probe.plain(catalog)
        if not catalog.is_relative_to(work) or probe.sha256(catalog) != catalog_sha256:
            raise ValueError('The private observer catalog identity changed.')
        command.extend((str(catalog), catalog_sha256))
    if shutdown != 'held-parent':
        if shutdown not in ('last-parent', 'engine-deinit'):
            raise ValueError('Unknown owned shutdown fixture mode.')
        command.append('--' + shutdown)
    try:
        result = subprocess.run(command, env=environment, cwd=output, capture_output=True,
                                timeout=45, creationflags=subprocess.CREATE_NO_WINDOW)
    except subprocess.TimeoutExpired:
        (output / 'failure.json').write_text(json.dumps({'reason': 'owned_fixture_timeout_45s'}), encoding='utf-8')
        raise RuntimeError('Owned synthetic hook fixture exceeded its 45-second limit.') from None
    (output / 'stdout.txt').write_bytes(result.stdout)
    (output / 'stderr.txt').write_bytes(result.stderr)
    if result.returncode:
        raise RuntimeError(f'COM hook GPU fixture failed ({result.returncode}); see {output}.')
    receipt = json.loads(result.stdout)
    if (receipt.get('format') != 's7-supersampling-hook-fixture-v1'
            or receipt.get('game_tested') is not False or receipt.get('hud_input_tested') is not False
            or receipt.get('output_present_calls') != 4 or not startup_receipt_valid(receipt)
            or receipt.get('fallback_calls') != 2 or receipt.get('hook_detached') != 1
            or receipt.get('owned_window_resize_applied') != 1
            or receipt.get('logical_resize_preserved_high_scene') is not True
            or receipt.get('logical_context_guards_passed') is not True
            or receipt.get('logical_point_and_mouse_roundtrip_passed') is not True
            or receipt.get('cache_reuse_passed') is not True
            or receipt.get('window_shutdown_cleanup_passed') is not True
            or receipt.get('failed_filtered_capture_recorded') is not True
            or receipt.get('original_destroy_device_usable') is not True
            or receipt.get('engine_deinit_rejections') != 2
            or receipt.get('authored_marker_gpu_magenta') is not True):
        raise RuntimeError('Incomplete COM hook fixture receipt.')
    for sequence, should_fallback in ((1, False), (2, True)):
        capture = json.loads((output / 'frames' / f'capture-{sequence}.json').read_text('utf-8'))
        if (capture.get('complete') is not True or capture.get('status') != 0
                or capture.get('fallback_used') is not should_fallback
                or capture.get('filtered_api_succeeded') is not (not should_fallback)
                or capture.get('presentation_path') != ('fallback_original' if should_fallback else 'filtered_output')
                or capture.get('final_present_hresult') != 0
                or type(capture.get('filtered_present_hresult')) is not int
                or (capture.get('filtered_present_hresult') == 0) is should_fallback):
            raise RuntimeError('Capture conflated filtered presentation with the final fallback result.')
        for role in ('source', 'output'):
            if probe.sha256(output / 'frames' / f'capture-{sequence}-{role}.bmp') != capture.get(f'{role}_bmp_sha256'):
                raise RuntimeError('Captured surface bitmap identity changed.')
    receipt['renderer_sha256'] = probe.DXVK_SHA256
    receipt['build'] = build
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--dxvk', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--catalog', type=Path)
    parser.add_argument('--catalog-sha256')
    parser.add_argument('--shutdown', choices=('held-parent', 'last-parent', 'engine-deinit'), default='held-parent')
    args = parser.parse_args()
    try:
        receipt = run(args.build, args.dxvk, args.output, catalog=args.catalog, catalog_sha256=args.catalog_sha256, shutdown=args.shutdown)
    except (ValueError, OSError, RuntimeError, json.JSONDecodeError) as error:
        parser.exit(1, f'{error}\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
