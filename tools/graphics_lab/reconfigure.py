"""Reconfigure read-only observation in one already-created owned lab case.

No PID/export selector, material activation, world command, process stop, job or
game wait is exposed. The existing fixed command worker performs only the 952B
WePlayNativeGraphicsConfigureObservation operation in the already-loaded DLL.
"""
from __future__ import annotations

import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys

from . import game, load_hook
import isolated_process
from observe_input_pump import Module

ROOT = Path(__file__).resolve().parents[2]
KNOWN_EXES = {
    '4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f',
    '8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410',
    '76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab',
}
MAX_JSON = 1024 * 1024
FILETIME_UNIX_OFFSET_NS = 11644473600000000000
CREATION_WINDOW_NS = 180 * 1000000000
REQUIRED_SOURCES = {'tools/graphics_lab/game.py', 'tools/graphics_lab/load_hook.py',
                    'tools/installer/isolated_process.py', 'tools/supersampling/hook_probe.py'}


def _digest(raw):
    return hashlib.sha256(raw).hexdigest()


def _lab(path):
    path = game.graphics_profile._plain(path)
    work = game.graphics_profile._plain(ROOT / 'work')
    if path == work or not path.is_relative_to(work):
        raise ValueError('Use an explicit existing laboratory path under work/.')
    return path


def _read(path, limit):
    path = game.graphics_profile._plain(path)
    with path.open('rb') as stream:
        before = os.fstat(stream.fileno())
        raw = stream.read(limit + 1)
        after = os.fstat(stream.fileno())
    now = path.stat()
    key = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns)
    if len(raw) > limit or len(raw) != now.st_size or key(before) != key(after) or key(after) != key(now):
        raise ValueError('Laboratory input exceeded its bound or changed during the read.')
    return raw


def _json(path):
    data = json.loads(_read(path, MAX_JSON))
    if not isinstance(data, dict):
        raise ValueError('Laboratory receipt must be a JSON object.')
    return data


def validate_config(raw, expected_sha256):
    if (not isinstance(expected_sha256, str) or not re.fullmatch('[0-9a-f]{64}', expected_sha256)
            or type(raw) is not bytes or len(raw) != 952 or _digest(raw) != expected_sha256):
        raise ValueError('An exact SHA256-bound 952-byte observation config is required.')
    size, version, targets, stride, first = struct.unpack_from('<4IQ', raw)
    if size != 952 or version != 1 or not 1 <= targets <= 8 or not 1 <= stride <= 36000:
        raise ValueError('Invalid read-only observation header.')
    for target in range(8):
        start = 24 + target * 116
        values = raw[start:start + 116]
        if target >= targets:
            if any(values):
                raise ValueError('Unused observation targets must be zero.')
            continue
        vertex, pixel, samples, constants, samplers = struct.unpack_from('<5I', values)
        if not 1 <= vertex <= 512 or not 1 <= pixel <= 512 or vertex == pixel or not 1 <= samples <= 8 or constants > 8 or samplers > 0xffff:
            raise ValueError('Invalid bounded shader observation target.')
        seen = set()
        for register in range(8):
            row = values[20 + register * 12:32 + register * 12]
            if register >= constants:
                if any(row):
                    raise ValueError('Unused constant requests must be zero.')
                continue
            stage, index, mask = struct.unpack('<3I', row)
            if stage not in (1, 2) or index > (255 if stage == 1 else 223) or not 1 <= mask <= 15 or (stage, index) in seen:
                raise ValueError('Invalid or duplicate read-only constant request.')
            seen.add((stage, index))
    return {'targets': targets, 'frame_stride': stride, 'first_frame': first, 'config_sha256': expected_sha256}


def _case_files(case, dll):
    case, dll = _lab(case), _lab(dll)
    if not case.is_dir() or (case / 'result.private.json').exists():
        raise ValueError('Only an already-created, unfinished owned lab case is accepted.')
    owned = _json(case / 'owned.private.json')
    if (owned.get('sha256') not in KNOWN_EXES or owned['sha256'] not in isolated_process.PROFILES
            or owned.get('native_initialized') is not True or owned.get('kernel_isolation') != 7
            or type(owned.get('pid')) is not int or not 1 <= owned['pid'] <= 0xffffffff
            or type(owned.get('parent')) is not int or not 1 <= owned['parent'] <= 0xffffffff
            or owned['pid'] == owned['parent']):
        raise ValueError('The case does not identify an original initialized portable child.')
    executable = _lab(owned['path'])
    edition = isolated_process.PROFILES[owned['sha256']][0]
    relative = ('Data/Base/_Dbg/Bin/Release/Settlers7R.exe' if edition == 3 else 'Base/Bin/Settlers7R.exe')
    parts = Path(relative).parts
    root = executable.parents[len(parts) - 1]
    if executable != root / relative:
        raise ValueError('Unexpected original game layout.')
    _, actual_executable, adapter = game.platform.load(root)
    if actual_executable != executable or load_hook.digest(executable) != owned['sha256']:
        raise ValueError('The portable source identity changed.')
    checkpoint = _json(case / 'checkpoint.private.json')
    journal = _json(case / 'recovery-journal.private.json')
    if journal.get('state') != 'prepared' or _lab(journal['root']) != root or journal.get('critical') != checkpoint.get('files'):
        raise ValueError('The active case lacks its original prepared recovery boundary.')
    expected_files = {path.relative_to(root).as_posix() for path in (
        executable, adapter, executable.with_name('d3d9.dll'), executable.with_name('Vision90.dll'),
        executable.with_name('vBase90.dll'))}
    if (not isinstance(checkpoint.get('files'), dict) or set(checkpoint['files']) != expected_files
            or checkpoint['files'][executable.with_name('d3d9.dll').relative_to(root).as_posix()] != game.graphics_profile.DXVK_SHA256):
        raise ValueError('The active case must retain all five original binary pins and pinned DXVK.')
    for relative, pin in checkpoint['files'].items():
        path = _lab(root / relative)
        if not path.is_relative_to(root) or load_hook.digest(path) != pin:
            raise ValueError('An original guarded game binary changed.')
    sources = checkpoint.get('sources')
    if not isinstance(sources, dict):
        raise ValueError('The owned runner has no source checkpoint.')
    normalized = {name.replace('\\', '/'): pin for name, pin in sources.items()}
    if len(normalized) != len(sources) or not REQUIRED_SOURCES <= normalized.keys():
        raise ValueError('The runner/loader source checkpoint is incomplete.')
    for name, pin in normalized.items():
        relative = Path(name)
        if relative.is_absolute() or '..' in relative.parts or not isinstance(pin, str) or not re.fullmatch('[0-9a-f]{64}', pin):
            raise ValueError('Invalid source checkpoint entry.')
        if _digest(_read(case / 'source' / relative, MAX_JSON)) != pin:
            raise ValueError('The frozen runner source changed.')
        if name in REQUIRED_SOURCES - {'tools/graphics_lab/game.py'} and _digest(_read(ROOT / relative, MAX_JSON)) != pin:
            raise ValueError('The invoking fixed loader differs from the original runner checkpoint.')
    initialized = _json(case / 'hook-initialized.private.json')
    dll, build = load_hook.verify_build(dll)
    if (_json(case / 'hook-build.private.json') != build or initialized.get('initialized') is not True
            or initialized.get('succeeded') is not True or initialized.get('export') != 'WePlaySupersamplingInitialize'
            or initialized.get('original_entry_guarded') is not True or initialized.get('worker_code_rx') is not True
            or initialized.get('dll_sha256') != build['files'][load_hook.DLL_NAME]):
        raise ValueError('The initialized hook and exact frozen build do not match.')
    return {'case': case, 'dll': dll, 'build': build, 'owned': owned, 'executable': executable,
            'root': root, 'record_sha256': _digest(_read(case / 'owned.private.json', 16384)),
            'record_mtime_ns': (case / 'owned.private.json').stat().st_mtime_ns}


def _bind(api):
    api.k.GetProcessTimes.argtypes = [W.HANDLE, *([C.POINTER(W.FILETIME)] * 4)]
    api.k.GetProcessTimes.restype = W.BOOL
    for name in ('Module32FirstW', 'Module32NextW'):
        fn = getattr(api.k, name)
        fn.argtypes = [W.HANDLE, C.POINTER(Module)]
        fn.restype = W.BOOL


def _times(api, handle):
    values = [W.FILETIME() for _ in range(4)]
    api.check(api.k.GetProcessTimes(handle, *(C.byref(v) for v in values)))
    return (values[0].dwHighDateTime << 32) | values[0].dwLowDateTime


def _modules(api, pid):
    snapshot = api.k.CreateToolhelp32Snapshot(0x18, pid)
    if snapshot == C.c_void_p(-1).value:
        api.check(False)
    try:
        item = Module(); item.size = C.sizeof(item)
        valid = api.k.Module32FirstW(snapshot, C.byref(item))
        rows = []
        while valid:
            if len(rows) >= 512:
                raise RuntimeError('Loaded module enumeration exceeded its bound.')
            rows.append({'name': item.name, 'path': str(item.path), 'image_size': item.image_size})
            item = Module(); item.size = C.sizeof(item)
            valid = api.k.Module32NextW(snapshot, C.byref(item))
        if C.get_last_error() != 18:
            api.check(False)
        return rows
    finally:
        api.k.CloseHandle(snapshot)


def _live_state(api, handle, pid, parent):
    rows = [p for p in api.processes() if p.pid in (pid, parent)]
    child = [p for p in rows if p.pid == pid]
    parents = [p for p in rows if p.pid == parent]
    if len(child) != 1 or len(parents) != 1 or child[0].parent != parent:
        raise RuntimeError('The live recorded PID/parent relationship changed.')
    code = W.DWORD()
    api.check(api.k.GetExitCodeProcess(handle, C.byref(code)))
    if code.value != 259:
        raise RuntimeError('The recorded child is no longer running.')
    parent_handle = api.check(api.k.OpenProcess(0x1000, False, parent))
    try:
        parent_created = _times(api, parent_handle)
    finally:
        api.k.CloseHandle(parent_handle)
    return {'path': api.process_path(pid), 'parent_path': api.process_path(parent),
            'created': _times(api, handle), 'parent_created': parent_created, 'modules': _modules(api, pid)}


def validate_live_state(state, files):
    owned = files['owned']
    if game.graphics_profile._plain(state['path']) != files['executable']:
        raise RuntimeError('The live executable path differs from the owned case.')
    python = game.graphics_profile._plain(sys.executable)
    if game.graphics_profile._plain(state['parent_path']) != python or python.name.casefold() not in ('python.exe', 'pythonw.exe'):
        raise RuntimeError('The recorded parent is not this expected Python runtime.')
    if (type(state['created']) is not int or type(state['parent_created']) is not int
            or not 0 < state['parent_created'] <= state['created']):
        raise RuntimeError('The recorded parent/child creation relationship is invalid.')
    created_ns = state['created'] * 100 - FILETIME_UNIX_OFFSET_NS
    age_at_record = files['record_mtime_ns'] - created_ns
    if not 0 <= age_at_record <= CREATION_WINDOW_NS:
        raise RuntimeError('Stale PID or creation time outside the owned-record boundary.')
    for name, path, pin in (
        (files['dll'].name, files['dll'], files['build']['files'][load_hook.DLL_NAME]),
        ('d3d9.dll', files['executable'].with_name('d3d9.dll'), game.graphics_profile.DXVK_SHA256),
    ):
        modules = [m for m in state['modules'] if m['name'].casefold() == name.casefold()]
        if (len(modules) != 1 or game.graphics_profile._plain(modules[0]['path']) != path
                or load_hook.digest(path) != pin):
            raise RuntimeError('The actual loaded original renderer/hook path or hash changed.')
    return state['created']


class _CaseOwner:
    """Borrowed live handle; close only this handle, never the owner's game/job."""
    def __init__(self, files):
        self.files = files
        self.pid = files['owned']['pid']
        self.executable = files['executable']
        self.expected = files['owned']['sha256']
        self.native_initialized = True
        self.api = isolated_process.Windows()
        _bind(self.api)
        self._handle = self.api.check(self.api.k.OpenProcess(0x43a, False, self.pid))
        self._created = None
        try:
            self.owned()
        except BaseException:
            self.close()
            raise

    def owned(self):
        if not self._handle or _digest(_read(self.files['case'] / 'owned.private.json', 16384)) != self.files['record_sha256']:
            raise RuntimeError('The owned case record changed or its borrowed handle closed.')
        if load_hook.digest(self.executable) != self.expected:
            raise RuntimeError('The original executable fingerprint changed.')
        state = _live_state(self.api, self._handle, self.pid, self.files['owned']['parent'])
        created = validate_live_state(state, self.files)
        if self._created is not None and created != self._created:
            raise RuntimeError('The original process creation identity changed.')
        self._created = created

    def close(self):
        if self._handle:
            self.api.k.CloseHandle(self._handle)
            self._handle = None


def configure(case, dll, config, expected_sha256, *, label):
    if not isinstance(label, str) or not re.fullmatch('[a-z][a-z0-9_-]{0,31}', label):
        raise ValueError('Use a short unique lowercase diagnostic label.')
    files = _case_files(case, dll)
    raw = _read(_lab(config), 952)
    summary = validate_config(raw, expected_sha256)
    request = files['case'] / f'reconfigure-{label}-request.private.json'
    result = files['case'] / f'reconfigure-{label}-result.private.json'
    if request.exists() or result.exists():
        raise ValueError('Keep existing diagnostics and use a new request label.')
    with load_hook.file_lease(files['dll']):
        owner = _CaseOwner(files)
        try:
            with request.open('x', encoding='utf-8') as stream:
                json.dump({'format': 's7-live-observer-config-request-v1', 'pid': owner.pid,
                           'owned_record_sha256': files['record_sha256'], 'read_only_observer': True,
                           **summary}, stream, indent=2)
            receipt = load_hook.invoke(owner, files['dll'], 'configure', raw)
            receipt.update(read_only_observer=True, config_sha256=expected_sha256, pid=owner.pid)
            with result.open('x', encoding='utf-8') as stream:
                json.dump(receipt, stream, indent=2)
            return receipt
        finally:
            owner.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', type=Path, required=True)
    parser.add_argument('--dll', type=Path, required=True)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--sha256', required=True)
    parser.add_argument('--label', required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(configure(args.case, args.dll, args.config, args.sha256, label=args.label), indent=2))
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(2, f'Live observation reconfiguration refused: {error}\n')


if __name__ == '__main__':
    main()
