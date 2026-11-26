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
import mmap
import os
from pathlib import Path
import re
import subprocess
import struct
import sys
import time

from . import recover

ROOT = Path(__file__).resolve().parents[2]
ADMISSION = 'crash-admission.private.json'
FORMAT = 's7-owned-crash-admission-v1'
DEFAULT_CDB = ROOT / 'work/debugger-acquisition-20261124/portable/x86/cdb.exe'
MAX_BYTES = 64 * 1024 * 1024
MAX_DUMPS = 4
BASELINE_MODE = 'owned-sdk-without-native-graphics-v1'
BASELINE_FORMAT = 's7-owned-baseline-crash-admission-v1'
BASELINE_ADMISSION = 'baseline-crash-admission.private.json'
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


def baseline_source_paths(*,metric=False):
    """Fixed source set; relocating/promoting this draft needs a NEW checkpoint."""
    common = {
        'tools/graphics_lab/game.py', 'tools/graphics_lab/crash_watch.py',
        'tools/graphics_lab/baseline_modules.py',
        'tools/installer/platform_standalone.py', 'tools/installer/isolated_process.py',
        'tools/d3d11/memory_guard.py', 'tools/shadow_detail/profile_guard.py',
        'tools/graphics_lab/recover.py', 'tools/graphics_lab/load_hook.py',
        'tools/supersampling/hook_probe.py',
    }
    if type(metric)is not bool:raise ValueError('Explicit boolean Metric source mode required.')
    return common|{'tools/graphics_lab/baseline_metric_profile.py'}if metric else common


def _baseline_metric_requested(case):
    value=_json(case/'checkpoint.private.json').get('baseline_metric_profile_requested',False)
    if type(value)is not bool:raise ValueError('Explicit boolean Metric owner intent required.')
    return value


def _baseline_native():
    # Lazy import: the dedicated provider inherits Native, never replaces it.
    from .baseline_modules import BaselineNative
    return BaselineNative()


def _baseline_module_proof(native, child):
    """Retain bounded source-provider evidence; never accept filename aliases."""
    from .baseline_modules import same_nt_file, driverstore_candidate
    rows, aliases = native.module_rows, native.alias_proofs
    if type(rows) is not list or not rows or len(rows) > 512 or type(aliases) is not list or len(aliases) > len(rows):
        raise ValueError('Missing bounded original baseline module proof.')
    row_keys = {'pid','base','image_bytes','reported_path','resolved_path'}
    for row in rows:
        if (not isinstance(row,dict) or set(row)!=row_keys or row['pid']!=child['pid']
                or type(row['base']) is not int or not 0 < row['base'] <= 0xffffffffffffffff
                or type(row['image_bytes']) is not int or not 0 < row['image_bytes'] <= 0xffffffff
                or any(not isinstance(row[key],str) or not row[key] or len(row[key])>=32768
                       for key in ('reported_path','resolved_path'))):
            raise ValueError('Malformed baseline module row evidence.')
        recover.plain(row['reported_path']); recover.plain(row['resolved_path'])
    keys = {'original_identity','module_base','image_bytes','reported_path','resolved_path','mapped_nt','final_nt',
            'file_sha256','file_identity','method','read_only'}
    selected = set()
    for alias in aliases:
        if (not isinstance(alias,dict) or set(alias)!=keys or alias['original_identity']!=child
                or alias['method']!='original-map-equals-opened-file-VOLUME_NAME_NT' or alias['read_only'] is not True
                or not same_nt_file(alias['mapped_nt'],alias['final_nt'])
                or not isinstance(alias['file_sha256'],str) or not re.fullmatch('[0-9a-f]{64}',alias['file_sha256'])):
            raise ValueError('Malformed baseline alias mapping proof.')
        info=alias['file_identity']
        if (not isinstance(info,dict) or set(info)!={'volume_serial','file_index','bytes'}
                or any(type(value) is not int or not 0 <= value <= 0xffffffffffffffff for value in info.values())
                or not info['bytes']):
            raise ValueError('Malformed baseline alias file identity.')
        matches=[row for row in rows if hex(row['base'])==alias['module_base']
                 and row['image_bytes']==alias['image_bytes']
                 and recover.plain(row['reported_path'])==recover.plain(alias['reported_path'])
                 and recover.plain(row['resolved_path'])==recover.plain(alias['resolved_path'])]
        key=(alias['module_base'],recover.plain(alias['reported_path']))
        if len(matches)!=1 or key in selected:
            raise ValueError('Alias proof does not identify one original module row.')
        selected.add(key)
        reported=recover.plain(alias['reported_path']);resolved=recover.plain(alias['resolved_path'])
        # The provider proves the actual Windows directory and exact NT mapping.
        # This retained record must also describe the same narrow path shape.
        parts=reported.parts
        positions=[i for i,part in enumerate(parts) if part.casefold()=='syswow64']
        if len(positions)!=1:
            raise ValueError('Alias record is outside its narrow Windows subtree.')
        windows=Path(*parts[:positions[0]])
        if driverstore_candidate(reported,windows)!=resolved or digest(resolved)!=alias['file_sha256']:
            raise ValueError('Alias record resolved file identity changed.')
    differing={(hex(row['base']),recover.plain(row['reported_path'])) for row in rows
               if recover.plain(row['reported_path'])!=recover.plain(row['resolved_path'])}
    if differing!=selected:
        raise ValueError('Resolved module lacks an exact alias mapping proof.')
    return {'module_rows':rows,'module_alias_proofs':aliases}


def _baseline_repeat_aliases(admission, proof):
    if proof['module_alias_proofs']!=admission['module_alias_proofs']:
        raise RuntimeError('Baseline alias mapping changed after owner admission.')


def baseline_options(*, baseline, crash_watch, hook, callback, observer, capture,
                     materials, comparison, detailed_shadows, initially_off, config_version):
    if (baseline is not True or crash_watch is not True or hook is not None or callback is not None
            or observer is not None or capture is not None or not isinstance(materials,(tuple,list)) or materials or comparison is not False
            or detailed_shadows is not False or initially_off is not False or type(config_version)is not int or config_version != 1):
        raise ValueError('Explicit baseline requires no graphics hook/callback/observation/material/comparison mode.')


def baseline_command(raw, previous):
    if not isinstance(raw, bytes) or len(raw) > 4096:
        raise ValueError('Baseline command exceeds its bound.')
    def pairs(items):
        value = {}
        for key, item in items:
            if key in value: raise ValueError('Duplicate baseline command key.')
            value[key] = item
        return value
    value = json.loads(raw, object_pairs_hook=pairs)
    if (not isinstance(value, dict) or set(value) != {'operation', 'sequence'}
            or value['operation'] != 'baseline-crash-watch-admit'
            or type(value['sequence']) is not int or not 1 <= value['sequence'] <= 1000000):
        raise ValueError('Baseline permits only the explicit owner admission command.')
    return value if value['sequence'] > previous else None


def _platform(root):
    sys.path.insert(0, str(ROOT / 'tools/installer'))
    import platform_standalone
    return platform_standalone.load(root)  # read-only original adapter/runtime/isolation verification


def _baseline_case(case):
    case = recover.lab(case)
    journal = _json(case / recover.JOURNAL)
    if (set(journal) != {'format', 'state', 'root', 'critical', 'targets', 'shared', 'attempts'}
            or journal['format'] != recover.FORMAT or journal['state'] != 'prepared'
            or type(journal['attempts']) is not int or journal['attempts'] != 0
            or (case / 'result.private.json').exists()):
        raise ValueError('Baseline requires an active fresh prepared recovery journal.')
    root = recover.lab(journal['root'])
    if case.is_relative_to(root) or root.is_relative_to(case):
        raise ValueError('Baseline case and owned game root must remain separate.')
    parent = recover._critical(root, journal['critical'])
    # Verify all recovery targets/backups and shared snapshots, never restore.
    targets = journal['targets']
    if not isinstance(targets, dict) or set(targets) != {'options', 'runtime', 'config'}:
        raise ValueError('Baseline recovery targets changed.')
    if any(not isinstance(item,dict) or set(item)!={'relative','original'} or not isinstance(item.get('relative'),str) for item in targets.values()):
        raise ValueError('Baseline recovery target structure changed.')
    recover._targets(root, {name: root / item['relative'] for name, item in targets.items()}, parent)
    recover._shared(journal['shared'])
    for name, item in targets.items():
        if not isinstance(item, dict) or set(item) != {'relative', 'original'}:
            raise ValueError('Baseline recovery target record changed.')
        record = item['original'];backup = case / 'recovery-original' / (name + '.bin')
        if not isinstance(record, dict) or type(record.get('exists')) is not bool:
            raise ValueError('Baseline backup record is invalid.')
        if record['exists']:
            _, actual = recover._read(backup, recover.MAX_SETTING)
            if actual != record: raise ValueError('Baseline original settings backup changed.')
        elif record != {'exists': False} or backup.exists():
            raise ValueError('Unexpected baseline settings backup.')
    checkpoint = _json(case / 'checkpoint.private.json')
    if (checkpoint.get('files') != journal['critical'] or checkpoint.get('baseline_mode') != BASELINE_MODE
            or checkpoint.get('baseline_crash_watch_allowed') is not True
            or checkpoint.get('crash_watch_allowed') is not False
            or checkpoint.get('native_graphics_hook_requested') is not False
            or checkpoint.get('comparison_uv_requested') is not False
            or checkpoint.get('detailed_shadows_requested') is not False
            or checkpoint.get('material_effects_requested') != []):
        raise ValueError('Explicit baseline source/mode checkpoint is absent or contradictory.')
    sources = checkpoint.get('sources')
    if not isinstance(sources, dict) or set(sources) != baseline_source_paths(metric=_baseline_metric_requested(case)):
        raise ValueError('Baseline requires every exact private/controller source checkpoint.')
    if sources.get(Path(__file__).resolve().relative_to(ROOT).as_posix())!=digest(__file__):
        raise ValueError('Baseline did not checkpoint this exact executing watcher.')
    for relative, pin in sources.items():
        if not isinstance(pin, str) or not re.fullmatch('[0-9a-f]{64}', pin):
            raise ValueError('Invalid baseline source digest.')
        if digest(recover.plain(ROOT / relative)) != pin or digest(recover.plain(case / 'source' / relative)) != pin:
            raise ValueError('Baseline current or retained source checkpoint changed.')
    # A case that previously initialized graphics is not a no-graphics baseline,
    # even if somebody unloaded its DLL before admission.
    if any((case / name).exists() for name in ('hook-initialized.private.json', 'hook-build.private.json',
            'effects-configured.private.json', 'comparison-configured.private.json', 'material-stage.private.json')):
        raise ValueError('A graphics-initialized case cannot become a baseline.')
    platform, executable, adapter = _platform(root)
    manifest = root / 'Launcher/platform-standalone.json'
    if (str(executable.relative_to(root)).replace('\\', '/') not in journal['critical']
            or platform['files'].get(adapter.relative_to(root).as_posix()) != journal['critical'].get(adapter.relative_to(root).as_posix())):
        raise ValueError('Baseline adapter/runtime manifest disagrees with critical pins.')
    required = dict(journal['critical']);required.update(platform['files'])
    for relative, pin in required.items():
        if digest(recover.plain(root / relative)) != pin: raise ValueError('Baseline critical/runtime binary changed.')
    return case, root, required, manifest


def graphics_export_present(path):
    """Bounded read-only PE exports; catches a graphics DLL with another name."""
    path = recover.plain(path)
    if not path.is_file() or not 256 <= path.stat().st_size <= 512 * 1024 * 1024:
        raise ValueError('Cannot verify baseline module export absence.')
    with path.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        def unpack(fmt, offset):
            size = struct.calcsize(fmt)
            if offset < 0 or offset + size > len(data): raise ValueError('Truncated baseline PE metadata.')
            return struct.unpack_from(fmt, data, offset)
        if data[:2] != b'MZ': raise ValueError('Invalid baseline module PE.')
        pe = unpack('<I', 0x3c)[0]
        if pe > 1024 * 1024 or data[pe:pe+4] != b'PE\0\0': raise ValueError('Invalid baseline PE header.')
        sections = unpack('<H', pe + 6)[0];optional = pe + 24;optional_size = unpack('<H', pe + 20)[0]
        magic = unpack('<H', optional)[0]
        directory = optional + (96 if magic == 0x10b else 112 if magic == 0x20b else 0)
        if not 1 <= sections <= 96 or directory == optional or optional_size < directory - optional + 8:
            raise ValueError('Unsupported baseline PE export metadata.')
        export_rva, export_size = unpack('<II', directory)
        if not export_rva and not export_size:return False
        headers = unpack('<I', optional + 60)[0];table = optional + optional_size
        ranges = []
        for index in range(sections):
            virtual_size, address, raw_size, raw_offset = unpack('<4I', table + index * 40 + 8)
            if raw_offset + raw_size > len(data):raise ValueError('Invalid baseline PE section.')
            ranges.append((address, raw_size, raw_offset))
        def offset(rva, size):
            if rva < headers and rva + size <= min(headers, len(data)):return rva, min(headers, len(data)) - rva
            for address, raw_size, raw_offset in ranges:
                if address <= rva and rva + size <= address + raw_size:return raw_offset + rva - address, address + raw_size - rva
            raise ValueError('Baseline export RVA is outside raw module data.')
        at, _ = offset(export_rva, 40);count = unpack('<I', at + 24)[0];names = unpack('<I', at + 32)[0]
        if count > 65536:raise ValueError('Baseline export name table exceeds bound.')
        if not count:return False
        table_at, _ = offset(names, count * 4)
        for index in range(count):
            name_rva = unpack('<I', table_at + index * 4)[0];name_at, available = offset(name_rva, 1)
            raw = data[name_at:name_at + min(1024, available)];end = raw.find(b'\0')
            if end < 0:raise ValueError('Unbounded baseline export name.')
            name = raw[:end].lower()
            if b'weplaysupersampling' in name or b'weplaynativegraphics' in name:return True
    return False


def _baseline_paths(modules,allowed_executables):
    # Toolhelp reports two mappings of the same pinned original Steam EXE.
    # Absence checks scan files, never select a function address from these rows.
    # Retain both rows and allow that observed case only; all other duplicate
    # identities, excessive counts and unknown snapshot shapes remain refused.
    if not isinstance(modules,(list,tuple,set,frozenset)) or not modules or len(modules)>512:
        raise RuntimeError('Ambiguous baseline module snapshot.')
    paths=[recover.plain(path) for path in modules]
    counts={}
    for path in paths:counts[path]=counts.get(path,0)+1
    if any(count>1 and (count!=2 or path not in allowed_executables or path.name.casefold()!='settlers7r.exe')
           for path,count in counts.items()):raise RuntimeError('Ambiguous baseline module snapshot.')
    return paths


def _baseline_modules(native, pid, root, required):
    allowed={recover.plain(root/name) for name,pin in required.items()
             if Path(name).name.casefold()=='settlers7r.exe' and pin in recover.EXES}
    paths = _baseline_paths(native.modules(pid),allowed)
    for relative, pin in required.items():
        path = recover.plain(root / relative)
        if path.suffix.lower() in ('.dll', '.exe') and path not in paths:
            # The policy executable is a separate owner-controlled service,
            # never expected as a module inside the game.
            if relative == 'Launcher/weplay_network_policy.exe':continue
            raise RuntimeError('A pinned baseline executable/runtime module is absent.')
        if digest(path) != pin:raise RuntimeError('Baseline module binary pin changed.')
    for path in paths:
        name = path.name.casefold()
        if 'nativegraphics'in name or ('weplay'in name and 'lab'in name) or graphics_export_present(path):
            raise RuntimeError('A native graphics/laboratory module is loaded in this purported baseline.')
    return paths


def _baseline_absence(native,pid,verified):
    """New modules cannot introduce graphics after baseline admission.

    Do not demand critical modules stay loaded during normal engine shutdown.
    Their on-disk files remain pinned/leased; only new module exports need scans.
    """
    allowed={path for path in verified if path.name.casefold()=='settlers7r.exe'}
    for path in _baseline_paths(native.modules(pid),allowed):
        name=path.name.casefold()
        if 'nativegraphics'in name or ('weplay'in name and 'lab'in name):raise RuntimeError('Graphics module appeared after baseline admission.')
        if path not in verified:
            if graphics_export_present(path):raise RuntimeError('Renamed graphics module appeared after baseline admission.')
            verified.add(path)


def baseline_admit(owned, case, *, load_and_new_save_confirmed=False):
    """Only the original retaining owner can create this separate admission."""
    if load_and_new_save_confirmed is not True:
        raise ValueError('Baseline admission requires original-UI load and a new private save.')
    if getattr(owned, '_weplay_owned', None) is not True:
        raise ValueError('Baseline admission requires the original retained OwnedProcess.')
    owned.owned()
    if owned.poll() is not None or owned.native_initialized is not True:
        raise ValueError('The startup-adapter initialized owned baseline must still be active.')
    case, root, required, manifest = _baseline_case(case)
    record = _json(case / 'owned.private.json')
    if (record.get('pid') != owned.pid or record.get('parent') != os.getpid()
            or record.get('path') != str(owned.executable) or record.get('sha256') != owned.expected
            or record.get('native_initialized') is not True or not owned.executable.is_relative_to(root)):
        raise ValueError('Baseline record is not this original retained child.')
    native = _baseline_native();child = native.identity(owned.pid, owned._handle)
    if any(child.get(key) != record.get(key) for key in ('pid','parent','path','sha256')):
        raise RuntimeError('Retained baseline child identity differs from its owner record.')
    native.bind_original(child, owned._handle)
    modules = _baseline_modules(native, owned.pid, root, required)
    proof = _baseline_module_proof(native, child)
    handle = native.open(os.getpid())
    try:parent = native.identity(os.getpid(), handle)
    finally:native.close(handle)
    if child['parent'] != parent['pid'] or child['creation_time'] < parent['creation_time']:
        raise RuntimeError('Invalid baseline owner/child creation ordering.')
    files = {name:digest(case/name)for name in (recover.JOURNAL,'owned.private.json','checkpoint.private.json')}
    value = dict(format=BASELINE_FORMAT,mode=BASELINE_MODE,case=str(case),child=child,parent=parent,
        case_files=files,watcher_sha256=digest(__file__),required_binaries=required,
        platform_manifest_sha256=digest(manifest),modules=[str(path)for path in modules],
        **proof,
        native_graphics_modules_absent=True,load_and_new_save_confirmed=True)
    if _baseline_metric_requested(case):
        from . import baseline_metric_profile
        value['baseline_metric_profile']=baseline_metric_profile.prepare_owner_profile(case,native,child)
    _write_new(case/BASELINE_ADMISSION,value);return value


def _baseline_admitted(case, native):
    case,root,required,manifest = _baseline_case(case);value=_json(case/BASELINE_ADMISSION)
    keys={'format','mode','case','child','parent','case_files','watcher_sha256','required_binaries',
           'platform_manifest_sha256','modules','module_rows','module_alias_proofs',
           'native_graphics_modules_absent','load_and_new_save_confirmed'}
    if _baseline_metric_requested(case):keys.add('baseline_metric_profile')
    if (set(value)!=keys or value['format']!=BASELINE_FORMAT or value['mode']!=BASELINE_MODE
            or value['case']!=str(case) or value['watcher_sha256']!=digest(__file__)
            or value['required_binaries']!=required or value['platform_manifest_sha256']!=digest(manifest)
            or value['native_graphics_modules_absent']is not True or value['load_and_new_save_confirmed']is not True
            or set(value['case_files'])!={recover.JOURNAL,'owned.private.json','checkpoint.private.json'}):
        raise ValueError('Missing/changed explicit baseline owner admission.')
    for name,pin in value['case_files'].items():
        if digest(case/name)!=pin:raise ValueError('Baseline case changed or closed after admission.')
    record=_json(case/'owned.private.json')
    if (any(value['child'].get(k)!=record.get(k)for k in ('pid','parent','path','sha256'))
            or record.get('native_initialized')is not True or value['child']['parent']!=value['parent']['pid']
            or value['child']['creation_time']<value['parent']['creation_time']):
        raise ValueError('Baseline admission no longer names its original owner and child.')
    executable=recover.plain(value['child']['path'])
    if not executable.is_relative_to(root)or executable.name.casefold()!='settlers7r.exe'or value['child']['sha256']not in recover.EXES:
        raise ValueError('Baseline admission cannot select another executable.')
    child=parent=None
    try:
        child=native.open(value['child']['pid']);parent=native.open(value['parent']['pid'])
        _match(value['child'],native.identity(value['child']['pid'],child));_match(value['parent'],native.identity(value['parent']['pid'],parent))
        native.bind_original(value['child'], child)
        _baseline_modules(native,value['child']['pid'],root,required)
        proof = _baseline_module_proof(native, value['child']);_baseline_repeat_aliases(value, proof)
        if value['module_rows']!=proof['module_rows']:
            raise RuntimeError('Baseline module rows changed before watcher admission.')
        if _baseline_metric_requested(case):
            from . import baseline_metric_profile
            if value['baseline_metric_profile']!=baseline_metric_profile.prepare_owner_profile(case,native,value['child']):
                raise ValueError('Metric owner/source/original-code admission proof changed.')
        value['game_root']=str(root);value['baseline_required_paths']=[str(root/name)for name in required]
        value['baseline_alias_paths']=[item['resolved_path']for item in proof['module_alias_proofs']]
        return case,value,child,parent
    except BaseException:
        if child:native.close(child)
        if parent:native.close(parent)
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


def watch(case, output, *, cdb=DEFAULT_CDB, seconds=300, baseline=False):
    if type(seconds) is not int or not 300 <= seconds <= 600:
        raise ValueError('Crash watch is bounded to300–600 seconds.')
    if type(baseline)is not bool:raise ValueError('Explicit boolean baseline mode required.')
    native = _baseline_native() if baseline else Native()
    case, admission, child, parent = (_baseline_admitted if baseline else _admitted)(case, native)
    proc = None
    result = dict(format='s7-owned-crash-watch-v1', diagnostic_not_benchmark=True,
                  game_terminated_by_watcher=False, limit_bytes=MAX_BYTES, limit_dumps=MAX_DUMPS)
    if baseline:result.update(baseline_mode=BASELINE_MODE,native_graphics_modules_absent=True,
        baseline_module_rows=admission['module_rows'],baseline_module_alias_proofs=admission['module_alias_proofs'],
        baseline_aliases_reproved_under_lease=False)
    if baseline and admission.get('baseline_metric_profile')is not None:
        result['baseline_metric_profile']=admission['baseline_metric_profile']
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
            if baseline and admission.get('baseline_metric_profile')is not None:
                from . import baseline_metric_profile
                stream.write(baseline_metric_profile.commands())
            else:
                stream.write(commands(steam_heap=admission['child']['sha256']==STEAM_EXE))
        args = arguments(cdb, admission['child']['pid'])
        result.update(case=str(case), original=admission['child'], command=args,
                      cdb_sha256=CDB_PINS['cdb.exe'], target_before=native.state(child))
        _write_new(output / 'watch-start.private.json', result)
        from .load_hook import file_lease
        with ExitStack() as leases:
            for name in CDB_PINS:
                leases.enter_context(file_lease(cdb.parent / name))
            if baseline:
                for path in admission['baseline_required_paths']:leases.enter_context(file_lease(Path(path)))
                for path in admission['baseline_alias_paths']:leases.enter_context(file_lease(Path(path)))
                for relative in baseline_source_paths(metric=admission.get('baseline_metric_profile')is not None):leases.enter_context(file_lease(ROOT/relative))
            else:leases.enter_context(file_lease(Path(admission['hook'])))
            # Recheck all identities after acquiring deny-write leases.
            verify_cdb(cdb, native)
            _match(admission['child'], native.identity(admission['child']['pid'], child))
            _match(admission['parent'], native.identity(admission['parent']['pid'], parent))
            if baseline:
                _baseline_case(case);_baseline_modules(native,admission['child']['pid'],Path(admission['game_root']),admission['required_binaries'])
                proof=_baseline_module_proof(native,admission['child']);_baseline_repeat_aliases(admission,proof)
                result.update(baseline_module_rows=proof['module_rows'],baseline_module_alias_proofs=proof['module_alias_proofs'],
                    baseline_aliases_reproved_under_lease=True)
                if admission.get('baseline_metric_profile')is not None:
                    from . import baseline_metric_profile
                    if admission['baseline_metric_profile']!=baseline_metric_profile.prepare_owner_profile(case,native,admission['child']):
                        raise ValueError('Metric original-code proof changed under leases.')
                    result['baseline_metric_reproved_under_lease']=True
            verified_modules={Path(path)for path in admission.get('modules',[])}
            with (output / 'cdb.private.log').open('xb') as log:
                proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT,
                    cwd=output, env=environment(os.environ), shell=False, creationflags=0x08000000)
                debugger_identity = native.identity(proc.pid, int(proc._handle))
                if debugger_identity['path'] != str(cdb) or debugger_identity['sha256'] != CDB_PINS['cdb.exe']:
                    raise RuntimeError('The spawned CDB identity changed.')
                deadline = time.monotonic() + seconds
                next_modules=0
                reason = 'debugger_exited'
                while proc.poll() is None:
                    used = usage(output)
                    if used['dumps'] >= MAX_DUMPS or used['bytes'] >= MAX_BYTES:
                        reason = 'output_limit'; break
                    if time.monotonic() >= deadline:
                        reason = 'timeout'; break
                    if baseline and time.monotonic()>=next_modules and native.state(child)['alive']:
                        _baseline_absence(native,admission['child']['pid'],verified_modules);next_modules=time.monotonic()+1
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
    parser.add_argument('--baseline',action='store_true',help='Only an explicitly checkpointed no-native-graphics baseline.')
    args = parser.parse_args(argv)
    try:
        print(json.dumps(watch(args.case, args.output, cdb=args.cdb, seconds=args.seconds,baseline=args.baseline), indent=2))
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(2, f'Crash watch refused: {error}\n')


if __name__ == '__main__':
    main()
