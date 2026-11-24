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

SOURCES = ('supersampling_hook.cpp', 'supersampling_hook.h', 'supersampling_hook.def',
           'supersampling_hook_fixture.cpp', 'supersampling_probe.cpp')
BINARIES = ('WePlaySupersamplingLab.dll', 'supersampling_hook_fixture.exe')


def verify_build(directory):
    directory = probe.plain(directory)
    manifest_path = probe.plain(directory / 'supersampling-hook-build.json')
    if manifest_path.stat().st_size > 4096:
        raise ValueError('Unbounded hook build manifest.')
    manifest = json.loads(manifest_path.read_text('utf-8'))
    expected = {
        'format': 's7-supersampling-hook-build-v1',
        'build_sha256': probe.sha256(probe.REPOSITORY / 'native/supersampling_hook_build.ps1'),
        'files': {name: probe.sha256(probe.REPOSITORY / 'native' / name) for name in SOURCES},
    }
    expected['files'].update({name: probe.sha256(probe.plain(directory / name)) for name in BINARIES})
    if manifest != expected:
        raise ValueError('Stale or changed hook build; rebuild from current source.')
    return expected


def run(directory, runtime, output):
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
            or receipt.get('output_present_calls') != 3 or receipt.get('reset_successes') != 4
            or receipt.get('fallback_calls') != 1 or receipt.get('hook_detached') != 1
            or receipt.get('owned_window_resize_applied') != 1
            or receipt.get('logical_resize_preserved_high_scene') is not True
            or receipt.get('logical_context_guards_passed') is not True
            or receipt.get('logical_point_and_mouse_roundtrip_passed') is not True
            or receipt.get('authored_marker_gpu_magenta') is not True):
        raise RuntimeError('Incomplete COM hook fixture receipt.')
    receipt['renderer_sha256'] = probe.DXVK_SHA256
    receipt['build'] = build
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--dxvk', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    try:
        receipt = run(args.build, args.dxvk, args.output)
    except (ValueError, OSError, RuntimeError, json.JSONDecodeError) as error:
        parser.exit(1, f'{error}\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
