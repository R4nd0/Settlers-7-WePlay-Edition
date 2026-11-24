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


def run(build, runtime, output):
    if os.name != 'nt':
        raise ValueError('Windows is required.')
    build, runtime, output = map(probe.plain, (build, runtime, output))
    work = probe.plain(probe.REPOSITORY / 'work')
    if not build.is_relative_to(work) or not output.is_relative_to(work) or output == work or output.exists():
        raise ValueError('Use an owned work/ build and a new work/ output directory.')
    manifest = hook_probe.verify_build(build)
    if manifest.get('format') != 's7-native-graphics-build-v2':
        raise ValueError('The explicit effects build is required.')
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
    receipt['renderer_sha256'] = probe.DXVK_SHA256
    receipt['build'] = manifest
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--dxvk', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(run(args.build, args.dxvk, args.output), indent=2))
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
