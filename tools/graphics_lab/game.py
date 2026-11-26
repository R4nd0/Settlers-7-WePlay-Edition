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
import struct
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


def capture_command(raw, previous):
    if not isinstance(raw, bytes) or len(raw) > 4096:
        raise ValueError('Native command exceeds its bound.')
    value = graphics_profile._json(raw)
    if (not isinstance(value, dict) or set(value) != {'operation', 'sequence'}
            or value['operation'] != 'capture' or type(value['sequence']) is not int
            or not 1 <= value['sequence'] <= 1000000):
        raise ValueError('Only a bounded native capture command is supported.')
    return value['sequence'] if value['sequence'] > previous else None


def native_command(raw, previous, material_flags=0,comparison_allowed=False,crash_watch_allowed=False,water_allowed=False,shadow_allowed=False,ambient_allowed=False,palette_allowed=False,water_palette_allowed=False):
    if not isinstance(raw,bytes) or len(raw)>4096:
        raise ValueError('Native command exceeds its bound.')
    value=graphics_profile._json(raw)
    if isinstance(value,dict)and value.get('operation')=='variant':
        if(set(value)!={'operation','sequence','variant','enabled'}or (palette_allowed is not True and water_palette_allowed is not True)
            or (palette_allowed is True and water_palette_allowed is True)
            or type(value['sequence'])is not int or not 1<=value['sequence']<=1000000
            or type(value['variant'])is not int or value['variant']not in(0,1,2)
            or type(value['enabled'])is not int or value['enabled']not in(0,1 if water_palette_allowed else 4)):
            raise ValueError('Only the three admitted palette programs may be selected.')
        return value if value['sequence']>previous else None
    if isinstance(value,dict)and value.get('operation')=='ambient':
        if(set(value)!={'operation','sequence','enabled'}or ambient_allowed is not True
            or type(value['sequence'])is not int or not 1<=value['sequence']<=1000000
            or type(value['enabled'])is not int or value['enabled']not in(0,1)):
            raise ValueError('Ambient occlusion requires explicit fixed-profile laboratory intent.')
        return value if value['sequence']>previous else None
    if isinstance(value,dict)and value.get('operation')=='shadow':
        if(set(value)!={'operation','sequence','enabled'}or shadow_allowed is not True
            or type(value['sequence'])is not int or not 1<=value['sequence']<=1000000
            or type(value['enabled'])is not int or value['enabled']not in(0,1)):
            raise ValueError('Shadow filter requires explicit fixed-profile laboratory intent.')
        return value if value['sequence']>previous else None
    if isinstance(value,dict)and value.get('operation')=='water':
        if water_palette_allowed:raise ValueError('Water palette cases require the explicit variant command.')
        if(set(value)!={'operation','sequence','enabled'}or water_allowed is not True
            or type(value['sequence'])is not int or not 1<=value['sequence']<=1000000
            or type(value['enabled'])is not int or value['enabled']not in(0,1)):
            raise ValueError('WaterProgram requires explicit staged source-bound intent.')
        return value if value['sequence']>previous else None
    if isinstance(value,dict) and value.get('operation')=='capture':
        return value if capture_command(raw,previous) is not None else None
    if isinstance(value,dict) and value.get('operation')=='crash-watch-admit':
        if (set(value)!={'operation','sequence'} or not crash_watch_allowed
                or type(value['sequence'])is not int or not 1<=value['sequence']<=1000000):
            raise ValueError('Crash watcher admission requires the explicit diagnostic owner mode.')
        return value if value['sequence']>previous else None
    if isinstance(value,dict) and value.get('operation')=='comparison':
        if (set(value)!={'operation','sequence','enabled'} or not comparison_allowed
                or type(value['sequence'])is not int or not 1<=value['sequence']<=1000000
                or type(value['enabled'])is not int or value['enabled']not in(0,1)):
            raise ValueError('Comparison requires the explicit initialized diagnostic mode.')
        return value if value['sequence']>previous else None
    if (not isinstance(value,dict) or set(value)!={'operation','sequence','enabled'}
            or value['operation']!='effects' or type(value['sequence'])is not int
            or not 1<=value['sequence']<=1000000 or type(value['enabled'])is not int
            or value['enabled']<0 or value['enabled']&~material_flags or not material_flags):
        raise ValueError('Only prepared material subsets and bounded capture are supported.')
    if palette_allowed:
        raise ValueError('Palette cases require the explicit variant command.')
    return value if value['sequence']>previous else None


def measurement_budget_expired(now, deadline, already_reported):
    """A healthy client's unsaved state is never discarded for a time budget."""
    return now >= deadline and not already_reported


class LiveMemoryReport:
    """Publish completed existing checks, without changing scanning or policy."""
    def __init__(self,path,pid,record_sha256):
        self.path=Path(path);self.pid=pid;self.record_sha256=record_sha256;self.last=None

    def publish(self,sentinel):
        guardian=sentinel.snapshot()
        stamp=tuple(guardian[key]for key in ('samples','max_pressure','monitor_failed','recovery_required'))
        if stamp==self.last:return False
        value=dict(format='s7-memory-live-v1',pid=self.pid,owned_record_sha256=self.record_sha256,
                   sampled_monotonic_ns=time.monotonic_ns(),guardian=guardian)
        temporary=self.path.with_suffix('.tmp')
        temporary.write_text(json.dumps(value),encoding='utf-8')
        os.replace(temporary,self.path);self.last=stamp
        return True


def run(root, output, *, width=1920, height=1080, seconds=900, on_initialized=None,
        hook=None, output_width=1280, output_height=720, observer_catalog=None,
        observer_sha256=None, material_capture=None, material_capture_sha256=None,
        high_quality=False, material_effects=(), effects_initially_off=False,detailed_shadows=False,comparison_uv=False,
        crash_watch=False,material_config_version=1,baseline_crash_watch=False,water_program=False,water_variant='A',terrain_macro=False,shadow_filter=False,ambient_occlusion=False,ambient_profile='A',leaf_hemisphere=False,baseline_metric_profile=False,leaf_wrapped=False,leaf_palette=False,water_palette=False,shader_overlay=False):
    if type(shader_overlay)is not bool or (shader_overlay and not water_palette):
        raise ValueError('Shader overlay requires explicit isolated WaterPalette intent.')
    if (type(water_palette)is not bool or (water_palette and
            (baseline_crash_watch or hook is None or observer_catalog is None or not high_quality or not water_program
             or material_config_version!=1 or tuple(material_effects)!=('improved_water',)
             or leaf_wrapped or leaf_hemisphere or leaf_palette or terrain_macro or shadow_filter
             or ambient_occlusion or comparison_uv or crash_watch))):
        raise ValueError('Water palette requires explicit isolated observed High-quality WaterProgram intent.')
    if (type(leaf_palette)is not bool or (leaf_palette and
            (baseline_crash_watch or hook is None or observer_catalog is None or not high_quality or material_config_version!=4
             or tuple(material_effects)!=('improved_vegetation',) or leaf_wrapped or leaf_hemisphere or terrain_macro
             or water_program or shadow_filter or ambient_occlusion or comparison_uv or crash_watch))
            or (material_config_version==4 and not leaf_palette)):
        raise ValueError('Leaf palette requires explicit isolated observed High-quality V4 intent.')
    if (type(leaf_wrapped)is not bool or (leaf_wrapped and
            (baseline_crash_watch or hook is None or not high_quality or material_config_version!=3
             or tuple(material_effects)!=('improved_vegetation',) or leaf_hemisphere or terrain_macro
             or water_program or shadow_filter or ambient_occlusion or comparison_uv))
            or (material_config_version==3 and not leaf_wrapped)):
        raise ValueError('Leaf wrapped diffuse requires explicit isolated High-quality V3 vegetation-only intent.')
    if (type(leaf_hemisphere)is not bool or (leaf_hemisphere and
            (baseline_crash_watch or hook is None or not high_quality or material_config_version!=2
             or tuple(material_effects)!=('improved_vegetation',)))):
        raise ValueError('Leaf hemisphere requires explicit source-bound High-quality V2 vegetation-only intent.')
    if (type(ambient_profile)is not str or ambient_profile not in('A','B')
            or ambient_profile=='B' and ambient_occlusion is not True):
        raise ValueError('Select explicit ambient profile A or B with its own diagnostic intent.')
    ambient_operation='ambient_v2' if ambient_profile=='B' else 'ambient'
    ambient_version=2 if ambient_profile=='B' else 1
    if (type(ambient_occlusion)is not bool or (ambient_occlusion and
            (baseline_crash_watch or hook is None or observer_catalog is None or not high_quality or shadow_filter))):
        raise ValueError('Ambient occlusion requires exclusive source-bound OBSERVE and High-quality laboratory mode.')
    if (type(shadow_filter)is not bool or (shadow_filter and
            (baseline_crash_watch or hook is None or observer_catalog is None or not high_quality))):
        raise ValueError('Shadow filter requires explicit source-bound OBSERVE and High-quality laboratory mode.')
    if (type(terrain_macro)is not bool or (terrain_macro and
            (baseline_crash_watch or hook is None or not high_quality or material_config_version!=2
             or 'improved_terrain'not in material_effects or 'improved_water'in material_effects))):
        raise ValueError('Terrain macro requires explicit source-bound High-quality V2 terrain without legacy River.')
    if type(water_variant)is not str or water_variant not in('A','B'):raise ValueError('Select fixed WaterProgram variant A or B.')
    if type(water_program)is not bool or(water_program and(baseline_crash_watch or hook is None or not high_quality or 'improved_water'not in material_effects)):
        raise ValueError('WaterProgram requires explicit source-bound High-quality graphics mode.')
    if type(baseline_crash_watch)is not bool:raise ValueError('Explicit Boolean baseline mode required.')
    if (type(baseline_metric_profile)is not bool or (baseline_metric_profile
            and (baseline_crash_watch is not True or crash_watch is not True))):
        raise ValueError('The fixed Metric profile requires explicit baseline and crash-watch intent.')
    if baseline_crash_watch:
        from . import crash_watch as baseline_watcher
        baseline_watcher.baseline_options(baseline=True,crash_watch=crash_watch,hook=hook,
            callback=on_initialized,observer=observer_catalog,capture=material_capture,materials=material_effects,
            comparison=comparison_uv,detailed_shadows=detailed_shadows,initially_off=effects_initially_off,
            config_version=material_config_version)
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
        if shadow_filter and load_hook.verify_build(hook)[1].get('diagnostic_abi',{}).get('shadow')!=12:
            raise ValueError('This exact build does not declare the fixed shadow diagnostic.')
        if ambient_occlusion and load_hook.verify_build(hook)[1].get('diagnostic_abi',{}).get(ambient_operation)!=12:
            raise ValueError('This exact build does not declare the fixed ambient diagnostic.')
    elif observer_catalog is not None or material_capture is not None:
        raise ValueError('Observation requires the fixed source-bound native hook.')
    capture_config = None
    material_keys=('improved_water','improved_terrain','improved_vegetation')
    if (not isinstance(material_effects,(list,tuple)) or len(set(material_effects))!=len(material_effects)
            or any(key not in material_keys for key in material_effects)
            or type(effects_initially_off)is not bool or type(detailed_shadows)is not bool or type(comparison_uv)is not bool
            or type(crash_watch)is not bool or type(material_config_version)is not int or material_config_version not in(1,2,3,4)
            or (material_config_version==2 and 'improved_water'in material_effects and not water_program)
            or (material_effects and (hook is None or not high_quality))):
        raise ValueError('Native materials require an explicit exact hook and High shader quality.')
    if comparison_uv and (hook is None or observer_catalog is None):
        raise ValueError('Comparison requires an explicit source-bound OBSERVE diagnostic.')
    if crash_watch and hook is None and not baseline_crash_watch:raise ValueError('Crash watcher requires the exact owned native build checkpoint.')
    if material_capture is not None:
        material_capture = lab_path(material_capture)
        with material_capture.open('rb') as stream:
            capture_config = stream.read(953)
        if (observer_catalog is None or len(capture_config) != 952
                or struct.unpack_from('<2I', capture_config) != (952, 1)
                or hashlib.sha256(capture_config).hexdigest() != material_capture_sha256):
            raise ValueError('Invalid or changed targeted observation configuration.')
    output.mkdir()
    effects_config=water_config=None
    material_flags=0
    material_selection=tuple(key for key in material_effects if not(water_program and key=='improved_water'))
    if material_selection:
        from tools.installer import native_graphics_settings
        from tools.material_shaders import staging,staging_v2
        axis=width*100//output_width
        if width*100!=axis*output_width or height*100!=axis*output_height:
            raise ValueError('Material laboratory requires a supported exact render scale.')
        selected={**native_graphics_settings.DEFAULTS,'render_scale':axis}
        selected.update(dict.fromkeys(material_selection,True))
        extra={}
        stage=staging.prepare
        if material_config_version==4:
            _,build=load_hook.verify_build(hook)
            if build.get('abi',{}).get('effects_v4')!=4120:raise ValueError('This exact build does not declare material V4.')
            from tools.material_shaders import staging_leaf_palette
            stage=staging_leaf_palette.prepare
            extra=dict(config_version=4,available_config_versions=(4,),worker_overhead_bytes=load_hook.worker_overhead(executable,hook))
        elif material_config_version==3:
            _,build=load_hook.verify_build(hook)
            if build.get('abi',{}).get('effects_v3')!=7204:raise ValueError('This exact build does not declare material V3.')
            from tools.material_shaders import staging_leaf_wrapped
            stage=staging_leaf_wrapped.prepare
            extra=dict(config_version=3,available_config_versions=(3,),worker_overhead_bytes=load_hook.worker_overhead(executable,hook))
        elif material_config_version==2:
            _,build=load_hook.verify_build(hook)
            if build.get('abi',{}).get('effects_v2')!=6180:raise ValueError('This exact build does not declare material V2.')
            if leaf_hemisphere:
                from tools.material_shaders import staging_leaf_hemi
                stage=staging_leaf_hemi.prepare
            elif terrain_macro:
                from tools.material_shaders import staging_macro
                stage=staging_macro.prepare
            else:
                stage=staging_v2.prepare
            extra=dict(config_version=2,available_config_versions=(2,),worker_overhead_bytes=load_hook.worker_overhead(executable,hook))
        prepared=stage(root,selected,dict(width=output_width,height=output_height,mode='windowed'),
            available_features=('supersampling',*material_keys),available_scales=(100,150,200),
            native_report=output/'hook-state.private.json',verified_platform=value,private_case=True,**extra)
        effects_config=prepared.pop('config');material_flags=prepared['material_flags']
        (output/'effects-config.bin').write_bytes(effects_config)
        (output/'material-stage.private.json').write_text(json.dumps(prepared,indent=2),encoding='utf-8')
    if water_program:
        from tools.material_shaders import water_variants,staging
        _,build=load_hook.verify_build(hook)
        if build.get('abi',{}).get('water')!=1076:raise ValueError('This exact build does not declare WaterProgram.')
        if water_palette:
            if build.get('abi',{}).get('water_palette')!=3096:raise ValueError('This exact build does not declare WaterPalette.')
            if shader_overlay and build.get('diagnostic_abi',{}).get('overlay')!=12:
                raise ValueError('This exact build does not declare the shader overlay diagnostic.')
            from tools.material_shaders import water_palette_staging
            prepared=water_palette_staging.prepare(root,(width,height),native_report=output/'hook-state.private.json',
                verified_platform=value,available_features=('improved_water',),available_config_versions=(2,),
                worker_overhead_bytes=load_hook.worker_overhead(executable,hook),private_case=True)
        else:
            prepared=water_variants.prepare(root,(width,height),variant=water_variant,native_report=output/'hook-state.private.json',verified_platform=value,
                available_features=('improved_water',),private_case=True)
        water_config=prepared.pop('config')
        (output/('water-palette-config.bin' if water_palette else 'water-config.bin')).write_bytes(water_config)
        (output/('water-palette-stage.private.json' if water_palette else 'water-stage.private.json')).write_text(json.dumps(prepared,indent=2),encoding='utf-8')
        if effects_config is None:
            effects_config=struct.pack('<5I4f',4132,1,0,width,height,1.15,.94,1.2,1.1)+bytes(3072)+staging._wide(str(output/'material-effects.private.json'))
            (output/'effects-config.bin').write_bytes(effects_config)
    native_material_flags=material_flags|(1 if water_program else 0)
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
        ROOT / 'tools/shadow_detail/profile_guard.py', ROOT / 'tools/graphics_lab/recover.py',
        ROOT / 'tools/graphics_lab/load_hook.py', ROOT / 'tools/supersampling/hook_probe.py')}
    if crash_watch:
        watcher=ROOT/'tools/graphics_lab/crash_watch.py'
        checkpoint[watcher.relative_to(ROOT).as_posix()]=digest(watcher)
    if baseline_crash_watch:
        checkpoint={name:digest(ROOT/name)for name in baseline_watcher.baseline_source_paths(metric=baseline_metric_profile)}
    for name, pin in checkpoint.items():
        source = ROOT / name
        target = output / 'source' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        if digest(source) != pin or digest(target) != pin:
            raise RuntimeError('The graphics controller changed during source checkpoint creation.')
    (output / 'checkpoint.private.json').write_text(json.dumps(dict(
        files=immutable, sources=checkpoint, requested_render_size=[width, height],
        reshade_requested=False,detailed_shadows_requested=detailed_shadows,
        comparison_uv_requested=comparison_uv,crash_watch_allowed=crash_watch and not baseline_crash_watch,
        baseline_mode=baseline_watcher.BASELINE_MODE if baseline_crash_watch else None,
        baseline_crash_watch_allowed=baseline_crash_watch,native_graphics_hook_requested=hook is not None,
        baseline_metric_profile_requested=baseline_metric_profile,
        material_effects_requested=list(material_effects),
        water_program_requested=water_program,
        water_variant=water_variant if water_program and not water_palette else None,
        water_palette_requested=water_palette,
        shader_overlay_requested=shader_overlay,
        material_config_version=material_config_version,terrain_macro_requested=terrain_macro,
        leaf_hemisphere_requested=leaf_hemisphere,
        leaf_wrapped_requested=leaf_wrapped,
        leaf_palette_requested=leaf_palette,
        shadow_filter_requested=shadow_filter,ambient_occlusion_requested=ambient_occlusion,
        ambient_profile=ambient_profile if ambient_occlusion else None), indent=2), encoding='utf-8')
    recovery.prepare(root, output, critical=immutable,
        targets={'options': options, 'runtime': runtime, 'config': config_file},
        shared=shared_profile_roots())
    game = service = sentinel = None
    failure = None
    timed_out = False
    budget_expired = False
    try:
        options.parent.mkdir(parents=True, exist_ok=True)
        option_bytes = display_settings.windowed_options(previous_options or b'')
        if high_quality:
            from tools.d3d11 import settings as lab_settings
            quality = lab_settings.read_defaults(option_bytes)
            quality.update(dict.fromkeys(lab_settings.LEVELS, 2))
            quality.update(dict.fromkeys(lab_settings.EFFECTS, 1))
            quality['DepthOfField'] = 0
            option_bytes = lab_settings.replace_game_values(option_bytes, quality)
        options.write_bytes(option_bytes)
        config_file.write_bytes(graphics_profile.CONFIG + b'd3d9.floatEmulation = Strict\n')
        if value['edition'] == 'history':
            with (output / 'service.private.jsonl').open('xb') as log, (output / 'service-errors.private.txt').open('xb') as errors:
                service = subprocess.Popen([
                    str(root / 'Runtime/dotnet/dotnet.exe'), str(root / 'Server/S7Patcher.dll'),
                    '--local-config-server', '--local-keepalive-probe', '--http-port', '0',
                    '--udp-port', '0', '--seconds', '86400'],
                    stdout=log, stderr=errors, creationflags=subprocess.CREATE_NO_WINDOW)
            configuration, port = launch_local.service_ready(service, output / 'service.private.jsonl')
            launch_local.write_configuration(root / 'LocalData', configuration, port,
                                             True, display_name='GraphicsLab')
        environment = clean_environment(os.environ)
        if detailed_shadows:environment['S7_DETAILED_SHADOWS']='1'
        environment['DXVK_LOG_PATH'] = str(output)
        environment['DXVK_CONFIG_FILE'] = str(config_file)
        arguments = ('-vSynch', '-noMapInMainMenu', '-width', str(width), '-height', str(height))
        initialize = on_initialized
        if hook is not None:
            def initialize(owned):
                receipt = load_hook.initialize(owned, hook, render=(width, height),
                    output=(output_width, output_height), report=output / 'hook-state.private.json',
                    observer_catalog=observer_catalog, observer_sha256=observer_sha256,material_flags=native_material_flags,
                    shadow_filter=shadow_filter,ambient_occlusion=ambient_occlusion)
                (output / 'hook-initialized.private.json').write_text(
                    json.dumps(receipt, indent=2), encoding='utf-8')
                _, build = load_hook.verify_build(hook)
                (output / 'hook-build.private.json').write_text(json.dumps(build, indent=2), encoding='utf-8')
                if capture_config is not None:
                    observed = load_hook.invoke(owned, hook, 'configure', capture_config)
                    (output / 'material-configured.private.json').write_text(
                        json.dumps(observed, indent=2), encoding='utf-8')
                if effects_config is not None:
                    initial=bytearray(effects_config)
                    if effects_initially_off:struct.pack_into('<I',initial,8,0)
                    configured=load_hook.invoke(owned,hook,'effects',bytes(initial))
                    (output/'effects-configured.private.json').write_text(json.dumps(configured,indent=2),encoding='utf-8')
                if comparison_uv:
                    configured=load_hook.invoke(owned,hook,'comparison',struct.pack('<3I',12,1,1))
                    (output/'comparison-configured.private.json').write_text(json.dumps(configured,indent=2),encoding='utf-8')
                if water_config is not None:
                    initial=bytearray(water_config)
                    if effects_initially_off:struct.pack_into('<I',initial,16,0)
                    configured=load_hook.invoke(owned,hook,'water_palette' if water_palette else 'water',bytes(initial))
                    (output/('water-palette-configured.private.json' if water_palette else 'water-configured.private.json')).write_text(json.dumps(configured,indent=2),encoding='utf-8')
                if shader_overlay:
                    configured=load_hook.invoke(owned,hook,'overlay',struct.pack('<3I',12,1,1))
                    (output/'overlay-configured.private.json').write_text(json.dumps(configured,indent=2),encoding='utf-8')
                if shadow_filter:
                    configured=load_hook.invoke(owned,hook,'shadow',struct.pack('<3I',12,1,0 if effects_initially_off else 1))
                    (output/'shadow-configured.private.json').write_text(json.dumps(configured,indent=2),encoding='utf-8')
                if ambient_occlusion:
                    configured=load_hook.invoke(owned,hook,ambient_operation,struct.pack('<3I',12,ambient_version,0 if effects_initially_off else 1))
                    (output/'ambient-configured.private.json').write_text(json.dumps(configured,indent=2),encoding='utf-8')
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
        live_memory=LiveMemoryReport(output/'memory-live.private.json',game.pid,
            hashlib.sha256((output/'owned.private.json').read_bytes()).hexdigest())
        live_memory.publish(sentinel)
        print(json.dumps(dict(event='graphics-game-launched', pid=game.pid,
                              requested_render_size=[width, height], portable=True,
                              reshade_requested=False)), flush=True)
        deadline = time.monotonic() + seconds
        last_command = None
        last_sequence = 0
        while game.poll() is None:
            live_memory.publish(sentinel)
            if sentinel.summary['monitor_failed'] or sentinel.summary['recovery_required']:
                raise RuntimeError('The memory guard failed or confirmed pressure; stop this owned experiment.')
            if measurement_budget_expired(time.monotonic(), deadline, budget_expired):
                budget_expired = True
                notification = dict(event='graphics-measurement-budget-ended',
                    regular_close_required=True, game_terminated=False)
                (output / 'close-needed.private.json').write_text(json.dumps(notification), encoding='utf-8')
                print(json.dumps(notification), flush=True)
                # Keep ownership, memory monitoring and the loopback service
                # alive until the original UI closes. A time limit is not a
                # hung-client finding and does not permit losing unsaved play.
            command = output / 'native-command.json'
            if (hook is not None or baseline_crash_watch) and command.exists():
                command = lab_path(command)
                with command.open('rb') as stream:
                    raw = stream.read(4097)
                identity = hashlib.sha256(raw).hexdigest()
                if identity != last_command:
                    last_command = identity
                    try:
                        request = baseline_watcher.baseline_command(raw,last_sequence) if baseline_crash_watch else native_command(raw, last_sequence, material_flags,comparison_uv,crash_watch,water_config is not None,shadow_filter,ambient_occlusion,leaf_palette,water_palette)
                        if request is not None:
                            sequence=request['sequence'];last_sequence=sequence
                            operation=request['operation']
                            if operation=='water':
                                changed=bytearray(water_config);struct.pack_into('<I',changed,16,request['enabled'])
                                receipt=load_hook.invoke(game,hook,'water',bytes(changed))
                            elif operation=='baseline-crash-watch-admit':
                                receipt=baseline_watcher.baseline_admit(game,output,load_and_new_save_confirmed=True)
                            elif operation=='crash-watch-admit':
                                from tools.graphics_lab import crash_watch as watcher
                                # Explicit owner command is issued after the
                                # original UI loaded and saved a new free slot.
                                receipt=watcher.admit(game,output,load_and_new_save_confirmed=True)
                            else:
                                payload=struct.pack('<3I',12,1,sequence)
                                if operation=='variant':
                                    payload=bytearray(water_config if water_palette else effects_config)
                                    struct.pack_into('<I',payload,16 if water_palette else 8,request['enabled'])
                                    struct.pack_into('<I',payload,20,request['variant'])
                                    payload=bytes(payload);operation='water_palette' if water_palette else 'effects'
                                elif operation=='effects':
                                    payload=bytearray(effects_config)
                                    struct.pack_into('<I',payload,8,request['enabled']);payload=bytes(payload)
                                elif operation=='ambient':
                                    operation=ambient_operation;payload=struct.pack('<3I',12,ambient_version,request['enabled'])
                                elif operation in('comparison','shadow'):payload=struct.pack('<3I',12,1,request['enabled'])
                                receipt = load_hook.invoke(game, hook, operation, payload)
                            (output / f'command-{sequence}.private.json').write_text(
                                json.dumps(receipt, indent=2), encoding='utf-8')
                    except (ValueError, RuntimeError, OSError) as error:
                        # An optional measurement refusal must not discard a running game.
                        detail=dict(type=type(error).__name__,sequence=last_sequence)
                        if baseline_crash_watch:detail['message']=str(error)[:512]
                        (output / 'command-error.private.json').write_text(json.dumps(
                            detail), encoding='utf-8')
            time.sleep(.25)
    except BaseException as error:
        failure = error
    finally:
        def stop_game():
            nonlocal timed_out
            if game and game.poll() is None:
                game.terminate()
                timed_out = True
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
                      measurement_budget_expired=budget_expired,
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
    parser.add_argument('--observer-catalog', type=Path)
    parser.add_argument('--observer-sha256')
    parser.add_argument('--material-capture', type=Path)
    parser.add_argument('--material-capture-sha256')
    parser.add_argument('--high-quality', action='store_true', help='Matched high native quality with DOF off in private options only')
    parser.add_argument('--material-effects',nargs='+',choices=('improved_water','improved_terrain','improved_vegetation'),default=[])
    parser.add_argument('--effects-initially-off',action='store_true')
    parser.add_argument('--water-program',action='store_true',help='Explicit new recipient-generated water pixel shader; legacy River factors stay disabled')
    parser.add_argument('--shader-overlay',action='store_true',help='Native live shader selector in the game; requires --water-palette')
    parser.add_argument('--water-palette',action='store_true',help='Three fixed WaterProgram alternatives in one process; requires --water-program')
    parser.add_argument('--water-variant',choices=('A','B'),default='A',help='Explicit fixed water candidate: A matte, B balanced gloss')
    parser.add_argument('--material-config-version',type=int,choices=(1,2,3,4),default=1,
        help='Explicit versioned terrain/leaf transport; V2 does not include the separate new water shader')
    parser.add_argument('--terrain-macro',action='store_true',help='Explicit source-pinned macro terrain recipe; V2 historical recipes stay unchanged')
    parser.add_argument('--leaf-hemisphere',action='store_true',help='Explicit original-leaf ambient hemisphere recipe; vegetation-only V2 trial')
    parser.add_argument('--leaf-wrapped',action='store_true',help='Explicit isolated leaf diffuse-lighting recipe; original vertex shader and V3 transport')
    parser.add_argument('--leaf-palette',action='store_true',help='Three admitted alternative Leaf PS programs in one process; explicit V4 transport')
    parser.add_argument('--shadow-filter',action='store_true',help='Fixed guarded 2048 far-shadow filter diagnostic; requires OBSERVE')
    parser.add_argument('--ambient-occlusion',action='store_true',help='Fixed original S7 SSAO strength diagnostic; excludes the shadow-filter trial')
    parser.add_argument('--ambient-profile',choices=('A','B'),default='A',help='Explicit fixed SSAO profile: A caps at1; B caps at1.25')
    parser.add_argument('--detailed-shadows',action='store_true',help='Existing guarded adapter 4096 shadow baseline; no new filter activation')
    parser.add_argument('--comparison-uv',action='store_true',help='Diagnostic-only draw-local original River PS80 isolation, never a production option')
    parser.add_argument('--crash-watch',action='store_true',help='Permit explicit post-save admission of a bounded own-child crash watcher')
    parser.add_argument('--baseline-crash-watch',action='store_true',help='Explicit separate admission with native graphics modules absent')
    parser.add_argument('--baseline-metric-profile',action='store_true',help='Fixed Steam SDK pre/post-destructor diagnostic; requires a fresh baseline owner checkpoint')
    args = parser.parse_args()
    run(args.bundle, args.output, width=args.width, height=args.height, seconds=args.seconds,
        hook=args.hook, output_width=args.output_width, output_height=args.output_height,
        observer_catalog=args.observer_catalog, observer_sha256=args.observer_sha256,
        material_capture=args.material_capture, material_capture_sha256=args.material_capture_sha256,
        high_quality=args.high_quality,material_effects=args.material_effects,effects_initially_off=args.effects_initially_off,
        detailed_shadows=args.detailed_shadows,comparison_uv=args.comparison_uv,crash_watch=args.crash_watch,
        material_config_version=args.material_config_version,baseline_crash_watch=args.baseline_crash_watch,water_program=args.water_program,
        water_variant=args.water_variant,terrain_macro=args.terrain_macro,shadow_filter=args.shadow_filter,
        ambient_occlusion=args.ambient_occlusion,ambient_profile=args.ambient_profile,leaf_hemisphere=args.leaf_hemisphere,
        baseline_metric_profile=args.baseline_metric_profile,leaf_wrapped=args.leaf_wrapped,leaf_palette=args.leaf_palette,water_palette=args.water_palette,shader_overlay=args.shader_overlay)


if __name__ == '__main__':
    main()
