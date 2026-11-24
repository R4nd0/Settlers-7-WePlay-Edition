"""Run one owned portable S7 graphics experiment with private profile backups.

No original installation, platform account cache, release pin or shared save is
changed. This laboratory runner deliberately omits ReShade preparation.
"""
from __future__ import annotations

import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools/installer'), str(ROOT / 'tools/offline-ui'),
                str(ROOT / 'tools/d3d11'), str(ROOT)]
import display_settings
import graphics_profile
import isolated_process
import launch_local
import memory_guard
import platform_standalone as platform
from tools.shadow_detail.profile_guard import ProfileGuard
from tools.graphics_lab import recover as recovery


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def lab_path(path):
    path = graphics_profile._plain(path)
    work = graphics_profile._plain(ROOT / 'work')
    if not path.is_relative_to(work) or path == work:
        raise ValueError('Use an explicit path inside repository work/.')
    return path


def clean_environment(inherited):
    blocked = ('DXVK_', 'D3D9_', 'VK_', 'RESHADE_', 'WEPLAY_', 'S7_')
    result = {key: value for key, value in inherited.items()
              if not key.upper().startswith(blocked)}
    # Disable implicit Vulkan add-ons only in this child's environment.
    result['VK_LOADER_LAYERS_DISABLE'] = '~implicit~'
    result['DXVK_CONFIG'] = 'd3d9.floatEmulation = Strict; d3d9.maxFrameRate = 60'
    return result


def shared_profile_roots():
    shell = C.WinDLL('shell32', use_last_error=True)
    shell.SHGetFolderPathW.argtypes = [W.HWND, C.c_int, W.HANDLE, W.DWORD, W.LPWSTR]
    shell.SHGetFolderPathW.restype = C.c_long
    buffer = C.create_unicode_buffer(32768)
    if shell.SHGetFolderPathW(None, 5, None, 0, buffer):
        raise RuntimeError('Cannot resolve the actual Windows Documents folder.')
    documents = Path(buffer.value)
    return {'shared-settlers7': documents / 'Settlers7',
            'shared-history': documents / 'THE SETTLERS 7 - History Edition',
            'shared-gold': documents / 'WePlay/Gold'}


def cleanup_steps(steps):
    """Attempt every independent cleanup, preserving a bounded failure record."""
    errors = []
    for label, action in steps:
        try:
            action()
        except Exception as error:
            errors.append({'stage': label, 'type': type(error).__name__})
    return errors


def run(root, output, *, width=1920, height=1080, seconds=900, on_initialized=None,
        hook=None, output_width=1280, output_height=720):
    if os.name != 'nt':
        raise ValueError('The portable graphics laboratory requires Windows.')
    root, output = lab_path(root), lab_path(output)
    if output.exists() or not output.parent.is_dir():
        raise ValueError('Use a fresh output directory with an existing parent.')
    if (type(width) is not int or type(height) is not int
            or not 800 <= width <= 3840 or not 600 <= height <= 2160
            or width * height > 3840 * 2160
            or type(seconds) is not int or not 30 <= seconds <= 1800):
        raise ValueError('Unsupported laboratory resolution or lifetime.')
    value, executable, adapter = platform.load(root)
    if any(p.exe.lower() == 'settlers7r.exe' for p in isolated_process.Windows().processes()):
        raise RuntimeError('Another S7 client is running; no concurrent diagnostic games.')
    renderer = executable.with_name('d3d9.dll')
    if digest(renderer) != graphics_profile.DXVK_SHA256:
        raise ValueError('Keep the exact pinned DXVK 3.1.1 renderer.')
    memory_guard.require_host_headroom()
    if hook is not None:
        if on_initialized is not None:
            raise ValueError('Choose the fixed hook initializer or an explicit callback, not both.')
        from . import load_hook
        hook, _ = load_hook.verify_build(hook)
    output.mkdir()
    shared = ProfileGuard(shared_profile_roots(), output / 'shared-before')
    shared.capture()
    private = ProfileGuard({'portable': root / 'LocalData/ProfileRoot'}, output / 'portable-before')
    private.capture()
    options = platform.options_path(root)
    previous_options = options.read_bytes() if options.exists() else None
    runtime = root / 'LocalData/s7-local-runtime.ini'
    previous_runtime = runtime.read_bytes() if runtime.exists() else None
    config_file = executable.with_name('dxvk.conf')
    previous_config = config_file.read_bytes() if config_file.exists() else None
    critical = [executable, adapter, renderer,
                executable.with_name('Vision90.dll'), executable.with_name('vBase90.dll')]
    immutable = {p.relative_to(root).as_posix(): digest(p) for p in critical}
    checkpoint = {str(p.relative_to(ROOT)): digest(p) for p in (
        Path(__file__), ROOT / 'tools/installer/platform_standalone.py',
        ROOT / 'tools/installer/isolated_process.py', ROOT / 'tools/d3d11/memory_guard.py',
        ROOT / 'tools/shadow_detail/profile_guard.py', ROOT / 'tools/graphics_lab/recover.py')}
    for name, pin in checkpoint.items():
        source = ROOT / name
        target = output / 'source' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        if digest(source) != pin or digest(target) != pin:
            raise RuntimeError('The graphics controller changed during source checkpoint creation.')
    (output / 'checkpoint.private.json').write_text(json.dumps(dict(
        files=immutable, sources=checkpoint, requested_render_size=[width, height],
        reshade_requested=False), indent=2), encoding='utf-8')
    recovery.prepare(root, output, critical=immutable,
        targets={'options': options, 'runtime': runtime, 'config': config_file},
        shared=shared_profile_roots())
    game = service = sentinel = None
    failure = None
    timed_out = False
    try:
        options.parent.mkdir(parents=True, exist_ok=True)
        options.write_bytes(display_settings.windowed_options(previous_options or b''))
        config_file.write_bytes(graphics_profile.CONFIG + b'd3d9.floatEmulation = Strict\n')
        if value['edition'] == 'history':
            with (output / 'service.private.jsonl').open('xb') as log, (output / 'service-errors.private.txt').open('xb') as errors:
                service = subprocess.Popen([
                    str(root / 'Runtime/dotnet/dotnet.exe'), str(root / 'Server/S7Patcher.dll'),
                    '--local-config-server', '--local-keepalive-probe', '--http-port', '0',
                    '--udp-port', '0', '--seconds', str(seconds + 60)],
                    stdout=log, stderr=errors, creationflags=subprocess.CREATE_NO_WINDOW)
            configuration, port = launch_local.service_ready(service, output / 'service.private.jsonl')
            launch_local.write_configuration(root / 'LocalData', configuration, port,
                                             True, display_name='GraphicsLab')
        environment = clean_environment(os.environ)
        environment['DXVK_LOG_PATH'] = str(output)
        environment['DXVK_CONFIG_FILE'] = str(config_file)
        arguments = ('-vSynch', '-noMapInMainMenu', '-width', str(width), '-height', str(height))
        initialize = on_initialized
        if hook is not None:
            def initialize(owned):
                receipt = load_hook.initialize(owned, hook, render=(width, height),
                    output=(output_width, output_height), report=output / 'hook-state.private.json')
                (output / 'hook-initialized.private.json').write_text(
                    json.dumps(receipt, indent=2), encoding='utf-8')
        game = platform.launch(root, environment=environment, arguments=arguments,
                               on_initialized=initialize)
        (output / 'owned.private.json').write_text(json.dumps(dict(
            pid=game.pid, parent=os.getpid(), path=str(executable), sha256=digest(executable),
            native_initialized=game.native_initialized, kernel_isolation=game.kernel_isolation,
            cpu_affinity_mask=game.cpu_affinity_mask), indent=2), encoding='utf-8')
        sentinel = memory_guard.Sentinel(game, output / 'memory.private.json',
            memory_guard.Probe(game, executable), warn=lambda level: print(
                json.dumps(dict(event='graphics-lab-memory-stop', level=level)), flush=True))
        sentinel.start()
        print(json.dumps(dict(event='graphics-game-launched', pid=game.pid,
                              requested_render_size=[width, height], portable=True,
                              reshade_requested=False)), flush=True)
        deadline = time.monotonic() + seconds
        while game.poll() is None:
            if sentinel.summary['monitor_failed'] or sentinel.summary['recovery_required']:
                raise RuntimeError('The memory guard failed or confirmed pressure; stop this owned experiment.')
            if time.monotonic() >= deadline or (output / 'stop-owned.request').exists():
                # Terminate only the original owned handle, after PID/parent/path/hash verification.
                timed_out = True
                game.terminate()
                game.wait(timeout=15, close=False)
                break
            time.sleep(.25)
    except BaseException as error:
        failure = error
    finally:
        def stop_game():
            if game and game.poll() is None:
                game.terminate()
                game.wait(timeout=15, close=False)
        def stop_service():
            if service and service.poll() is None:
                service.terminate()
                service.wait(timeout=15)
        exit_state = {'code': None}
        def close_game():
            if game:
                exit_state['code'] = game.poll()
                game.close()
        cleanup = [('game-stop', stop_game),
                   ('memory-close', lambda: sentinel.close() if sentinel else None),
                   ('game-close', close_game), ('service-stop', stop_service)]
        # The durable journal restores only private lab configuration, never saves.
        cleanup.append(('recover-private-configuration', lambda: recovery.recover(output)))
        cleanup.append(('shared-profile-verification', shared.verify_unchanged))
        cleanup_errors = cleanup_steps(cleanup)
        def verify(label, check):
            try:
                return check()
            except Exception as error:
                cleanup_errors.append({'stage': label, 'type': type(error).__name__})
                return False
        unchanged = not any(e['stage'] == 'shared-profile-verification' for e in cleanup_errors)
        result = dict(game_started=game is not None, exit_code=exit_state['code'],
                      owned_timeout_or_stop=timed_out, shared_profiles_unchanged=unchanged,
                      game_binaries_unchanged=verify('binary-verification', lambda: all(
                          digest(root / n) == h for n, h in immutable.items())),
                      private_options_restored=verify('options-verification', lambda: (
                          options.read_bytes() if options.exists() else None) == previous_options),
                      memory=dict(sentinel.summary) if sentinel else None,
                      cleanup_errors=cleanup_errors,
                      error_type=type(failure).__name__ if failure else None,
                      supersampling_game_qualified=False)
        (output / 'result.private.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        print(json.dumps(result), flush=True)
        if cleanup_errors or not result['game_binaries_unchanged'] or not result['private_options_restored']:
            if failure:
                failure.add_note('graphics_lab_cleanup_or_verification_failed; retain private diagnostics')
            else:
                failure = RuntimeError('Graphics lab cleanup/verification failed; retain current files and backups.')
    if failure:
        raise failure
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--width', type=int, default=1920)
    parser.add_argument('--height', type=int, default=1080)
    parser.add_argument('--seconds', type=int, default=900)
    parser.add_argument('--hook', type=Path, help='Exact source-bound experimental supersampling DLL')
    parser.add_argument('--output-width', type=int, default=1280)
    parser.add_argument('--output-height', type=int, default=720)
    args = parser.parse_args()
    run(args.bundle, args.output, width=args.width, height=args.height, seconds=args.seconds,
        hook=args.hook, output_width=args.output_width, output_height=args.output_height)


if __name__ == '__main__':
    main()
