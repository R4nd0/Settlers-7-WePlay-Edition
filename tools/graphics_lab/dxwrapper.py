"""Pinned, reversible dxwrapper experiments; never a production renderer choice."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

from . import recover

VERSION = 'v1.9.8700.25'
ARCHIVE_SHA256 = 'e6aa4fc685115c8f73dee4ea2d446b6a1f8b57dd04db9889d2c6d1b6d7b8c825'
DLL_SHA256 = '23a2889e0785a02f17f1ee764254bf22b937fc5bdc6ed6374201a9f50cc7b43d'
STUB_SHA256 = 'b8b878e29c2931d6a8d064665c4c93edb28a0105152e7247f8f543c1603985c3'
MODES = ('native', 'dxwrapper-d3d9', 'dxwrapper-d3d9on12')
JOURNAL = 'renderer-journal.private.json'
FORMAT = 's7-dxwrapper-lab-v1'
NAMES = ('d3d9.dll', 'dxwrapper.dll', 'dxwrapper.ini')


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def configuration(mode):
    if mode not in MODES[1:]:
        raise ValueError('An explicit dxwrapper laboratory mode is required.')
    return (f'[General]\nRealDllPath=AUTO\nWrapperMode=d3d9\n'
            'IncludeProcess=Settlers7R.exe\nDisableLogging=0\n'
            '[Plugins]\nLoadPlugins=0\n'
            '[Compatibility]\nEnableD3d9Wrapper=1\nD3d9to9Ex=0\n'
            f'D3d9on12={int(mode == MODES[2])}\n'
            '[d3d9]\nAnisotropicFiltering=0\nAntiAliasing=0\n'
            'LimitPerFrameFPS=60\n').encode('ascii')


def artifacts(package, mode):
    if mode not in MODES:
        raise ValueError('Unknown isolated renderer mode.')
    if mode == 'native':
        if package is not None:
            raise ValueError('Native reference does not take a wrapper package.')
        return {'d3d9.dll': None}
    package = recover.lab(package)
    dll, _ = recover._read(package / 'dxwrapper.dll', 16 * 1024 * 1024)
    stub, _ = recover._read(package / 'Stub/d3d9.dll', 1024 * 1024)
    if digest(dll) != DLL_SHA256 or digest(stub) != STUB_SHA256:
        raise ValueError('The dxwrapper release or x86 stub differs from the pinned release.')
    return {'d3d9.dll': stub, 'dxwrapper.dll': dll, 'dxwrapper.ini': configuration(mode)}


def current(path):
    path = recover.plain(path)
    return recover._read(path, 16 * 1024 * 1024)[0] if path.exists() else None


class Session:
    def __init__(self, root, case, mode, package=None, *, water_probe=None,water_pow_cap=False,water_benchmark=False,water_worker_priority=None,on12_runtime_build=None,water_backend=False):
        from . import game
        if type(water_backend)is not bool or (water_backend and (on12_runtime_build is None or water_probe is None
                or mode!='dxwrapper-d3d9on12' or water_pow_cap or water_benchmark)):
            raise ValueError('Backend water correction requires its explicit source build and exclusive read-only capture.')
        if type(water_pow_cap)is not bool or (water_pow_cap and (water_probe is None or mode!='dxwrapper-d3d9on12')):
            raise ValueError('Water POW cap requires the exact probe and D3D9On12 backend.')
        if type(water_benchmark)is not bool or (water_benchmark and not water_pow_cap):raise ValueError('Water benchmark requires the explicit On12 correction.')
        if on12_runtime_build is not None and (mode!='dxwrapper-d3d9on12' or not (water_pow_cap or water_backend) or water_probe is None):
            raise ValueError('A local runtime requires the explicit corrected On12 laboratory profile.')
        if water_worker_priority is None:water_worker_priority=(water_pow_cap or water_backend) and not water_benchmark
        if type(water_worker_priority)is not bool or (water_worker_priority and (not (water_pow_cap or water_backend) or water_benchmark)):
            raise ValueError('Worker priority requires its exclusive exact On12 correction.')
        self.worker_policy=None
        if water_worker_priority:
            from .on12_worker import Policy
            self.worker_policy=Policy()
        self.root, self.case = recover.lab(root), recover.lab(case)
        if self.case.is_relative_to(self.root) or self.root.is_relative_to(self.case):
            raise ValueError('Renderer case and game must be separate.')
        value, self.executable, _ = game.platform.load(self.root)
        if value['edition'] != 'history':
            raise ValueError('First dxwrapper evaluation is scoped to History Edition.')
        if (self.root / 'LocalData/graphics-choice.json').exists():
            raise ValueError('Use a laboratory copy without production graphics auto-loading.')
        self.mode, self.payload = mode, artifacts(package, mode)
        self.probe = None
        if water_probe is not None:
            if mode not in MODES[1:]:
                raise ValueError('Water observation requires an exclusive pinned dxwrapper backend.')
            from .water_on12_probe import Probe
            self.probe=Probe(water_probe,self.root,self.case,pow_cap=water_pow_cap,benchmark=water_benchmark,once=water_backend)
        self.last_observed = 0.0
        self.runtime=None
        if on12_runtime_build is not None:
            from .on12_runtime import Session as RuntimeSession
            self.runtime=RuntimeSession(self.root,self.case,on12_runtime_build)
            expected='water-worker' if water_backend else 'stock'
            if self.runtime.manifest['variant']!=expected:raise ValueError('The On12 source variant does not match the requested water correction.')
        if current(self.executable.with_name('d3d9.dll')) is None or digest(
                current(self.executable.with_name('d3d9.dll'))) != recover.DXVK:
            raise ValueError('The laboratory must begin with the original pinned DXVK renderer.')
        for name in NAMES[1:]:
            if self.executable.with_name(name).exists():
                raise ValueError('An existing dxwrapper configuration must not be overwritten.')

    def apply(self):
        recover._no_games()
        if (self.case / JOURNAL).exists():
            raise ValueError('The renderer journal already exists; recover the old case first.')
        originals = self.case / 'renderer-original'
        originals.mkdir()
        targets = {}
        for name, raw in self.payload.items():
            path = self.executable.with_name(name)
            before = current(path)
            if (name == 'd3d9.dll' and (before is None or digest(before) != recover.DXVK)
                    or name != 'd3d9.dll' and before is not None):
                raise RuntimeError('Renderer inputs changed before staging.')
            if before is not None:
                recover._write_new(originals / name, before)
            targets[name] = {'before': digest(before) if before is not None else None,
                             'during': digest(raw) if raw is not None else None}
        value = dict(format=FORMAT, state='prepared', root=str(self.root), mode=self.mode,
                     executable_sha256=digest(self.executable.read_bytes()), targets=targets)
        recover._atomic(self.case / JOURNAL, json.dumps(value, indent=2).encode())
        for name, raw in self.payload.items():
            path = recover.plain(self.executable.with_name(name))
            if raw is None:
                path.unlink()
            else:
                recover._atomic(path, raw)
        value['state'] = 'applied'
        recover._atomic(self.case / JOURNAL, json.dumps(value, indent=2).encode())
        if self.probe is not None:
            self.probe.prepare()
        if self.runtime is not None:self.runtime.apply()

    def observe(self, owned):
        """Inventory modules; the separate native probe must prove On12 identity."""
        now = time.monotonic()
        if now - self.last_observed < 5:
            return
        self.last_observed = now
        owned.owned()
        from .crash_watch import Native
        native = Native()
        paths = native.modules(owned.pid)
        if self.runtime is not None:self.runtime.observe(owned,paths)
        selected = [p for p in paths if p.name.casefold() in
                    ('d3d9.dll', 'd3d9on12.dll', 'd3d12.dll', 'd3d12core.dll', 'dxwrapper.dll')]
        owned.owned()
        rows = [dict(path=str(p), sha256=digest(p.read_bytes())) for p in selected]
        recover._atomic(self.case / 'renderer-modules.private.json', json.dumps(dict(
            pid=owned.pid, mode=self.mode, sampled_monotonic_ns=time.monotonic_ns(),
            modules=rows, device_identity_proven=False), indent=2).encode())
        if self.worker_policy is not None and not self.worker_policy.done:
            try:
                raw,_=recover._read(self.case/'water-probe-live.private.json',32768)
                report=self.worker_policy.observe(owned,json.loads(raw))
            except (OSError,ValueError):return
            if report is not None:
                recover._atomic(self.case/'worker-priority.private.json',json.dumps(report,indent=2).encode())

    def restore(self):
        try:
            if self.worker_policy is not None:
                report=self.worker_policy.close()
                if self.worker_policy.done:
                    try:recover._atomic(self.case/'worker-priority.private.json',json.dumps(report,indent=2).encode())
                    except OSError:pass
        finally:
            try:
                if self.probe is not None:self.probe.close()
            finally:
                try:
                    if self.runtime is not None:self.runtime.restore()
                finally:
                    if (self.case / JOURNAL).exists():restore(self.case)


def restore(case):
    """Recover a fixed renderer file set, including partially applied staging."""
    from . import game
    case = recover.lab(case)
    raw, _ = recover._read(case / JOURNAL, 16384)
    value = json.loads(raw)
    if (not isinstance(value, dict) or set(value) != {
            'format', 'state', 'root', 'mode', 'executable_sha256', 'targets'}
            or value['format'] != FORMAT or value['state'] not in ('prepared', 'applied', 'restored')
            or value['mode'] not in MODES):
        raise ValueError('Invalid renderer recovery journal.')
    root = recover.lab(value['root'])
    if root.is_relative_to(case) or case.is_relative_to(root):
        raise ValueError('Invalid renderer recovery separation.')
    recover._no_games()
    _, executable, _ = game.platform.load(root)
    if digest(executable.read_bytes()) != value['executable_sha256']:
        raise RuntimeError('The original executable changed.')
    expected = {'d3d9.dll': None} if value['mode'] == 'native' else {
        'd3d9.dll': STUB_SHA256, 'dxwrapper.dll': DLL_SHA256,
        'dxwrapper.ini': digest(configuration(value['mode']))}
    targets = value['targets']
    if not isinstance(targets, dict) or set(targets) != set(expected):
        raise ValueError('Invalid renderer recovery target set.')
    originals = {}
    # Validate every target and backup before restoring any file.
    for name, during in expected.items():
        before = recover.DXVK if name == 'd3d9.dll' else None
        if targets[name] != {'before': before, 'during': during}:
            raise ValueError('Invalid renderer recovery identity.')
        original = current(case / 'renderer-original' / name)
        if (digest(original) if original is not None else None) != before:
            raise RuntimeError('Renderer backup changed; retain diagnostics.')
        present = current(executable.with_name(name))
        pin = digest(present) if present is not None else None
        allowed = (before,) if value['state'] == 'restored' else (before, during)
        if pin not in allowed:
            raise RuntimeError('A renderer file changed; refusing to overwrite newer data.')
        originals[name] = original
    for name, original in originals.items():
        path = recover.plain(executable.with_name(name))
        if original is None:
            if path.exists():
                path.unlink()
        else:
            recover._atomic(path, original)
    value['state'] = 'restored'
    recover._atomic(case / JOURNAL, json.dumps(value, indent=2).encode())
    return {'renderer_restored': True, 'saves_restored': False}


def device_result(mode, completed):
    """Reject a successful process exit that silently fell back from D3D9On12."""
    record = dict(mode=mode, exit_code=completed.returncode,
                  stdout=completed.stdout.decode(errors='replace'),
                  stderr=completed.stderr.decode(errors='replace'), synthetic_only=True,
                  passed=False)
    try:
        value = json.loads(record['stdout'])
        integers = ('create_device_hr', 'on12_query_hr', 'd3d12_device_hr', 'raytracing_tier')
        if (mode not in MODES or not isinstance(value, dict) or set(value) != {
                *integers, 'pixels_present_reset_passed', 'synthetic_only'}
                or any(type(value[key]) is not int for key in integers)
                or value['synthetic_only'] is not True
                or type(value['pixels_present_reset_passed']) is not bool
                or value['raytracing_tier'] < 0):
            raise ValueError('Unexpected device-fixture result schema.')
        record['device'] = value
        bridge = mode == 'dxwrapper-d3d9on12'
        record['passed'] = (completed.returncode == 0 and value['create_device_hr'] == 0
            and value['pixels_present_reset_passed']
            and (value['on12_query_hr'] >= 0) == bridge
            and (value['d3d12_device_hr'] >= 0) == bridge)
    except (ValueError, TypeError):
        record['result_error'] = 'Invalid or missing device-fixture JSON.'
    return record


def device_fixture(build, output, package):
    """Run a source-bound authored fixture; its results never qualify the game."""
    from . import game
    build, output = recover.lab(build), recover.lab(output)
    if output.exists() or not output.parent.is_dir():
        raise ValueError('Use a fresh device-fixture output directory.')
    raw, _ = recover._read(build / 'build.json', 16384)
    manifest = json.loads(raw.decode('utf-8-sig'))
    sources = {'source_sha256': game.ROOT / 'native/dxwrapper_device_fixture.cpp',
               'build_sha256': game.ROOT / 'native/dxwrapper_device_fixture_build.ps1',
               'binary_sha256': build / 'dxwrapper_device_fixture.exe'}
    if (not isinstance(manifest, dict) or set(manifest) != {'format', *sources}
            or manifest['format'] != 's7-dxwrapper-device-fixture-v1'):
        raise ValueError('Unknown device-fixture manifest.')
    for key, source in sources.items():
        if digest(current(source)) != manifest[key]:
            raise ValueError('Device-fixture source or binary changed; rebuild.')
    payloads = {mode: artifacts(None if mode == 'native' else package, mode) for mode in MODES}
    recover._no_games()
    game.memory_guard.observe_host_memory()
    output.mkdir()
    for source in sources.values():
        shutil.copyfile(source, output / source.name)
    results = []
    for mode in MODES:
        recover._no_games()
        game.memory_guard.observe_host_memory()
        case = output / mode
        case.mkdir()
        exe = case / 'dxwrapper_device_fixture.exe'
        raw = current(output / exe.name)
        if digest(raw) != manifest['binary_sha256']:
            raise RuntimeError('Fixture executable changed after checkpoint.')
        recover._write_new(exe, raw)
        for name, raw in payloads[mode].items():
            if raw is not None:
                if name == 'dxwrapper.ini':
                    raw = raw.replace(b'Settlers7R.exe', b'dxwrapper_device_fixture.exe')
                recover._write_new(case / name, raw)
        completed = subprocess.run([str(exe)], cwd=case,
            env=game.clean_environment(os.environ), capture_output=True, timeout=30)
        record = device_result(mode, completed)
        recover._write_new(case / 'result.private.json', json.dumps(record, indent=2).encode())
        results.append(record)
        print(json.dumps(record), flush=True)
    recover._write_new(output / 'results.private.json', json.dumps(results, indent=2).encode())
    if not all(record['passed'] for record in results):
        raise RuntimeError('Device-fixture validation failed; preserve the per-mode results.')
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    launch = sub.add_parser('run')
    launch.add_argument('--bundle', type=Path, required=True)
    launch.add_argument('--output', type=Path, required=True)
    launch.add_argument('--mode', choices=MODES, required=True)
    launch.add_argument('--package', type=Path)
    launch.add_argument('--seconds', type=int, default=900)
    launch.add_argument('--water-probe', type=Path)
    launch.add_argument('--on12-runtime-build',type=Path,help='Explicit source-built local On12 laboratory runtime')
    launch.add_argument('--water-backend',action='store_true',help='Use the exact-water shader translation variant and detach its observer after capture')
    launch.add_argument('--water-pow-cap',action='store_true')
    launch.add_argument('--water-benchmark',action='store_true')
    worker=launch.add_mutually_exclusive_group()
    worker.add_argument('--water-worker-priority',dest='water_worker_priority',action='store_true',default=None)
    worker.add_argument('--no-water-worker-priority',dest='water_worker_priority',action='store_false')
    recovery = sub.add_parser('recover')
    recovery.add_argument('--case', type=Path, required=True)
    fixture = sub.add_parser('device-fixture')
    fixture.add_argument('--build', type=Path, required=True)
    fixture.add_argument('--output', type=Path, required=True)
    fixture.add_argument('--package', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'recover':
        from . import on12_runtime
        if (args.case/on12_runtime.JOURNAL).exists():print(json.dumps(on12_runtime.restore(args.case)))
        print(json.dumps(restore(args.case)))
        print(json.dumps(recover.recover(args.case)))
    elif args.command == 'device-fixture':
        device_fixture(args.build, args.output, args.package)
    else:
        from . import game
        game.run(args.bundle, args.output, width=1920, height=1080,
                 high_quality=True, seconds=args.seconds,
                 laboratory_renderer=args.mode, dxwrapper_package=args.package,
                 water_probe=args.water_probe,water_pow_cap=args.water_pow_cap,water_benchmark=args.water_benchmark,water_worker_priority=args.water_worker_priority,on12_runtime_build=args.on12_runtime_build,water_backend=args.water_backend)


if __name__ == '__main__':
    main()
