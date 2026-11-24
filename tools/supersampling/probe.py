"""Bounded standalone DXVK GPU downsampling test; never launches a game."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

DXVK_SHA256 = '265888c31ca78dffa290c39cb7e50bfb02762590e41927906e46fb32f01497fa'
REPOSITORY = Path(__file__).resolve().parents[2]


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def plain(path):
    value = Path(path).absolute()
    for item in (value, *value.parents):
        if item.is_symlink() or (hasattr(item, 'is_junction') and item.is_junction()):
            raise ValueError('Reparse paths are not accepted for this experiment.')
    return value.resolve()


def verify_probe(probe):
    if probe.name != 'supersampling_probe.exe' or not probe.is_file():
        raise ValueError('Use the named standalone supersampling_probe.exe.')
    manifest_path = plain(probe.with_name('supersampling-build.json'))
    if manifest_path.stat().st_size > 4096:
        raise ValueError('Unbounded probe build manifest.')
    manifest = json.loads(manifest_path.read_text('utf-8'))
    expected = {
        'format': 's7-supersampling-build-v1',
        'probe_sha256': sha256(probe),
        'source_sha256': sha256(REPOSITORY / 'native/supersampling_probe.cpp'),
        'build_sha256': sha256(REPOSITORY / 'native/supersampling_build.ps1'),
    }
    if manifest != expected:
        raise ValueError('Stale or changed GPU probe; rebuild from the current source.')
    return expected


def run(probe, dxvk, output, *, show=False):
    if os.name != 'nt':
        raise ValueError('The GPU probe requires Windows.')
    probe, dxvk, output = map(plain, (probe, dxvk, output))
    work = plain(REPOSITORY / 'work')
    if not output.is_relative_to(work) or output == work or output.exists():
        raise ValueError('Use a new output directory inside repository work/.')
    if not probe.is_relative_to(work):
        raise ValueError('Use the standalone probe built inside repository work/.')
    build = verify_probe(probe)
    if sha256(dxvk) != DXVK_SHA256:
        raise ValueError('The exact pinned DXVK 3.1.1 x86 D3D9 DLL is required.')
    output.mkdir(parents=False)
    # Retain/load our own exact copy, not a DLL belonging to a reserved game.
    runtime = output / 'd3d9.dll'
    shutil.copyfile(dxvk, runtime)
    if sha256(runtime) != DXVK_SHA256:
        raise ValueError('Copied renderer identity changed.')
    environment = os.environ.copy()
    # Do not inherit unrelated ReShade/Vulkan add-ons or DXVK overrides.
    for key in tuple(environment):
        if key.upper().startswith(('DXVK_', 'D3D9_', 'VK_', 'RESHADE_')):
            environment.pop(key)
    environment['DXVK_LOG_PATH'] = str(output)
    command = [str(probe), str(runtime), str(output / 'frames')]
    if show:
        command.append('--show')
    executable_hash = build['probe_sha256']
    try:
        result = subprocess.run(command, cwd=output, env=environment,
                                capture_output=True, timeout=45,
                                creationflags=subprocess.CREATE_NO_WINDOW)
    except subprocess.TimeoutExpired:
        (output / 'failure.json').write_text(json.dumps({
            'format': 's7-supersampling-gpu-run-v1', 'game_tested': False,
            'passed': False, 'reason': 'owned_probe_timeout_45_seconds',
            'probe_sha256': executable_hash}), encoding='utf-8')
        raise RuntimeError('The owned synthetic GPU probe exceeded its 45-second limit.') from None
    (output / 'stdout.txt').write_bytes(result.stdout)
    (output / 'stderr.txt').write_bytes(result.stderr)
    if result.returncode:
        raise RuntimeError(f'Synthetic GPU probe failed ({result.returncode}); see {output}.')
    receipt = json.loads(result.stdout)
    if (receipt.get('format') != 's7-supersampling-gpu-probe-v1'
            or receipt.get('renderer_sha256') != DXVK_SHA256
            or receipt.get('game_tested') is not False
            or receipt.get('resize_reset_passed') is not True
            or receipt.get('probe_completed') is not True
            or len(receipt.get('cases', [])) != 6
            or any(case.get('preserved_corners') != 4
                   or (case.get('downsample_path') == 'explicit_stretchrect'
                       and case.get('client_capture_verified') is not True)
                   for case in receipt['cases'])):
        raise RuntimeError('Incomplete synthetic GPU receipt.')
    receipt['probe_sha256'] = executable_hash
    receipt['source_sha256'] = build['source_sha256']
    receipt['build_sha256'] = build['build_sha256']
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--dxvk', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--show', action='store_true', help='Keep the synthetic preview visible for five seconds.')
    args = parser.parse_args()
    try:
        receipt = run(args.probe, args.dxvk, args.output, show=args.show)
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        parser.exit(1, f'{error}\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
