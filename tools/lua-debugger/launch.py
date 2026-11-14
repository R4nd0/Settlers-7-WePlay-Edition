"""Launch an existing isolated lab copy with the opt-in local Lua debugger.

No installed original is accepted. The separate game bundle must already have
the project's lab-compatible platform adapter and DXVK. Ordinary observation
deploys no binaries/archives. The Gold marker self-test temporarily stages one
guarded core-script fixture and restores its archive after game closure.
--self-test runs a small private chunk inside the real VM.
Ordinary mode accepts JSON-lines commands from --commands; it injects no chunk.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'tools/installer'), str(ROOT/'tools/offline-ui'), str(ROOT/'tools/d3d11')]
import debugger
import launch_local
import memory_guard
import observe_game
from profiles import select


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def snapshot(folder):
    return {str(p.relative_to(folder)): dict(sha256=digest(p), mtime_ns=p.stat().st_mtime_ns)
            for p in folder.rglob('*') if p.is_file()
            and (p.suffix.lower() in ('.s7s', '.s7local') or p.name in ('Options.ini', 'Profiles.xml'))}


def backup(folder, destination, files):
    for relative in files:
        target = destination/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(folder/relative, target)


class OwnedThread:
    def __init__(self, game):
        self.k = c.WinDLL('kernel32', use_last_error=True)
        self.k.OpenThread.argtypes = [w.DWORD, w.BOOL, w.DWORD]
        self.k.OpenThread.restype = w.HANDLE
        self.k.GetProcessIdOfThread.argtypes = [w.HANDLE]
        self.k.GetProcessIdOfThread.restype = w.DWORD
        self.k.ResumeThread.argtypes = [w.HANDLE]
        self.k.ResumeThread.restype = w.DWORD
        self.k.CloseHandle.argtypes = [w.HANDLE]
        self.k.SetProcessAffinityMask.argtypes = [w.HANDLE, c.c_size_t]
        self.k.SetProcessAffinityMask.restype = w.BOOL
        # Reuse the project's Toolhelp wrapper and structure, not guessed TIDs.
        api = observe_game.Windows()
        threads = [t for t in api.snapshot(4, 0, observe_game.Thread, 'Thread32First', 'Thread32Next')
                   if t.owner == game.pid]
        if len(threads) != 1 or game.poll() is not None:
            raise RuntimeError('Suspended primary thread is ambiguous')
        self.pid = game.pid
        self.handle = self.k.OpenThread(0x0802, False, threads[0].tid)
        if not self.handle or self.k.GetProcessIdOfThread(self.handle) != self.pid:
            raise RuntimeError('Cannot pin the owned primary thread')

    def affinity(self, game, mask):
        self.k.GetProcessAffinityMask.argtypes = [w.HANDLE, c.POINTER(c.c_size_t), c.POINTER(c.c_size_t)]
        self.k.GetProcessAffinityMask.restype = w.BOOL
        current, system = c.c_size_t(), c.c_size_t()
        if (not self.k.GetProcessAffinityMask(int(game._handle), c.byref(current), c.byref(system))
                or mask <= 0 or mask & ~system.value or mask.bit_count() > 4
                or not self.k.SetProcessAffinityMask(int(game._handle), mask)):
            raise RuntimeError('Cannot apply the explicit owned-process CPU mask')

    def resume(self):
        try:
            if self.k.GetProcessIdOfThread(self.handle) != self.pid or self.k.ResumeThread(self.handle) != 1:
                raise RuntimeError('Owned thread resume mismatch')
        finally:
            self.k.CloseHandle(self.handle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--dotnet', type=Path)
    parser.add_argument('--service', type=Path)
    parser.add_argument('--gold-adapter', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--trace-errors-only',action='store_true')
    parser.add_argument('--gold-marker',action='append',default=[],metavar='EXACT_MARKER')
    parser.add_argument('--self-test-gold-markers',action='store_true')
    parser.add_argument('--gold-channel',choices=('both','log','console'),default='both')
    parser.add_argument('--commands', type=Path)
    parser.add_argument('--breakpoint', action='append', default=[], metavar='SOURCE:LINE')
    parser.add_argument('--pause-ms', type=int, default=10000)
    parser.add_argument('--affinity-mask', type=lambda text: int(text, 0))
    args = parser.parse_args()
    capture=bool(args.gold_marker or args.self_test_gold_markers)
    if args.gold_channel!='both' and not capture:parser.error('Gold channel selection requires marker capture')
    if capture:
        if args.self_test or args.trace_errors_only or args.breakpoint or args.commands is not None:
            parser.error('Gold marker capture cannot be combined with debugger execution modes')
        if args.self_test_gold_markers and args.gold_marker:
            parser.error('The Gold marker self-test uses its fixed explicit markers')
        if args.self_test_gold_markers and args.gold_channel!='both':parser.error('The self-test requires both Gold channels')
        import gold_capture
        try:gold_capture.markers(gold_capture.TEST_MARKERS if args.self_test_gold_markers else args.gold_marker)
        except ValueError as error:parser.error(str(error))
    if args.trace_errors_only and (args.self_test or args.breakpoint or args.commands is not None):
        parser.error('Error-only tracing cannot be combined with fixtures, breakpoints or commands')
    root, output = args.bundle.resolve(), args.output.resolve()
    if not root.is_relative_to((ROOT/'work').resolve()) or not output.is_relative_to((ROOT/'work').resolve()):
        parser.error('Bundle and fresh output must be beneath repository work/')
    if output.exists() or not output.parent.is_dir():
        parser.error('Use a fresh output directory with an existing parent')
    if not args.self_test and not args.trace_errors_only and not capture and args.commands is None:
        parser.error('Ordinary mode requires a local JSON-lines --commands file')
    if args.commands and not args.commands.resolve().is_relative_to((ROOT/'work').resolve()):
        parser.error('Keep commands under repository work/')
    if (root/'Launcher/platform-standalone.json').exists():
        if args.affinity_mask is not None:parser.error('Portable copies use the verified physical-core policy')
        import standalone_launch
        return standalone_launch.run(args,dict(snapshot=snapshot,backup=backup,digest=digest))
    if args.trace_errors_only:parser.error('Error-only tracing requires a prepared portable copy')
    if capture:parser.error('Gold marker capture requires a prepared portable copy')
    gold_exe = root/'Data/Base/_Dbg/Bin/Release/Settlers7R.exe'
    if gold_exe.is_file():
        if args.gold_adapter is None:
            parser.error('Gold requires its explicitly selected --gold-adapter')
        import gold_launch
        return gold_launch.run(args, dict(ROOT=ROOT, snapshot=snapshot, backup=backup,
                                         digest=digest, OwnedThread=OwnedThread))
    if args.service is None or args.dotnet is None:
        parser.error('History requires --dotnet and --service for its loopback bootstrap')
    exe = root/'Base/Bin/Settlers7R.exe'
    pin = digest(exe)
    selected = select(pin)
    slot = selected['slot']
    if selected['version'] not in ('history-ubisoft', 'history-steam'):
        parser.error('Unexpected History profile')
    if slot is None:
        parser.error('Original History images require a prepared portable package copy')
    docs_buffer = c.create_unicode_buffer(32768)
    if c.WinDLL('shell32').SHGetFolderPathW(None, 5, None, 0, docs_buffer) != 0:
        raise RuntimeError('Cannot resolve actual Windows Documents')
    documents = Path(docs_buffer.value).resolve()
    profile = documents/'Claude/Projects/7S_Patcher/work'/slot
    if profile.resolve() != (ROOT/'work'/slot).resolve():
        raise RuntimeError('Native lab profile does not resolve to repository work/')
    observer = observe_game.Windows()
    for process in observer.snapshot(2, 0, observe_game.Process, 'Process32FirstW', 'Process32NextW'):
        if process.name.lower() != 'settlers7r.exe':
            continue
        handle = observer.k.OpenProcess(0x1000, False, process.pid)
        if not handle:
            raise RuntimeError('Cannot verify another S7 process')
        try:
            other = observer.path(handle)
            marker = ('\\Claude\\Projects\\7S_Patcher\\work\\'+slot).encode()+b'\0'
            if other.resolve() == exe.resolve() or marker in other.read_bytes():
                raise RuntimeError('Another game already uses this diagnostic profile')
        finally:
            observer.k.CloseHandle(handle)
    memory_guard.require_host_headroom()
    output.mkdir()
    shared = [documents/name for name in ('Settlers7', 'THE SETTLERS 7 - History Edition')
              if (documents/name).is_dir()]
    before_shared = {str(p): snapshot(p) for p in shared}
    before = snapshot(profile)
    cloud_before = snapshot(root/'LocalData/saves')
    backup(profile, output/'before/Profile', before)
    for n, folder in enumerate(shared):
        backup(folder, output/'before'/('Shared'+str(n)), before_shared[str(folder)])
    backup(root/'LocalData/saves', output/'before/Cloud', cloud_before)
    runtime = root/'LocalData/s7-local-runtime.ini'
    previous_runtime = runtime.read_bytes() if runtime.exists() else None
    immutable = {name: digest(root/name) for name in
                 ('Base/Bin/Settlers7R.exe', 'Base/Bin/uplay_r1_loader.dll', 'Base/BBA/Interface.bba')}
    sources = {name: digest(Path(__file__).with_name(name)) for name in
               ('agent.js', 'stepping.js','lua_errors.js', 'fixture.lua', 'debugger.py', 'launch.py', 'profiles.py')}
    (output/'source-checkpoint.private.json').write_text(json.dumps(sources, indent=2))
    service = game = sentinel = adapter = None
    resumed = False
    failure = None
    try:
        with (output/'service.private.jsonl').open('xb') as log, (output/'service-errors.private.txt').open('xb') as errors:
            service = subprocess.Popen([str(args.dotnet.resolve()), str(args.service.resolve()), '--local-config-server',
                '--local-catalogue-probe', '--http-port', '0', '--udp-port', '0', '--seconds', '1800'],
                stdout=log, stderr=errors, env=dict(os.environ, S7_LOCAL_PAIR_ADDRESS='127.0.0.2'),
                creationflags=subprocess.CREATE_NO_WINDOW)
            configuration, port = launch_local.service_ready(service, output/'service.private.jsonl')
        launch_local.write_configuration(root/'LocalData', configuration, port, True, display_name='LuaDebuggerLab')
        env = dict(os.environ, DXVK_CONFIG_FILE=str(exe.with_name('dxvk.conf')),
                   DXVK_CONFIG='d3d9.floatEmulation = Strict; d3d9.presentInterval = 1; d3d9.maxFrameRate = 60',
                   DXVK_LOG_PATH=str(output), DXVK_SHADER_CACHE_PATH=str(root/'LocalData'),
                   DXVK_STATE_CACHE_PATH=str(root/'LocalData'))
        if selected['version'] == 'history-steam':
            env.update(SteamAppId='965320', SteamGameId='965320')
        memory_guard.require_host_headroom()
        game = subprocess.Popen([str(exe), '-vSynch', '-noMapInMainMenu'], cwd=exe.parent,
                                env=env, creationflags=0x4|0x20)
        primary = OwnedThread(game)
        try:
            if args.affinity_mask is not None:
                primary.affinity(game, args.affinity_mask)
            points = [dict(source=p.rsplit(':', 1)[0], line=int(p.rsplit(':', 1)[1])) for p in args.breakpoint]
            if selected['version'] != 'history-steam':
                adapter = debugger.Adapter(exe, game.pid, output/'debugger.private.jsonl',
                                           selftest=args.self_test, pause_ms=args.pause_ms, breakpoints=points)
        finally:
            primary.resume()
            resumed = True
        if selected['version'] == 'history-steam':
            # Let the genuine SDK initialize without loader-time instrumentation.
            # Existing states are registered later at an actual owner-thread pcall.
            user = c.WinDLL('user32', use_last_error=True)
            user.WaitForInputIdle.argtypes = [w.HANDLE, w.DWORD]
            user.WaitForInputIdle.restype = w.DWORD
            idle = user.WaitForInputIdle(int(game._handle), 30000)
            if game.poll() is not None or idle != 0:
                raise RuntimeError('Steam startup did not reach an idle game before debugger attachment')
            adapter = debugger.Adapter(exe, game.pid, output/'debugger.private.jsonl',
                                       selftest=args.self_test, pause_ms=args.pause_ms, breakpoints=points)
        (output/'owned.private.json').write_text(json.dumps(dict(pid=game.pid, parent=os.getpid(),
            path=str(exe), sha256=pin, profile_slot=slot), indent=2))
        sentinel = memory_guard.Sentinel(game, output/'memory.private.json', memory_guard.Probe(game, exe))
        sentinel.start()
        print(json.dumps(dict(event='debugger-launched', pid=game.pid, profile_slot=slot, version=selected['version'])), flush=True)
        if args.self_test:
            result = adapter.selftest()
            (output/'test-result.private.json').write_text(json.dumps(result, indent=2))
            print(json.dumps(dict(event='live-self-test-passed', result=result)), flush=True)
        else:
            offset = 0
            while game.poll() is None:
                try:
                    event = adapter.events.get(timeout=.1)
                    print(json.dumps(event), flush=True)
                except queue.Empty:
                    pass
                if args.commands.exists():
                    if args.commands.stat().st_size > 1024*1024:
                        raise ValueError('Command file exceeds 1 MiB')
                    with args.commands.open(encoding='utf-8') as stream:
                        stream.seek(offset)
                        while True:
                            line = stream.readline()
                            if not line.endswith('\n'):
                                break
                            command = json.loads(line)
                            adapter.command(**command)
                            offset = stream.tell()
    except BaseException as error:
        failure = error
        (output/'failure.private.json').write_text(json.dumps(dict(type=type(error).__name__, detail=str(error))))
        if game is not None and not resumed and game.poll() is None:
            # The game has never executed. Revalidate PID/parent/path/hash before
            # rolling back only this owned suspended startup, not another app.
            debugger.validate_target(exe, game.pid, os.getpid())
            (output/'startup-abort.private.json').write_text(json.dumps(dict(pid=game.pid,
                parent=os.getpid(), path=str(exe), sha256=pin, suspended_before_entry=True)))
            game.terminate(); game.wait(timeout=10)
        if adapter is not None:
            try:
                adapter.close_after_restored(timeout=10)
            except Exception:
                print('Debugger cleanup pending; retain the agent until normal game exit.', flush=True)
    finally:
        if game is not None:
            print(json.dumps(dict(event='waiting-for-normal-game-close', pid=game.pid)), flush=True)
            game.wait()
        if sentinel is not None:
            sentinel.close()
        if adapter is not None and not adapter.closed:
            adapter.close_after_restored(timeout=5)
        if service is not None and service.poll() is None:
            service.terminate(); service.wait(timeout=15)
        after = snapshot(profile)
        backup(profile, output/'after/Profile', after)
        # Restore only diagnostic settings; never replace any save, old or new.
        for name in ('Options.ini', 'Profiles.xml'):
            old = output/'before/Profile'/name
            if old.exists():
                shutil.copy2(old, profile/name)
        if previous_runtime is not None:
            runtime.write_bytes(previous_runtime)
        elif runtime.exists():
            runtime.unlink()
        restoration = dict(exit=game.returncode if game else None,
            settings_restored=all(digest(profile/n) == v['sha256'] for n, v in before.items()
                                  if n in ('Options.ini', 'Profiles.xml')),
            saves_unchanged={n: v for n, v in after.items() if n.lower().endswith('.s7s')} ==
                            {n: v for n, v in before.items() if n.lower().endswith('.s7s')},
            cloud_unchanged=snapshot(root/'LocalData/saves') == cloud_before,
            shared_unchanged={str(p): snapshot(p) for p in shared} == before_shared,
            game_files_unchanged=all(digest(root/n) == v for n, v in immutable.items()))
        (output/'restoration.private.json').write_text(json.dumps(restoration, indent=2))
        print(json.dumps(restoration), flush=True)
    if failure is not None:
        raise failure


if __name__ == '__main__':
    main()
