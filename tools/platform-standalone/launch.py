"""Run the isolated Steam local-facade candidate with reversible DLL deployment.

Uses the existing debugger launcher's PID/path/hash, memory, profile backup,
loopback service and normal-exit restoration workflow. Production copies and
installed originals are deliberately unsupported.
"""
import argparse
import json
import os
from pathlib import Path
import runpy
import shutil
import sys
from verify import check, sha

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'tools/lua-debugger'), str(ROOT/'tools/installer'),
               str(ROOT/'tools/offline-ui'), str(ROOT/'tools/d3d11')]
import observe_game
import memory_guard


def not_running(exe):
    observer = observe_game.Windows()
    for row in observer.snapshot(2, 0, observe_game.Process, 'Process32FirstW', 'Process32NextW'):
        if row.name.lower() != 'settlers7r.exe':
            continue
        handle = observer.k.OpenProcess(0x1000, False, row.pid)
        if not handle:
            raise RuntimeError('Cannot verify another S7 process')
        try:
            if observer.path(handle).resolve() == exe.resolve():
                raise RuntimeError('Candidate copy is still running; retain backups and do not overwrite its DLLs')
        finally:
            observer.k.CloseHandle(handle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--language', default='english')
    args, rest = parser.parse_known_args()
    for path in (args.bundle, args.native, args.output):
        if not path.resolve().is_relative_to((ROOT/'work').resolve()):
            parser.error('All candidate paths must be beneath repository work/')
    if args.output.exists() or not args.output.parent.is_dir():
        parser.error('Use a fresh output with an existing parent')
    if not args.language.isascii() or not args.language.islower() or not args.language.isalpha() or len(args.language) > 31:
        parser.error('Language must be a lower-case ASCII Steam language name, at most 31 letters')
    # Do not permit forwarded arguments to substitute the reviewed owned copy.
    if any(value.split('=')[0] in ('--bundle', '--native', '--output', '--gold-adapter') for value in rest):
        parser.error('Reserved launch argument')
    exe = args.bundle.resolve()/'Base/Bin/Settlers7R.exe'
    not_running(exe)
    result = check(exe, args.native)
    sdk = exe.with_name('steam_api.dll')
    if sha(sdk) != 'd99d425793f588fbc15f91c7765977cdd642b477be01dac41c0388ab6a5d492d':
        parser.error('Expected the unchanged original SDK before reversible lab deployment')
    args.output.mkdir()
    (args.output/'guards.private.json').write_text(json.dumps(result, indent=2))
    sources = {p.name: sha(p) for p in Path(__file__).parent.iterdir() if p.is_file()}
    (args.output/'source-checkpoint.private.json').write_text(json.dumps(sources, indent=2))
    memory = memory_guard.host_memory()
    (args.output/'preflight-memory.private.json').write_text(json.dumps(memory, indent=2))
    try:
        memory_guard.require_host_headroom(memory)
    except memory_guard.HostMemoryError:
        (args.output/'blocked.private.json').write_text(json.dumps(dict(reason='host-memory',
            required_free_ram_and_commit=4096*1024*1024, deployment_started=False, game_started=False)))
        raise
    names = ('uplay_r1_loader.dll', 'steam_api.dll')
    pins = {}
    for name in names:
        target = exe.with_name(name)
        pins[name] = sha(target)
        shutil.copy2(target, args.output/('before-'+name))
    previous_language = os.environ.get('WEPLAY_STANDALONE_LANGUAGE')
    os.environ['WEPLAY_STANDALONE_LANGUAGE'] = args.language
    try:
        for name in names:
            not_running(exe)
            shutil.copy2(args.native/name, exe.with_name(name))
            expected = result['boundary_sha256'] if name == 'steam_api.dll' else result['adapter_sha256']
            if sha(exe.with_name(name)) != expected:
                raise RuntimeError('Candidate DLL changed during deployment')
        sys.argv = ['launch.py', '--bundle', str(args.bundle), '--output', str(args.output/'live'), *rest]
        runpy.run_path(str(ROOT/'tools/lua-debugger/launch.py'), run_name='__main__')
    finally:
        if previous_language is None:
            os.environ.pop('WEPLAY_STANDALONE_LANGUAGE', None)
        else:
            os.environ['WEPLAY_STANDALONE_LANGUAGE'] = previous_language
        not_running(exe)
        for name in names:
            shutil.copy2(args.output/('before-'+name), exe.with_name(name))
            if sha(exe.with_name(name)) != pins[name]:
                raise RuntimeError('Candidate DLL restoration failed')
        (args.output/'deployment-restored.private.json').write_text(json.dumps(dict(all_original_dlls_restored=True)))


if __name__ == '__main__':
    main()
