"""Gold-specific, owner-thread native bootstrap for the shared Lua debugger."""
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import queue
import shutil
import struct
import subprocess
import sys
import time

import debugger
from profiles import select


class Entry:
    def __init__(self, game, dll):
        from bootstrap import Windows
        from entry_bridge import bridge
        self.api = Windows()
        self.k = self.api.k
        self.handle = int(game._handle)
        self.pid = game.pid
        peb, returned = c.c_size_t(), w.ULONG()
        if self.api.nt.NtQueryInformationProcess(self.handle, 26, c.byref(peb), c.sizeof(peb), c.byref(returned)) != 0 or not peb.value:
            raise RuntimeError('Gold WOW64 image identity unavailable')
        image = struct.unpack('<I', self.read(peb.value+8, 4))[0]
        header = self.read(image, 4096)
        pe = struct.unpack_from('<I', header, 0x3c)[0]
        if (header[:2] != b'MZ' or not 64 <= pe < 3000 or header[pe:pe+6] != b'PE\0\0\x4c\x01'
                or struct.unpack_from('<I', header, pe+8)[0] != 0x4d8744d4
                or struct.unpack_from('<I', header, pe+0x50)[0] != 0xdc4000):
            raise RuntimeError('Gold mapped image identity mismatch')
        self.entry = image+0x7aee5b
        self.original = bytes.fromhex('e8de090000')
        if self.read(self.entry, 5) != self.original:
            raise RuntimeError('Gold entry-byte guard failed')
        remote = self.k.VirtualAllocEx(self.handle, None, 4096, 0x3000, 4)
        self.status = self.k.VirtualAllocEx(self.handle, None, 4096, 0x3000, 4)
        if not remote or not self.status or max(remote, self.status) > 0xfffff000:
            raise RuntimeError('Gold bounded bridge allocation failed')
        data, _ = bridge(remote, image, self.status, str(dll))
        self.write(remote, data)
        if self.protect(remote, 4096, 0x20) != 4 or not self.k.FlushInstructionCache(self.handle, remote, 4096):
            raise RuntimeError('Gold bridge protection failed')
        self.jump = b'\xe9'+struct.pack('<I', (remote-self.entry-5) & 0xffffffff)
        old = self.protect(self.entry, 5, 0x40)
        try:
            self.write(self.entry, self.jump)
        finally:
            self.protect(self.entry, 5, old)
        if not self.k.FlushInstructionCache(self.handle, self.entry, 5):
            raise RuntimeError('Gold entry instruction flush failed')

    def read(self, address, size):
        data, done = c.create_string_buffer(size), c.c_size_t()
        if not self.k.ReadProcessMemory(self.handle, address, data, size, c.byref(done)) or done.value != size:
            raise RuntimeError('Gold bounded native read failed')
        return data.raw

    def write(self, address, value):
        data, done = c.create_string_buffer(value), c.c_size_t()
        if not self.k.WriteProcessMemory(self.handle, address, data, len(value), c.byref(done)) or done.value != len(value):
            raise RuntimeError('Gold bounded native write failed')

    def protect(self, address, size, mode):
        old = w.DWORD()
        if not self.k.VirtualProtectEx(self.handle, address, size, mode, c.byref(old)):
            raise RuntimeError('Gold native protection transaction failed')
        return old.value

    def confirm(self, game):
        deadline = time.monotonic()+20
        while time.monotonic() < deadline:
            if game.poll() is not None:
                raise RuntimeError('Gold exited before native initialization')
            state = struct.unpack('<I', self.read(self.status, 4))[0]
            if state == 1:
                break
            if state == 2:
                raise RuntimeError('Gold native bootstrap rejected this copy')
            time.sleep(.02)
        else:
            raise TimeoutError('Gold initialization deadline')
        if self.read(self.entry, 5) != self.jump:
            raise RuntimeError('Gold bridge ownership lost')
        old = self.protect(self.entry, 5, 0x40)
        try:
            self.write(self.entry, self.original)
        finally:
            self.protect(self.entry, 5, old)
        if not self.k.FlushInstructionCache(self.handle, self.entry, 5) or self.read(self.entry, 5) != self.original:
            raise RuntimeError('Gold original entry restoration failed')


def profile_for(exe, documents):
    k = c.WinDLL('kernel32', use_last_error=True)
    k.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
    k.CreateFileW.restype = w.HANDLE
    k.GetFinalPathNameByHandleW.argtypes = [w.HANDLE, w.LPWSTR, w.DWORD, w.DWORD]
    k.GetFinalPathNameByHandleW.restype = w.DWORD
    k.CloseHandle.argtypes = [w.HANDLE]
    handle = k.CreateFileW(str(exe), 0x80000000, 1, None, 3, 0, None)
    if handle == c.c_void_p(-1).value:
        raise RuntimeError('Cannot resolve Gold final executable path')
    try:
        path = c.create_unicode_buffer(32768)
        length = k.GetFinalPathNameByHandleW(handle, path, len(path), 0)
        if not 0 < length < len(path):
            raise RuntimeError('Gold final path is truncated')
        identifier = hashlib.sha256(path.value.upper().encode('utf-16le')).hexdigest()[:32]
        return documents/'WePlay/Gold'/identifier
    finally:
        k.CloseHandle(handle)


def run(args, helpers):
    ROOT, snapshot, backup, digest, OwnedThread = (helpers[n] for n in
        ('ROOT', 'snapshot', 'backup', 'digest', 'OwnedThread'))
    import memory_guard, observe_game
    sys.path.insert(0, str(ROOT/'tools/gold'))
    import pefile
    root, output = args.bundle.resolve(), args.output.resolve()
    exe = root/'Data/Base/_Dbg/Bin/Release/Settlers7R.exe'
    pin = digest(exe)
    if select(pin)['version'] != 'gold':
        raise ValueError('Expected the exact prepared Gold image')
    dll = args.gold_adapter.resolve()
    if not dll.is_relative_to((ROOT/'work').resolve()) or dll.stat().st_size > 16*1024*1024:
        raise ValueError('Use an explicit bounded Gold adapter beneath work/')
    pe = pefile.PE(str(dll))
    if pe.FILE_HEADER.Machine != 0x14c or not any(e.name == b'GoldInitialize' for e in pe.DIRECTORY_ENTRY_EXPORT.symbols):
        raise ValueError('Gold native initializer DLL identity failed')
    pe.close()
    docs = c.create_unicode_buffer(32768)
    if c.WinDLL('shell32').SHGetFolderPathW(None, 5, None, 0, docs) != 0:
        raise RuntimeError('Windows Documents unavailable')
    documents = Path(docs.value).resolve()
    profile = profile_for(exe, documents)
    observer = observe_game.Windows()
    for p in observer.snapshot(2, 0, observe_game.Process, 'Process32FirstW', 'Process32NextW'):
        if p.name.lower() != 'settlers7r.exe':
            continue
        handle = observer.k.OpenProcess(0x1000, False, p.pid)
        if not handle:
            raise RuntimeError('Cannot verify concurrent game identity')
        try:
            if observer.path(handle).resolve() == exe.resolve():
                raise RuntimeError('Gold diagnostic copy already running')
        finally:
            observer.k.CloseHandle(handle)
    memory_guard.require_host_headroom()
    output.mkdir()
    before = snapshot(profile)
    backup(profile, output/'before/Profile', before)
    shared = [documents/n for n in ('Settlers7', 'THE SETTLERS 7 - History Edition') if (documents/n).is_dir()]
    shared_before = {str(p): snapshot(p) for p in shared}
    for i, p in enumerate(shared):
        backup(p, output/'before'/('Shared'+str(i)), shared_before[str(p)])
    immutable = {str(p): digest(p) for p in (exe, dll, root/'Data/Base/BBA/Interface.bba')}
    (output/'source-checkpoint.private.json').write_text(json.dumps({n: digest(Path(__file__).with_name(n)) for n in
        ('agent.js', 'stepping.js', 'fixture.lua', 'debugger.py', 'launch.py', 'profiles.py', 'gold_launch.py')}, indent=2))
    game = adapter = sentinel = None
    resumed = False
    failure = None
    initialized = False
    try:
        env = dict(os.environ, DXVK_CONFIG_FILE=str(exe.with_name('dxvk.conf')),
            DXVK_CONFIG='d3d9.floatEmulation = Strict; d3d9.presentInterval = 1; d3d9.maxFrameRate = 60',
            DXVK_LOG_PATH=str(output), DXVK_SHADER_CACHE_PATH=str(output), DXVK_STATE_CACHE_PATH=str(output))
        game = subprocess.Popen([str(exe), '-vSynch', '-noMapInMainMenu'], cwd=exe.parent, env=env, creationflags=0x4|0x20)
        primary = OwnedThread(game)
        try:
            if args.affinity_mask is not None:
                primary.affinity(game, args.affinity_mask)
            entry = Entry(game, dll)
            points = [dict(source=p.rsplit(':', 1)[0], line=int(p.rsplit(':', 1)[1])) for p in args.breakpoint]
            adapter = debugger.Adapter(exe, game.pid, output/'debugger.private.jsonl', selftest=args.self_test,
                                       pause_ms=args.pause_ms, breakpoints=points)
        finally:
            primary.resume(); resumed = True
        (output/'owned.private.json').write_text(json.dumps(dict(pid=game.pid, parent=os.getpid(), path=str(exe), sha256=pin,
            native_adapter_sha256=digest(dll), profile=str(profile)), indent=2))
        entry.confirm(game); initialized = True
        sentinel = memory_guard.Sentinel(game, output/'memory.private.json', memory_guard.Probe(game, exe)); sentinel.start()
        print(json.dumps(dict(event='debugger-launched', version='gold', pid=game.pid, native_entry_restored=True)), flush=True)
        if args.self_test:
            result = adapter.selftest()
            (output/'test-result.private.json').write_text(json.dumps(result, indent=2))
            print(json.dumps(dict(event='live-self-test-passed', version='gold', result=result)), flush=True)
        else:
            offset = 0
            while game.poll() is None:
                try:
                    print(json.dumps(adapter.events.get(timeout=.1)), flush=True)
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
                            adapter.command(**json.loads(line)); offset = stream.tell()
    except BaseException as error:
        failure = error
        (output/'failure.private.json').write_text(json.dumps(dict(type=type(error).__name__, detail=str(error))))
        if game is not None and not initialized and game.poll() is None:
            debugger.validate_target(exe, game.pid, os.getpid())
            game.terminate(); game.wait(timeout=10)
        if adapter is not None and game.poll() is None:
            try:
                adapter.close_after_restored(timeout=10)
            except Exception:
                pass
    finally:
        if game is not None:
            print(json.dumps(dict(event='waiting-for-normal-game-close', pid=game.pid)), flush=True); game.wait()
        if sentinel is not None:
            sentinel.close()
        if adapter is not None and not adapter.closed:
            adapter.close_after_restored(timeout=5)
        after = snapshot(profile); backup(profile, output/'after/Profile', after)
        for name in ('Options.ini', 'Profiles.xml'):
            old = output/'before/Profile'/name
            if old.exists():
                shutil.copy2(old, profile/name)
        result = dict(exit=game.returncode if game else None, native_entry_restored=initialized,
            settings_restored=all(digest(profile/n) == v['sha256'] for n, v in before.items() if n in ('Options.ini', 'Profiles.xml')),
            saves_unchanged={n:v for n,v in after.items() if n.lower().endswith(('.s7s','.s7local'))} ==
                            {n:v for n,v in before.items() if n.lower().endswith(('.s7s','.s7local'))},
            shared_unchanged={str(p):snapshot(p) for p in shared} == shared_before,
            game_files_unchanged=all(digest(Path(p)) == pin_ for p,pin_ in immutable.items()))
        (output/'restoration.private.json').write_text(json.dumps(result, indent=2)); print(json.dumps(result), flush=True)
    if failure is not None:
        raise failure
