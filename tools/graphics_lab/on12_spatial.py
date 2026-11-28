"""Opt-in native D3D12 spatial pass; authored GPU tests and isolated game host."""
from contextlib import ExitStack
import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess

from . import capture_bootstrap as loader, recover

ROOT = Path(__file__).resolve().parents[2]
DLL = 'WePlayOn12SpatialLab.dll'
SOURCES = ('on12_frame_context.h', 'on12_frame_context.cpp', 'on12_spatial_pass.h',
           'on12_spatial_pass.cpp', 'on12_spatial_pass.hlsl', 'on12_spatial_hook.cpp',
           'on12_spatial_hook.def', 'on12_spatial_gpu.cpp', 'water_on12_identity.h',
           'on12_output_window.h', 'on12_output_window.cpp', 'on12_output_window_fixture.cpp',
           'on12_scene_targets.h', 'on12_scene_targets.cpp', 'on12_scene_boundary.h', 'on12_scene_abi_fixture.cpp',
           'on12_temporal_inputs.h', 'on12_temporal_inputs_fixture.cpp','on12_depth_capture.h',
           'on12_native_scope.h','on12_native_scope_fixture.cpp','on12_native_temporal.h','on12_native_temporal.cpp',
           'on12_scene_projection.h','on12_temporal.h','on12_temporal.cpp',
           'on12_projection_binding.h','on12_projection_jitter.h','on12_projection_jitter_fixture.cpp','on12_native_temporal_gpu.cpp','on12_jitter_program.h')
BINARIES = (DLL, 'on12_spatial_gpu.exe', 'on12_output_window_fixture.exe', 'on12_scene_abi_fixture.exe', 'on12_temporal_inputs_fixture.exe','on12_native_scope_fixture.exe')
BINARIES += ('on12_projection_jitter_fixture.exe','on12_projection_jitter_gpu.exe')
BINARIES += ('on12_motion_pose_fixture.exe',)
SOURCES += ('on12_motion_pose.h','on12_motion_pose_fixture.cpp')
SOURCES += ('on12_frame_temporal_gpu.h','on12_temporal_camera.h','on12_temporal_camera.cpp','on12_temporal_camera.hlsl',
            'on12_temporal_camera_pass.h','on12_temporal_camera_pass.cpp','on12_temporal_pass.h','on12_temporal_pass.cpp',
            'on12_temporal.hlsl','on12_temporal_graph.h','on12_temporal_graph.cpp','on12_temporal_pipeline.h','on12_temporal_pipeline.cpp')
MANIFEST = 'on12-spatial-build.json'
WINDOWS_ON12 = '8d79e366f97ea0236e57b3d597626c7072b67adc182a9780548e9e3926aaff9a'
SOURCE_ON12 = '24aceac4aa280d09f1ec2c4c9a2b3d784bf158fe1f0f23292ab1072db9187364'
TEMPORAL_ON12 = 'd2e9c5e28d0ddd42dbdf650ec76b9935ddb69b756c4e6a301272169fac0d465e'


def verify_build(directory):
    directory = recover.lab(directory)
    raw, _ = recover._read(directory / MANIFEST, 16384)
    value = json.loads(raw.decode('utf-8-sig'))
    if (not isinstance(value, dict) or set(value) != {'format', 'settings_bytes', 'build_sha256', 'files'}
            or value['format'] != 's7-on12-spatial-build-v1' or value['settings_bytes'] != 16
            or not isinstance(value['files'], dict) or set(value['files']) != {*SOURCES, *BINARIES}
            or value['build_sha256'] != loader.digest(ROOT / 'native/on12_spatial_build.ps1')):
        raise ValueError('Unknown or stale native On12 spatial build.')
    for name, pin in value['files'].items():
        if loader.digest(directory / name) != pin or (name in SOURCES and loader.digest(ROOT / 'native' / name) != pin):
            raise ValueError('Native On12 spatial source or binary changed; rebuild.')
    return directory, value


def configuration(report, *, render_size=None, output_size=None, scene_only=False):
    report = recover.lab(report)
    text = str(report)
    raw = text.encode('utf-16le')
    if report.name != 'on12-spatial-live.private.json' or '\0' in text or len(raw) > 1022 or report.exists():
        raise ValueError('Use a fresh native On12 spatial report below work/.')
    if type(scene_only) is not bool:
        raise ValueError('Scene separation requires explicit boolean intent.')
    if render_size is None and output_size is None and not scene_only:
        return struct.pack('<3I', 1036, 1, 1) + raw.ljust(1024, b'\0')
    if (type(render_size) is not tuple or type(output_size) is not tuple or len(render_size) != 2 or len(output_size) != 2
            or any(type(x) is not int for x in (*render_size, *output_size))):
        raise ValueError('Upscaling requires explicit integer render and output extents.')
    sw, sh = render_size
    ow, oh = output_size
    if not (640 <= sw < ow <= 4096 and 360 <= sh < oh <= 4096 and ow <= sw * 2 and oh <= sh * 2 and sw * oh == sh * ow):
        raise ValueError('Use an aspect-matched upscale of at most twice the internal resolution.')
    version = 3 if scene_only else 2
    return struct.pack('<7I', 1052, version, version, sw, sh, ow, oh) + raw.ljust(1024, b'\0')


def command(case, sequence, enabled):
    if type(sequence) is not int or not 1 <= sequence <= 0xffffffff or type(enabled) is not bool:
        raise ValueError('Spatial commands require a positive sequence and explicit boolean.')
    case = recover.lab(case)
    recover._atomic(case / 'on12-spatial.private.command', struct.pack('<4I', 16, 1, sequence, int(enabled)))


class Session:
    """Attach only after the source-bound water observer detached every hook."""
    def __init__(self, build, executable, case, *, render_size=None, output_size=None, scene_only=False):
        self.directory, self.build = verify_build(build)
        self.executable, self.case = recover.lab(executable), recover.lab(case)
        if loader.digest(self.executable) != loader.UBI:
            raise ValueError('The native spatial host requires the exact isolated Ubisoft original.')
        if any(a.is_relative_to(b) or b.is_relative_to(a) for a, b in
               ((self.directory, self.case), (self.directory, self.executable.parent))):
            raise ValueError('Keep spatial builds, game and diagnostic output separate.')
        self.config = configuration(self.case / 'on12-spatial-live.private.json', render_size=render_size, output_size=output_size, scene_only=scene_only)
        self.protocol = 'on12_scene' if scene_only else 'on12_upscale' if output_size is not None else 'on12_spatial'
        self.leases = ExitStack()
        self.prepared = self.initialized = False

    def prepare(self):
        from .load_hook import file_lease
        from . import dxwrapper
        try:
            for path, pin in ((self.directory / DLL, self.build['files'][DLL]),
                              (self.executable.with_name('d3d9.dll'), dxwrapper.STUB_SHA256),
                              (self.executable.with_name('dxwrapper.dll'), dxwrapper.DLL_SHA256)):
                self.leases.enter_context(file_lease(path))
                if loader.digest(path) != pin:
                    raise ValueError('Spatial host deployment identity changed.')
            runtime=self.executable.with_name('d3d9on12.dll')
            self.leases.enter_context(file_lease(runtime))
            if loader.digest(runtime)not in(SOURCE_ON12,TEMPORAL_ON12):raise ValueError('Spatial host runtime identity changed.')
            verify_build(self.directory)
            self.prepared = True
        except BaseException:
            self.close()
            raise

    def observe(self, owned):
        if self.initialized:
            return
        if not self.prepared or owned.executable != self.executable:
            raise RuntimeError('Spatial host is not prepared for this owned process.')
        try:
            raw, _ = recover._read(self.case / 'water-probe-live.private.json', 32768)
        except FileNotFoundError:
            return
        report = json.loads(raw)
        if not report.get('snapshot_done'):
            return
        if (report.get('failure') != 0 or report.get('attached') is not True or report.get('on12_query_hr') != 0
                or report.get('snapshot_once') is not True or report.get('hooks_detached') is not True
                or report.get('pow_cap_requested') is not False or report.get('caps_applied') != 0):
            raise RuntimeError('Spatial host requires the fully detached read-only On12 water observer.')
        receipt = loader._invoke_fixed(owned, self.directory / DLL, self.config, self.protocol)
        receipt.update(format='s7-on12-spatial-init-v1', dll_sha256=self.build['files'][DLL], water_hooks_detached=True)
        recover._write_new(self.case / 'on12-spatial-initialized.private.json', json.dumps(receipt, indent=2).encode())
        self.initialized = True

    def close(self):
        self.prepared = False
        self.leases.close()


def gpu(build, package, output, runtime=None):
    from . import dxwrapper, on12_runtime
    directory, manifest = verify_build(build)
    output = recover.lab(output)
    if output.exists() or not output.parent.is_dir():
        raise ValueError('Use a fresh GPU-fixture output below work/.')
    local = None
    if runtime is not None:
        local, local_manifest, _ = on12_runtime.verify_build(runtime)
        on12_runtime.validate_runtime()
        allowed={'water-worker':SOURCE_ON12,'temporal-worker':TEMPORAL_ON12}
        if allowed.get(local_manifest['variant'])!=local_manifest['files']['d3d9on12.dll']:
            raise ValueError('Use the exact admitted source runtime for spatial GPU tests.')
    payload = {name: dxwrapper.artifacts(package, mode) for name, mode in
               (('native', 'dxwrapper-d3d9'), ('windows-on12', 'dxwrapper-d3d9on12'))}
    if local is not None:
        payload['local-on12'] = dxwrapper.artifacts(package, 'dxwrapper-d3d9on12')
    recover._no_games()
    output.mkdir()
    recover._write_new(output / MANIFEST, json.dumps(manifest, indent=2).encode())
    results = []
    for label, files in payload.items():
        recover._no_games()
        case = output / label
        case.mkdir()
        verify_build(directory)
        for name in ('on12_spatial_gpu.exe', 'on12_spatial_pass.hlsl','on12_temporal.hlsl','on12_temporal_camera.hlsl'):
            shutil.copyfile(directory / name, case / name)
            if loader.digest(case / name) != manifest['files'][name]:
                raise RuntimeError('Spatial GPU fixture changed during copying.')
        for name, raw in files.items():
            if name == 'dxwrapper.ini':
                raw = raw.replace(b'IncludeProcess=Settlers7R.exe', b'IncludeProcess=on12_spatial_gpu.exe')
            recover._write_new(case / name, raw)
        expected = WINDOWS_ON12
        if label == 'local-on12':
            expected = local_manifest['files']['d3d9on12.dll']
            for name in on12_runtime.PAYLOAD:
                if on12_runtime.build.digest(local / name) != local_manifest['files'][name]:
                    raise RuntimeError('Local On12 payload changed.')
                shutil.copyfile(local / name, case / name)
        done = subprocess.run([str(case / 'on12_spatial_gpu.exe'), expected, 'native' if label == 'native' else 'on12'],
                              cwd=case, capture_output=True, timeout=45, creationflags=subprocess.CREATE_NO_WINDOW)
        result = dict(case=label, exit_code=done.returncode, stdout=done.stdout.decode(errors='replace'),
                      stderr=done.stderr.decode(errors='replace'), synthetic_only=True)
        results.append(result)
        recover._atomic(output / 'result.private.json', json.dumps(results, indent=2).encode())
        print(json.dumps(result), flush=True)
        if done.returncode:
            raise RuntimeError('Native On12 spatial GPU fixture failed; retain the diagnostic output.')
        parsed = json.loads(result['stdout'])
        if parsed.get('passed') is not True or parsed.get('synthetic_only') is not True:
            raise RuntimeError('Invalid spatial GPU result.')
        if label!='native' and (parsed.get('temporal_handoff')is not True or parsed.get('geometry_motion_handoff')is not True):
            raise RuntimeError('The D3D9 temporal resource handoff was not verified.')
    return results


def projection_gpu(build,package,output,runtime):
    """Compare ordered D3D9 projection jitter with an independent shader reference."""
    from . import dxwrapper,on12_runtime
    directory,manifest=verify_build(build)
    local,local_manifest,_=on12_runtime.verify_build(runtime)
    if local_manifest['variant']!='water-worker' or local_manifest['files']['d3d9on12.dll']!=SOURCE_ON12:
        raise ValueError('Use the exact optimized source runtime for ordered projection tests.')
    output=recover.lab(output)
    if (output.exists() or not output.parent.is_dir() or output.is_relative_to(directory) or directory.is_relative_to(output)
            or output.is_relative_to(local) or local.is_relative_to(output)):
        raise ValueError('Use a fresh separate projection test directory below work/.')
    on12_runtime.validate_runtime();recover._no_games()
    payload=dxwrapper.artifacts(package,'dxwrapper-d3d9on12')
    output.mkdir()
    executable='on12_projection_jitter_gpu.exe'
    shutil.copyfile(directory/executable,output/executable)
    if loader.digest(output/executable)!=manifest['files'][executable]:raise RuntimeError('Projection test binary changed.')
    for name,raw in payload.items():
        if name=='dxwrapper.ini':raw=raw.replace(b'IncludeProcess=Settlers7R.exe',b'IncludeProcess='+executable.encode())
        recover._write_new(output/name,raw)
    for name in on12_runtime.PAYLOAD:
        shutil.copyfile(local/name,output/name)
        if loader.digest(output/name)!=local_manifest['files'][name]:raise RuntimeError('Projection runtime changed.')
    verify_build(directory);on12_runtime.verify_build(local);recover._no_games()
    done=subprocess.run([str(output/executable),SOURCE_ON12],cwd=output,capture_output=True,timeout=45,creationflags=subprocess.CREATE_NO_WINDOW)
    result=dict(exit_code=done.returncode,stdout=done.stdout.decode(errors='replace'),stderr=done.stderr.decode(errors='replace'),synthetic_only=True,game_taa_enabled=False)
    recover._write_new(output/'result.private.json',json.dumps(result,indent=2).encode())
    print(json.dumps(result),flush=True)
    if done.returncode:raise RuntimeError('Ordered projection GPU test failed.')
    parsed=json.loads(result['stdout'])
    if any(parsed.get(name)is not True for name in ('passed','synthetic_only','ordered_projection','reference_matched','ui_unchanged','shadow_unchanged','logical_constants_unchanged','stateblock_restored')):
        raise RuntimeError('Unexpected ordered projection GPU result.')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--runtime', type=Path)
    parser.add_argument('--projection-only',action='store_true',help='Explicit ordered projection GPU conformance; requires --runtime.')
    args = parser.parse_args()
    if args.projection_only:
        if args.runtime is None:parser.error('--projection-only requires --runtime')
        projection_gpu(args.build,args.package,args.output,args.runtime)
    else:gpu(args.build, args.package, args.output, args.runtime)


if __name__ == '__main__':
    main()
