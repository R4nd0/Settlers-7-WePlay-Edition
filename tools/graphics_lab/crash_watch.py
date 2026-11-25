"""Bounded CDB exception capture for a handle-admitted graphics-lab child.

The original game owner must call admit AFTER original-UI load and a new private
save. This module neither launches a game nor terminates one. Debugger timings
are diagnostic, never frame-pacing measurements. Dumps/logs stay under work/.

Commands follow Microsoft CDB, sx, gn, qd and .dump documentation. First-chance
AVs are passed to the application's original handler with gn. A second-chance
AV is captured then detached. No AeDebug/WER, heap, registry or shader changes.
"""
from __future__ import annotations

import argparse
from contextlib import ExitStack
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

from . import recover

ROOT = Path(__file__).resolve().parents[2]
ADMISSION = 'crash-admission.private.json'
FORMAT = 's7-owned-crash-admission-v1'
DEFAULT_CDB = ROOT / 'work/debugger-acquisition-20261124/portable/x86/cdb.exe'
MAX_BYTES = 64 * 1024 * 1024
MAX_DUMPS = 4
STEAM_EXE = '8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410'
# Individually Microsoft-Authenticode-verified x86 payload from the official
# WinDbg1.2611.4001.0 MSIX, not an arbitrary executable selected by the caller.
CDB_PINS = {
    'cdb.exe': '224f1efeaa65ea98d2270b0080173c80cd0d1170be8bd660e434bd23c1fd5fe0',
    'dbgcore.dll': '6faa8dd0063a7b00d82d8cf74f08c886e29234ffd50b9e161902381cc533b194',
    'DbgCredentialProvider.dll': '5a0b692398238bcf81a3e50ed033146e087785a50e4b0e25fbf50e642778f645',
    'dbgeng.dll': 'ba7f18c81891497adec9af6fb221ff1fa51e33c419497863f2c6331e6853acc8',
    'dbghelp.dll': '7c3bc343889431929302718b42575d1ea834b43cbc91be0c665ea89568e651f4',
    'dbgmodel.dll': '5a0924af7a55b663bbef406f168cfd9f43802c89fd6b74aa219b36b006ecf6c3',
    'dbgsrv.exe': '90571f002574ce5a5a792d9a5b22ebad3dc4b910c20a8befac153900afeb11aa',
    'EngHost.exe': '76e7d188c930bf2176de47ece4410115e9aec1acb20a07b1e31dc3149c4645fd',
    'kd.exe': '67aed2f537f423798772758811c4feefae333d728dd1898b5de4f0a6c532a62c',
    'kdbgctrl.exe': '63422febe3fca24d5a1dfc050b5d29cba90bf9092a4f19cb42f580fbfef17804',
    'kdnet-dev-ext.dll': '319004e308622b73e3ef61e45aabe08de34646cbebbb98b8d1fcec344f9eb030',
    'kdnet.exe': '6695bee21eb5ad19f03489fd4545eebf027ed8b00b1f73f4a06ace6a020eac4e',
    'mdviewer.exe': '0cd833c4cbd2bf9370e1826e407d853ae4a1a6023b1a15688da171dd6f4d6ea3',
    'mdviewercore.dll': '00632cb3f0927163f41112ffc9c7624c0398331dece26048b134d7dac696f8cd',
    'msdia140.dll': 'cf51213fc24ebc8c2624f26b504c42396c9203ff10884ac07d73fc3369a4c8ae',
    'ntsd.exe': '2d762def52236e57d8f4be7c1af30eefd7f6af83dd1aa945f6d2f6565883c1b2',
    'srcsrv.dll': 'feca0abbfa4425240c53615f32b387d0c27e0a9a7062000bc1be4022022ea977',
    'symsrv.dll': '8aee09ca1a96d929274725b6c85ef0732c11e27ec6f459fb90bf85360beacf0c',
}


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def _json(path):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('Duplicate diagnostic JSON key.')
            result[key] = value
        return result
    return json.loads(recover._read(path, 1024 * 1024)[0], object_pairs_hook=pairs)


def _write_new(path, value):
    raw = (json.dumps(value, indent=2, allow_nan=False) + '\n').encode('utf-8')
    with path.open('xb') as stream:
        stream.write(raw)


def _case(case):
    case = recover.lab(case)
    journal = _json(case / recover.JOURNAL)
    if (journal.get('format') != recover.FORMAT or journal.get('state') != 'prepared'
            or (case / 'result.private.json').exists()):
        raise ValueError('Only an active prepared owned graphics case is admitted.')
    root = recover.lab(journal['root'])
    recover._critical(root, journal['critical'])
    checkpoint = _json(case / 'checkpoint.private.json')
    if (checkpoint.get('files') != journal['critical'] or not checkpoint.get('sources')
            or checkpoint.get('crash_watch_allowed') is not True):
        raise ValueError('Case binary/source checkpoint mismatch.')
    watcher_sources = [pin for name, pin in checkpoint['sources'].items()
                       if Path(name).as_posix() == 'tools/graphics_lab/crash_watch.py']
    if watcher_sources != [digest(__file__)]:
        raise ValueError('This case did not checkpoint and enable this exact crash watcher.')
    for relative, pin in checkpoint['sources'].items():
        path = recover.plain(ROOT / relative)
        if not path.is_relative_to(ROOT / 'tools'):
            raise ValueError('Unexpected controller source.')
        if digest(path) != pin or digest(case / 'source' / relative) != pin:
            raise ValueError('Controller/source checkpoint changed.')
    return case, root


def commands(*,steam_heap=False):
    """All debugger-language tokens are fixed; no user path is interpolated.

    Three first-chance dumps leave one possible second-chance slot. On a
    fourth first-chance event detach without swallowing the original exception.
    /m includes only the normal module/thread/stack minidump; /u is unique.
    """
    # The prior owned Steam exception established this original instruction
    # and its allocation-header registers against the exact admitted EXE.
    # Read only small fixed windows when that same fault occurs; never probe
    # arbitrary pointers for another exception or change exception handling.
    header = ('.if (@eip == (Settlers7R+0x9350e0)) { '
              '.echo S7_STEAM_ALLOCATION_HEADER; dd @edi-0x20 L40; '
              '.echo S7_STEAM_ALLOCATION_MANAGER; dd @esi L20; '
              '.echo S7_STEAM_PERF_REGISTRY; dd (Settlers7R+0xf756b0) L1; '
              '.echo S7_STEAM_CANDIDATE_OWNER_HEADER; dd @edi-0xa18 L40 }')
    evidence = '.exr -1; .ecxr; r; kv; lm' + ('; '+header if steam_heap else '')
    first = ('.echo S7_FIRST_CHANCE_AV; ' + evidence + '; '
             '.if (@$t0 < 3) { r @$t0 = @$t0 + 1; '
             '.dump /m /u first-chance.dmp; gn } .else { qd }')
    second = ('.echo S7_SECOND_CHANCE_AV; ' + evidence + '; '
              '.if (@$t0 < 4) { r @$t0 = @$t0 + 1; '
              '.dump /m /u second-chance.dmp }; qd')
    return ('r @$t0 = 0\n' + f'sxe -c "{first}" -c2 "{second}" av\n'
            '.echo S7_OWNED_CRASH_WATCH_READY\ng\n')


def arguments(cdb, pid):
    if type(pid) is not int or not 1 <= pid <= 0xffffffff:
        raise ValueError('Invalid admitted PID.')
    # Startup file and all debugger filenames are relative to the fresh cwd.
    return [str(cdb), '-pd', '-noshell', '-nosqm', '-hd', '-G', '-sins',
            '-y', 'symbols', '-srcpath', 'sources', '-netsyms:no',
            '-cf', 'watch.cmd', '-p', str(pid)]


def environment(inherited):
    return {k: v for k, v in inherited.items()
            if not k.upper().startswith(('_NT_', '_NTDEBUG', 'DBG', 'CDB', 'WINDBG'))}


class Native:
    """Read-only process/module identity, plus verification of pinned CDB code."""
    def __init__(self):
        if os.name != 'nt' or C.sizeof(C.c_void_p) != 8:
            raise RuntimeError('Crash watch requires the Windows x64 controller.')
        sys.path.insert(0, str(ROOT / 'tools/installer'))
        import isolated_process
        self.api = isolated_process.Windows()
        self.k = self.api.k
        for name, args, ret in (
                ('GetProcessId', [W.HANDLE], W.DWORD),
                ('GetProcessTimes', [W.HANDLE] + [C.POINTER(W.FILETIME)] * 4, W.BOOL),
                ('Module32FirstW', [W.HANDLE, C.c_void_p], W.BOOL),
                ('Module32NextW', [W.HANDLE, C.c_void_p], W.BOOL)):
            getattr(self.k, name).argtypes = args
            getattr(self.k, name).restype = ret

    def open(self, pid):
        # QUERY_LIMITED_INFORMATION | SYNCHRONIZE; never TERMINATE/VM_WRITE.
        return self.api.check(self.k.OpenProcess(0x101000, False, pid))

    def close(self, handle):
        self.k.CloseHandle(handle)

    def state(self, handle):
        wait = self.k.WaitForSingleObject(handle, 0)
        if wait not in (0, 258):
            raise RuntimeError('Owned process state query failed.')
        code = W.DWORD()
        self.api.check(self.k.GetExitCodeProcess(handle, C.byref(code)))
        return {'alive': wait == 258, 'exit_code': None if wait == 258 else code.value}

    def wait_exit(self, handle, milliseconds):
        wait = self.k.WaitForSingleObject(handle, milliseconds)
        if wait not in (0, 258):
            raise RuntimeError('Original process exit wait failed.')
        return wait == 0

    def identity(self, pid, handle):
        if self.k.GetProcessId(handle) != pid or not self.state(handle)['alive']:
            raise RuntimeError('The original admitted process is no longer active.')
        rows = [p for p in self.api.processes() if p.pid == pid]
        if len(rows) != 1:
            raise RuntimeError('Ambiguous original process identity.')
        times = [W.FILETIME() for _ in range(4)]
        self.api.check(self.k.GetProcessTimes(handle, *(C.byref(t) for t in times)))
        path = C.create_unicode_buffer(32768); size = W.DWORD(len(path))
        self.api.check(self.k.QueryFullProcessImageNameW(handle, 0, path, C.byref(size)))
        path = recover.plain(path.value)
        return dict(pid=pid, parent=rows[0].parent, path=str(path), sha256=digest(path),
                    creation_time=times[0].dwLowDateTime | times[0].dwHighDateTime << 32)

    def modules(self, pid):
        class Entry(C.Structure):
            _fields_ = [('size', W.DWORD), ('id', W.DWORD), ('pid', W.DWORD),
                        ('global_usage', W.DWORD), ('process_usage', W.DWORD),
                        ('base', C.c_void_p), ('image_size', W.DWORD),
                        ('module', W.HMODULE), ('name', W.WCHAR * 256),
                        ('path', W.WCHAR * 260)]
        snapshot = self.k.CreateToolhelp32Snapshot(0x18, pid)
        if snapshot == C.c_void_p(-1).value:
            self.api.check(False)
        result = []
        try:
            entry = Entry(); entry.size = C.sizeof(entry)
            found = self.k.Module32FirstW(snapshot, C.byref(entry))
            while found:
                result.append(recover.plain(entry.path))
                found = self.k.Module32NextW(snapshot, C.byref(entry))
            if C.get_last_error() != 18:
                self.api.check(False)
            return result
        finally:
            self.close(snapshot)

    @staticmethod
    def trust(path):
        class GUID(C.Structure):
            _fields_ = [('a', W.DWORD), ('b', W.WORD), ('c', W.WORD), ('d', C.c_ubyte * 8)]
        class File(C.Structure):
            _fields_ = [('size', W.DWORD), ('path', W.LPCWSTR), ('handle', W.HANDLE), ('subject', C.c_void_p)]
        class Data(C.Structure):
            _fields_ = [('size', W.DWORD), ('policy', C.c_void_p), ('sip', C.c_void_p),
                        ('ui', W.DWORD), ('revocation', W.DWORD), ('choice', W.DWORD),
                        ('file', C.POINTER(File)), ('action', W.DWORD), ('state', W.HANDLE),
                        ('url', W.LPCWSTR), ('flags', W.DWORD), ('context', W.DWORD), ('settings', C.c_void_p)]
        action = GUID(0x00aac56b, 0xcd44, 0x11d0, (C.c_ubyte * 8)(0x8c, 0xc2, 0, 0xc0, 0x4f, 0xc2, 0x95, 0xee))
        file = File(C.sizeof(File), str(path), None, None)
        data = Data(); data.size = C.sizeof(Data); data.ui = 2; data.choice = 1
        data.file = C.pointer(file); data.flags = 0x1000  # offline cache-only, no UI/network
        api = C.WinDLL('wintrust', use_last_error=True).WinVerifyTrust
        api.argtypes = [W.HWND, C.POINTER(GUID), C.POINTER(Data)]; api.restype = C.c_long
        if api(None, C.byref(action), C.byref(data)) != 0:
            raise RuntimeError('Pinned Microsoft Authenticode trust check failed.')


def verify_cdb(path, native):
    path = recover.lab(path)
    if path.name != 'cdb.exe' or not path.is_file():
        raise ValueError('Use the pinned x86 Microsoft CDB payload.')
    actual = {p.name for p in path.parent.iterdir() if p.suffix.lower() in ('.exe', '.dll')}
    if actual != set(CDB_PINS) or any(p.is_dir() for p in path.parent.iterdir()):
        raise ValueError('Unexpected debugger code or extension directory.')
    for name, pin in CDB_PINS.items():
        file = recover.plain(path.parent / name)
        if digest(file) != pin:
            raise ValueError('Debugger binary pin changed.')
        native.trust(file)
    return path


def _hook(case, dll):
    from . import load_hook
    dll, manifest = load_hook.verify_build(dll)
    initialized = _json(case / 'hook-initialized.private.json')
    if (initialized.get('succeeded') is not True or initialized.get('initialized') is not True
            or initialized.get('original_entry_guarded') is not True
            or initialized.get('dll_sha256') != digest(dll)
            or _json(case / 'hook-build.private.json') != manifest):
        raise ValueError('The admitted source-bound hook is not the initialized case DLL.')
    return dll


def _match(expected, current):
    if (not isinstance(expected, dict) or set(expected) != {'pid', 'parent', 'path', 'sha256', 'creation_time'}
            or any(type(expected[k]) is not int or expected[k] <= 0 for k in ('pid', 'parent', 'creation_time'))
            or not re.fullmatch('[0-9a-f]{64}', expected.get('sha256', ''))
            or expected != current):
        raise RuntimeError('PID/parent/path/hash/CreateTime no longer identifies the original child.')


def admit(owned, case, dll=None, *, load_and_new_save_confirmed=False):
    """Owner-process-only admission; no CLI can manufacture a PID admission."""
    if load_and_new_save_confirmed is not True:
        raise ValueError('Admit only after original-UI load and a new private save.')
    owned.owned()
    if owned.poll() is not None or not owned.native_initialized:
        raise ValueError('The initialized owned child must still be active.')
    case, root = _case(case)
    record = _json(case / 'owned.private.json')
    if (record.get('pid') != owned.pid or record.get('parent') != os.getpid()
            or record.get('path') != str(owned.executable) or record.get('sha256') != owned.expected
            or record.get('native_initialized') is not True or not owned.executable.is_relative_to(root)):
        raise ValueError('The active case is not owned by this retained child handle.')
    native = Native()
    modules = native.modules(owned.pid)
    if dll is None:
        pin = _json(case / 'hook-initialized.private.json').get('dll_sha256')
        matching = [p for p in modules if p.name == 'WePlaySupersamplingLab.dll' and digest(p) == pin]
        if len(matching) != 1:
            raise RuntimeError('The exact initialized hook module is ambiguous or absent.')
        dll = matching[0]
    dll = _hook(case, dll)
    child = native.identity(owned.pid, owned._handle)
    parent_handle = native.open(os.getpid())
    try:
        parent = native.identity(os.getpid(), parent_handle)
    finally:
        native.close(parent_handle)
    if child['parent'] != parent['pid'] or child['creation_time'] < parent['creation_time']:
        raise RuntimeError('Invalid owner/child creation ordering.')
    if dll not in modules:
        raise RuntimeError('The exact initialized DLL is not loaded in this child.')
    files = {name: digest(case / name) for name in (
        recover.JOURNAL, 'owned.private.json', 'checkpoint.private.json',
        'hook-initialized.private.json', 'hook-build.private.json')}
    value = dict(format=FORMAT, case=str(case), child=child, parent=parent,
                 hook=str(dll), hook_sha256=digest(dll), case_files=files,
                 watcher_sha256=digest(__file__), load_and_new_save_confirmed=True)
    _write_new(case / ADMISSION, value)
    return value


def _admitted(case, native):
    case, root = _case(case)
    value = _json(case / ADMISSION)
    expected = {'format', 'case', 'child', 'parent', 'hook', 'hook_sha256', 'case_files',
                'watcher_sha256', 'load_and_new_save_confirmed'}
    if (set(value) != expected or value['format'] != FORMAT or value['case'] != str(case)
            or value['load_and_new_save_confirmed'] is not True or value['watcher_sha256'] != digest(__file__)
            or set(value['case_files']) != {recover.JOURNAL, 'owned.private.json', 'checkpoint.private.json',
                                          'hook-initialized.private.json', 'hook-build.private.json'}):
        raise ValueError('Missing or changed original-handle crash admission.')
    for name, pin in value['case_files'].items():
        if digest(case / name) != pin:
            raise ValueError('The admitted case changed or was recovered/closed.')
    owned_record = _json(case / 'owned.private.json')
    if (any(value['child'].get(k) != owned_record.get(k) for k in ('pid', 'parent', 'path', 'sha256'))
            or owned_record.get('native_initialized') is not True
            or value['child']['parent'] != value['parent']['pid']
            or value['child']['creation_time'] < value['parent']['creation_time']):
        raise ValueError('Admission does not describe the original case child and owner.')
    executable = recover.plain(value['child']['path'])
    if (not executable.is_relative_to(root) or executable.name.lower() != 'settlers7r.exe'
            or value['child']['sha256'] not in recover.EXES):
        raise ValueError('Admission cannot select another executable or game copy.')
    dll = _hook(case, value['hook'])
    if digest(dll) != value['hook_sha256']:
        raise ValueError('The initialized hook changed.')
    child_handle = parent_handle = None
    try:
        child_handle = native.open(value['child']['pid'])
        parent_handle = native.open(value['parent']['pid'])
        _match(value['child'], native.identity(value['child']['pid'], child_handle))
        _match(value['parent'], native.identity(value['parent']['pid'], parent_handle))
        if value['child']['parent'] != value['parent']['pid'] or dll not in native.modules(value['child']['pid']):
            raise RuntimeError('Original parent or loaded hook changed.')
        value['game_root'] = str(root)
        return case, value, child_handle, parent_handle
    except BaseException:
        if child_handle: native.close(child_handle)
        if parent_handle: native.close(parent_handle)
        raise


def usage(output):
    files = [p for p in output.iterdir() if p.is_file()]
    return dict(dumps=sum(p.suffix.lower() == '.dmp' for p in files),
                bytes=sum(p.stat().st_size for p in files))


def av_event_counts(raw):
    """Count emitted marker lines, never echoed debugger command strings."""
    if not isinstance(raw, bytes) or len(raw) > MAX_BYTES:
        return None
    lines = raw.splitlines()
    return dict(first_chance=lines.count(b'S7_FIRST_CHANCE_AV'),
                second_chance=lines.count(b'S7_SECOND_CHANCE_AV'))


def stop_debugger(proc, identity, native):
    """Try detach first; the sole terminate target is our retained pinned CDB."""
    result = {'qd_attempted': False, 'debugger_terminated': False}
    if proc.poll() is not None:
        return result
    _match(identity, native.identity(proc.pid, int(proc._handle)))
    result['qd_attempted'] = True
    try:
        proc.stdin.write(b'qd\n'); proc.stdin.flush()
    except (OSError, ValueError):
        pass
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        _match(identity, native.identity(proc.pid, int(proc._handle)))
        if identity['sha256'] != CDB_PINS['cdb.exe'] or identity['parent'] != os.getpid():
            raise RuntimeError('Refuse termination of an unowned debugger.')
        proc.terminate()  # retained CDB Popen handle only; -pd is mandatory
        result['debugger_terminated'] = True
        proc.wait(timeout=5)
    return result


def final_target(native, handle, expected, *, debugger_exited_naturally):
    """Resolve exit races using only the retained original process handle.

    An identity mismatch is always fatal. A failed identity *query* is accepted
    only after that same handle is signalled and its exit code is available.
    This never reopens a PID or accepts a still-active ambiguous process.
    """
    race = False
    if debugger_exited_naturally:
        native.wait_exit(handle, 500)
    state = native.state(handle)
    if state['alive']:
        try:
            current = native.identity(expected['pid'], handle)
        except (OSError, RuntimeError):
            if not native.wait_exit(handle, 500):
                raise
            state = native.state(handle)
            if state['alive']:
                raise
            race = True
        else:
            _match(expected, current)  # never treat an identity mismatch as an exit race
    return state, race


def watch(case, output, *, cdb=DEFAULT_CDB, seconds=300):
    if type(seconds) is not int or not 300 <= seconds <= 600:
        raise ValueError('Crash watch is bounded to300–600 seconds.')
    native = Native()
    case, admission, child, parent = _admitted(case, native)
    proc = None
    result = dict(format='s7-owned-crash-watch-v1', diagnostic_not_benchmark=True,
                  game_terminated_by_watcher=False, limit_bytes=MAX_BYTES, limit_dumps=MAX_DUMPS)
    debugger_identity = None
    output_created = False
    try:
        output = recover.lab(output)
        if output.exists() or not output.parent.is_dir() or output.is_relative_to(Path(admission['game_root'])):
            raise ValueError('Use a fresh separate work directory, never the game binary directory.')
        cdb = verify_cdb(cdb, native)
        output.mkdir()
        output_created = True
        (output / 'symbols').mkdir(); (output / 'sources').mkdir()
        with (output / 'watch.cmd').open('x', encoding='ascii', newline='\n') as stream:
            stream.write(commands(steam_heap=admission['child']['sha256']==STEAM_EXE))
        args = arguments(cdb, admission['child']['pid'])
        result.update(case=str(case), original=admission['child'], command=args,
                      cdb_sha256=CDB_PINS['cdb.exe'], target_before=native.state(child))
        _write_new(output / 'watch-start.private.json', result)
        from .load_hook import file_lease
        with ExitStack() as leases:
            for name in CDB_PINS:
                leases.enter_context(file_lease(cdb.parent / name))
            leases.enter_context(file_lease(Path(admission['hook'])))
            # Recheck all identities after acquiring deny-write leases.
            verify_cdb(cdb, native)
            _match(admission['child'], native.identity(admission['child']['pid'], child))
            _match(admission['parent'], native.identity(admission['parent']['pid'], parent))
            with (output / 'cdb.private.log').open('xb') as log:
                proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT,
                    cwd=output, env=environment(os.environ), shell=False, creationflags=0x08000000)
                debugger_identity = native.identity(proc.pid, int(proc._handle))
                if debugger_identity['path'] != str(cdb) or debugger_identity['sha256'] != CDB_PINS['cdb.exe']:
                    raise RuntimeError('The spawned CDB identity changed.')
                deadline = time.monotonic() + seconds
                reason = 'debugger_exited'
                while proc.poll() is None:
                    used = usage(output)
                    if used['dumps'] >= MAX_DUMPS or used['bytes'] >= MAX_BYTES:
                        reason = 'output_limit'; break
                    if time.monotonic() >= deadline:
                        reason = 'timeout'; break
                    time.sleep(.05)
                result['stop_reason'] = reason
                result['detach'] = stop_debugger(proc, debugger_identity, native)
                result['debugger_exit_code'] = proc.poll()
        state, race = final_target(native, child, admission['child'],
            debugger_exited_naturally=result['stop_reason'] == 'debugger_exited')
        result.update(target_after=state, target_identity_query_exit_race=race, usage=usage(output))
        with (output / 'cdb.private.log').open('rb') as stream:
            result['av_events'] = av_event_counts(stream.read(MAX_BYTES + 1))
        result['output_limit_overshoot'] = result['usage']['bytes'] > MAX_BYTES
        # Filesystem polling is an operational cutoff, not an OS disk quota:
        # an individual in-progress minidump can exceed64MiB before detachment.
        _write_new(output / 'watch-result.private.json', result)
        return result
    except BaseException as error:
        if output_created:
            result['error_type'] = type(error).__name__
            try:
                result.update(target_after=native.state(child), usage=usage(output))
                _write_new(output / 'watch-error.private.json', result)
            except (OSError, ValueError, RuntimeError):
                pass
        raise
    finally:
        try:
            if proc is not None and proc.poll() is None:
                if debugger_identity is None:
                    # Recover identity from the retained own Popen handle if
                    # the initial post-spawn snapshot raced an early exit.
                    debugger_identity = native.identity(proc.pid, int(proc._handle))
                stop_debugger(proc, debugger_identity, native)
        finally:
            if proc is not None and proc.stdin is not None:
                proc.stdin.close()
            native.close(child); native.close(parent)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cdb', type=Path, default=DEFAULT_CDB)
    parser.add_argument('--seconds', type=int, default=300)
    args = parser.parse_args(argv)
    try:
        print(json.dumps(watch(args.case, args.output, cdb=args.cdb, seconds=args.seconds), indent=2))
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(2, f'Crash watch refused: {error}\n')


if __name__ == '__main__':
    main()
