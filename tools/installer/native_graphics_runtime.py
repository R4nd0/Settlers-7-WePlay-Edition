"""Admit qualified native graphics and attach only during owned child startup.

Compiled feature support and per-edition/scaling qualification are separate.
An installed JSON file or a saved checkbox cannot grant a capability. Payload
pins below are populated only after real-game and final-package qualification.
"""
from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
import struct

import native_graphics_loader as loader
import native_graphics_settings as settings

DLL = 'WePlayNativeGraphics.dll'
MANIFEST = 'native-graphics.json'
DIRECTORY = 'Launcher/NativeGraphics'
FORMAT = 's7-native-graphics-payload-v1'
FEATURES = ('supersampling', 'improved_water', 'improved_terrain',
            'improved_vegetation', 'stable_shadows')
ORIGINAL_EDITIONS = (
    '4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f',
    '8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410',
    '76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab')
# SHA256 -> {manifest_sha256, editions: {original EXE SHA256: {features, scales}}}
# No source build, synthetic fixture or developer assertion qualifies a release.
QUALIFIED_BUILDS = {}


@dataclass(frozen=True)
class Capability:
    dll: Path
    sha256: str
    features: tuple
    scales: tuple


def payload(directory, executable_sha256):
    directory = loader.plain(directory)
    dll, manifest = (loader.plain(directory / name) for name in (DLL, MANIFEST))
    if dll.parent != directory or manifest.parent != directory:
        raise ValueError('native_graphics_payload_invalid')
    pin = loader.digest(dll)
    admitted = QUALIFIED_BUILDS.get(pin)
    if not admitted or loader.digest(manifest) != admitted['manifest_sha256']:
        raise ValueError('native_graphics_unavailable')
    with manifest.open('rb') as stream:
        raw = stream.read(16385)
    if len(raw) > 16384:
        raise ValueError('native_graphics_payload_invalid')
    value = json.loads(raw)
    if (not isinstance(value, dict) or set(value) != {'format', 'dll', 'sha256', 'abi', 'compiled_features'}
            or value['format'] != FORMAT or value['dll'] != DLL or value['sha256'] != pin
            or type(value['abi']) is not int or value['abi'] != 2
            or not isinstance(value['compiled_features'], list)
            or any(item not in FEATURES for item in value['compiled_features'])
            or len(set(value['compiled_features'])) != len(value['compiled_features'])):
        raise ValueError('native_graphics_payload_invalid')
    edition = admitted['editions'].get(executable_sha256)
    if (not edition or not set(edition['features']) <= set(value['compiled_features'])
            or any(type(scale) is not int or scale not in settings.SCALES for scale in edition['scales'])):
        raise ValueError('native_graphics_unavailable')
    return Capability(dll, pin, tuple(edition['features']), tuple(edition['scales']))


def installed(root):
    import platform_standalone
    root = loader.plain(root)
    _, executable, _ = platform_standalone.load(root)
    return payload(root / DIRECTORY, loader.digest(executable))


def distribution(directory):
    """A user-facing distribution must carry an admitted original-edition matrix."""
    directory = loader.plain(directory)
    for executable in ORIGINAL_EDITIONS:
        payload(directory, executable)
    return directory


def available(root):
    """Read-only UI availability; incomplete payloads expose no feature."""
    try:
        value = installed(root)
        return dict(features=list(value.features), scales=list(value.scales))
    except (OSError, ValueError, RuntimeError, KeyError, TypeError):
        return dict(features=[], scales=[100])


def require(root, selected, *, renderer='dxvk', quality=None, check_quality=False):
    requested = settings.requested_features(selected.get('native_graphics'))
    if not requested:
        return None
    if renderer != 'dxvk':
        raise ValueError('native_graphics_renderer_required')
    if any(selected.get(key, False) for key in
           ('midsummer_tone', 'enhanced_aa', 'contact_shadows', 'maximum_fx')):
        raise ValueError('native_graphics_effect_conflict')
    value = settings.validate(selected['native_graphics'])
    capability = installed(root)
    settings.require_features(value, capability.features)
    if value['render_scale'] not in capability.scales:
        raise ValueError('native_graphics_unavailable')
    dimensions = settings.render_size(value, selected.get('display'))
    if (check_quality or quality is not None) and any(value[key] for key in ('improved_water', 'improved_terrain', 'improved_vegetation')):
        import quality_settings
        quality = quality_settings.load(root) if quality is None else quality_settings.validate(quality)
        if quality['ShaderQuality'] != 2 or (value['improved_water'] and quality['WaterQuality'] != 2):
            raise ValueError('native_graphics_high_quality_required')
    return capability, dimensions


def configuration(dimensions, report):
    """Current v2 ABI; production never enables the diagnostic observer."""
    report = loader.plain(report)
    path = str(report).encode('utf-16le')
    if '\0' in str(report) or len(path) > 1022 or report.suffix.lower() != '.json':
        raise ValueError('native_graphics_path_invalid')
    numbers = [dimensions[key] for key in
               ('render_width', 'render_height', 'output_width', 'output_height')]
    return struct.pack('<6I', 3132, 2, *numbers) + path.ljust(1024, b'\0') + bytes(2084)


def render_arguments(arguments, dimensions):
    """Keep physical output placement; replace only validated render dimensions."""
    result = list(arguments)
    for flag, key in (('-width', 'render_width'), ('-height', 'render_height')):
        positions = [i for i, item in enumerate(result) if item == flag]
        if len(positions) > 1 or (positions and positions[0] + 1 >= len(result)):
            raise ValueError('native_graphics_arguments_invalid')
        if positions:
            result[positions[0] + 1] = str(dimensions[key])
        else:
            result.extend((flag, str(dimensions[key])))
    return tuple(result)


def prepare_launch(root, arguments, callback, *, renderer='dxvk'):
    import graphics_choice
    selected = graphics_choice._read_choice(root)
    admitted = require(root, selected, renderer=renderer, check_quality=True)
    if admitted is None:
        return arguments, callback
    import graphics_profile
    graphics_profile.ensure_closed()
    capability, dimensions = admitted
    import platform_standalone
    platform, executable, _ = platform_standalone.load(root)
    if loader.digest(loader.plain(executable.with_name('d3d9.dll'))) != loader.DXVK_SHA256:
        raise ValueError('native_graphics_renderer_required')
    local = loader.plain(Path(root) / 'LocalData')
    local.mkdir(exist_ok=True)
    report = loader.plain(local / 'native-graphics.private.json')
    initial = configuration(dimensions, report)
    material = None
    if any(selected['native_graphics'][key] for key in
           ('improved_water', 'improved_terrain', 'improved_vegetation')):
        try:
            from material_shaders import staging
        except ImportError:
            from tools.material_shaders import staging
        material = staging.prepare(root, selected['native_graphics'], selected['display'],
            available_features=capability.features, available_scales=capability.scales,
            native_report=report, verified_platform=platform)

    def initialize(owned):
        if callback is not None:
            callback(owned)
        loader.invoke(owned, capability.dll, capability.sha256, 'initialize', initial)
        if material is not None:
            loader.invoke(owned, capability.dll, capability.sha256, 'effects', material['config'])

    return render_arguments(arguments, dimensions), initialize
