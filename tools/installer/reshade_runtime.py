"""Opt-in, per-copy ReShade tone runtime for the Modern (DXVK) renderer.

Download the official signed setup only on explicit enablement. Extract the
pinned x86 Vulkan layer into LocalData; never run setup, touch the registry,
replace d3d9.dll or activate a system-wide layer. Maximum additionally uses one
hash-pinned WePlay read-only sun-clock bridge. External add-ons remain disabled.
"""
import hashlib
import configparser
import io
import json
import os
from pathlib import Path
import re
import urllib.request
import zipfile

URL = 'https://reshade.me/downloads/ReShade_Setup_6.8.0.exe'
SETUP_SHA256 = '207aea16205fbf952bc8fe1879966672454cf04002e7ad34237c7990a5b3c0b4'
DLL_SHA256 = '538998f66c0197adcdeadfe8e5ea19dd6ca3253e40399476d291f385ce518b88'
MANIFEST_SHA256 = 'dbfdbadb8c64b398b2f8bf30469841ab8c35e10c9da40ba69b966559d5fa15f6'
ADDON_URL = 'https://reshade.me/downloads/ReShade_Setup_6.8.0_Addon.exe'
ADDON_SETUP_SHA256 = 'afe4c8f13048306307983b8b3d41d5bf00a86820440b0e57dea10950e1176445'
ADDON_DLL_SHA256 = 'da430e0a9c6eecefa0d1b27d05e16c426fb5d04e808b194d914eaac4b31bc0f8'
CLOCK_SHA256 = 'bc397467e09ffdad3c00c3f4654aa54766bf97a8cc7c5d26a22e30fee3aaa2e4'
CLOCK_FILE = 'Runtime/Graphics/WePlayAtmosphere.addon32'
# Versioned profiles preserve the earlier installed runtime and never rewrite
# a shader/configuration which an already running game may have loaded.
FOLDER = 'LocalData/ReShade-Visual-v2'
OPTIONS = ('midsummer_tone', 'enhanced_aa', 'contact_shadows', 'maximum_fx')
MAXIMUM_ASSETS = ('dh_uber_rt.fx', 'dh_uber_motion.fx', 'DH-LICENSE.txt',
                  'ReShade.fxh', 'ReShadeUI.fxh', 'SMAA.fx', 'SMAA.fxh',
                  'AreaTex.png', 'SearchTex.png', 'SweetFX-LICENSE.txt',
                  'WePlay-World.fxh', 'Atmosphere-LICENSE.txt', 'THIRD-PARTY.md')
CONFIG = b'''[INSTALL]
Logging=1

[GENERAL]
EffectSearchPaths=.\\Shaders
TextureSearchPaths=.\\Textures
PreprocessorDefinitions=RESHADE_DEPTH_INPUT_IS_REVERSED=0
PresetPath=.\\WePlay-Midsummer.ini
PerformanceMode=0
NoDebugInfo=1
EffectCachePath=.\\Cache

[OVERLAY]
TutorialProgress=4
ShowPresetName=0

[DEPTH]
DepthCopyBeforeClears=1
UseAspectRatioHeuristics=1

[ADDON]
AddonPath=.\\NoAddons
'''


def _asset(name):
    here = Path(__file__).resolve().parent
    flat = here / name
    return flat if flat.is_file() else here.parent / 'midsummer' / name


def _plain(path):
    import graphics_profile
    return graphics_profile._plain(path)


def _read(path, limit):
    import graphics_profile
    return graphics_profile._read(_plain(path), limit)


def _options(options):
    if options is None:
        return (True, False, False, False)
    if not isinstance(options, dict):
        raise ValueError('Ungueltige ReShade-Auswahl.')
    values = tuple(options.get(name, False) for name in OPTIONS)
    if any(type(value) is not bool for value in values):
        raise ValueError('Ungueltige ReShade-Auswahl.')
    return values


def _folder(root, options):
    mask = sum(int(value) << i for i, value in enumerate(_options(options)))
    fingerprint = hashlib.sha256(_config(options) + _runtime(options)[2].encode('ascii') + _preset(options))
    for name, data in _profile_files(options, root).items():
        fingerprint.update(name.encode('utf-8') + b'\0' + data)
    version = fingerprint.hexdigest()[:12]
    return _plain(Path(root) / FOLDER / ('profile-' + str(mask) + '-' + version))


def _runtime(options):
    return ((ADDON_URL, ADDON_SETUP_SHA256, ADDON_DLL_SHA256) if any(_options(options)[2:])
            else (URL, SETUP_SHA256, DLL_SHA256))


def _config(options):
    return CONFIG.replace(b'AddonPath=.\\NoAddons', b'AddonPath=.\\ManagedAddons') if _options(options)[3] else CONFIG


def _clock(root):
    data = _read(Path(root) / CLOCK_FILE, 4 * 1024 * 1024)
    if hashlib.sha256(data).hexdigest() != CLOCK_SHA256:
        raise ValueError('Der gepruefte WePlay-Sonnenzeitgeber fehlt oder wurde veraendert. WePlay aktualisieren.')
    return data


def _preset(options):
    tone, aa, contacts, maximum = _options(options)
    raw = _asset('WePlay-Midsummer.ini').read_text(encoding='utf-8')
    raw = raw.replace('Strength=0.650000', 'Strength=' + ('0.650000' if tone else '0.000000'))
    if maximum:
        techniques = ['WePlay_Midsummer@WePlay-Midsummer.fx',
                      'DH_UBER_MOTION_020@dh_uber_motion.fx', 'DH_UBER_RT@dh_uber_rt.fx',
                      'WePlay_Maximum@WePlay-Midsummer.fx']
        if aa: techniques.append('SMAA@SMAA.fx')
        raw = raw.replace('WePlay_Midsummer@WePlay-Midsummer.fx', ','.join(techniques))
    raw = (raw.rstrip() + '\nEdgeStrength=' + ('0.800000' if aa and not maximum else '0.000000')
            + '\nContactStrength=' + ('0.220000' if contacts else '0.000000')
            + '\nMaximumStrength=' + ('1.000000' if maximum else '0.000000')
            + '\nHazeStrength=0.420000\nHeatStrength=0.850000\nDebugView=0\n')
    if maximum:
        raw += '''
[dh_uber_motion.fx]
iMotionRadius=4
bUnjitterDepth=0

[dh_uber_rt.fx]
bSkyAt0=0
bDepthMulti5=0
bRemoveAmbient=1
fSourceAmbientIntensity=0.350000
fGIRenderScale=0.500000
iRTMaxRays=4
iSmoothSamples=8
iSmoothRadius=8
iGIFrameAccu=8
iAOFrameAccu=5
fGIBounce=0.450000
fGIHueBiais=0.600000
fGIDarkMerging=0.650000
fGILightMerging=0.250000
fGIFinalMerging=1.250000
fAOMultiplier=0.650000
fAOBoostFromGI=0.200000
bRreinhardFinalMerging=0
bSSR=0
iDebug=0

[SMAA.fx]
EdgeDetectionType=1
EdgeDetectionThreshold=0.075000
MaxSearchSteps=64
MaxSearchStepsDiagonal=20
CornerRounding=25
DebugOutput=0
'''
    return raw.encode('utf-8')


def _profile_files(options, root):
    files = {'Shaders/WePlay-Midsummer.fx': _asset('WePlay-Midsummer.fx').read_bytes(),
             'Shaders/WePlay-World.fxh': _asset('effects/WePlay-World.fxh').read_bytes()}
    if _options(options)[3]:
        files['ManagedAddons/WePlayAtmosphere.addon32'] = _clock(root)
        for name in MAXIMUM_ASSETS:
            directory = 'Textures' if name.endswith('.png') else ('Shaders' if name.endswith(('.fx', '.fxh')) else 'Licenses')
            files[directory + '/' + name] = _asset('effects/' + name).read_bytes()
    return files


def verify(root, options=None):
    folder = _folder(root, options)
    dll = _plain(folder / 'ReShade32.dll')
    if (not dll.is_file() or dll.stat().st_size > 16 * 1024 * 1024
            or hashlib.sha256(dll.read_bytes()).hexdigest() != _runtime(options)[2]):
        raise ValueError('Das gepruefte ReShade-Laufzeitmodul fehlt. Grafikauswahl erneut speichern.')
    raw_manifest = _read(folder / 'ReShade32.json', 16384)
    if hashlib.sha256(raw_manifest).hexdigest() != MANIFEST_SHA256:
        raise ValueError('Das ReShade-Layermodul wurde veraendert.')
    manifest = json.loads(raw_manifest.decode('utf-8'))
    layer = manifest.get('layer', {})
    if layer.get('name') != 'VK_LAYER_reshade' or layer.get('library_path') != '.\\ReShade32.dll':
        raise ValueError('Das ReShade-Layerprofil ist unbekannt.')
    no_addons = _plain(folder / 'NoAddons')
    if not no_addons.is_dir() or any(no_addons.iterdir()):
        raise ValueError('Der verwaltete ReShade-Ordner fuer externe Add-ons ist veraendert.')
    managed = _plain(folder / 'ManagedAddons')
    if _options(options)[3] and (not managed.is_dir() or {p.name for p in managed.iterdir()} != {'WePlayAtmosphere.addon32'}):
        raise ValueError('Der verwaltete ReShade-Sonnenzeitgeber-Ordner wurde veraendert.')
    if (any(_read(folder / name, 4 * 1024 * 1024) != data for name, data in _profile_files(options, root).items())
            or _plain(folder / 'WePlay-Midsummer.ini').read_bytes() != _preset(options)):
        raise ValueError('Das WePlay-Grafikprofil wurde veraendert. Grafikauswahl erneut speichern.')
    # ReShade writes UI/style defaults on a clean exit. Compare managed values,
    # not the complete byte stream, so the next ordinary launch still works.
    config = _read(folder / 'ReShade.ini', 65536)
    actual = configparser.ConfigParser(interpolation=None)
    expected = configparser.ConfigParser(interpolation=None)
    try:
        actual.read_string(config.decode('utf-8-sig'))
        expected.read_string(_config(options).decode('utf-8'))
        if any(actual.get(section, key, fallback=None) != value
               for section in expected.sections() for key, value in expected[section].items()):
            raise ValueError('Die verwalteten ReShade-Einstellungen wurden veraendert.')
    except (UnicodeError, configparser.Error) as error:
        raise ValueError('Die lokale ReShade-Konfiguration ist ungueltig.') from error
    return folder


def prepare(root, *, setup=None, options=None):
    import graphics_profile
    root = _plain(Path(root))
    url, setup_sha256, dll_sha256 = _runtime(options)
    folder = _folder(root, options)
    if folder.exists():
        return verify(root, options)
    graphics_profile.ensure_closed()
    # Reuse only the pinned official runtime; never migrate an old shader or
    # preset. This avoids a second download when upgrading the original tone.
    legacy = _plain(root / 'LocalData/ReShade-Midsummer')
    dll = manifest = None
    if setup is None:
        candidates = [legacy] if legacy.is_dir() else []
        if folder.parent.is_dir():
            candidates += sorted((p for p in folder.parent.iterdir()
                                  if re.fullmatch(r'profile-(?:[0-9]|1[0-5])-[0-9a-f]{12}',p.name)),key=lambda p:p.name)[:32]
        for candidate_folder in candidates:
            try:
                candidate = _read(candidate_folder / 'ReShade32.dll', 16 * 1024 * 1024)
                candidate_manifest = _read(candidate_folder / 'ReShade32.json', 16384)
            except (OSError, ValueError):
                continue
            if (hashlib.sha256(candidate).hexdigest() == dll_sha256
                    and hashlib.sha256(candidate_manifest).hexdigest() == MANIFEST_SHA256):
                dll, manifest = candidate, candidate_manifest
                break
    if dll is None and setup is None:
        request = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0', 'Referer': 'https://reshade.me/'})
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                raw = response.read(8 * 1024 * 1024 + 1)
        except OSError as error:
            raise ValueError('ReShade konnte nicht von der offiziellen Website geladen werden.') from error
    elif dll is None:
        raw = _plain(Path(setup)).read_bytes()
    if dll is None:
        if len(raw) > 8 * 1024 * 1024 or hashlib.sha256(raw).hexdigest() != setup_sha256:
            raise ValueError('Der offizielle ReShade-Download stimmt nicht mit der geprueften Version ueberein.')
        with zipfile.ZipFile(io.BytesIO(raw)) as archive:
            dll, manifest = archive.read('ReShade32.dll'), archive.read('ReShade32.json')
    if (hashlib.sha256(dll).hexdigest() != dll_sha256
            or hashlib.sha256(manifest).hexdigest() != MANIFEST_SHA256):
        raise ValueError('ReShade-Laufzeitpruefung fehlgeschlagen.')
    folder.parent.mkdir(parents=True, exist_ok=True)
    staging = _plain(folder.with_name(folder.name + '.preparing'))
    if staging.exists():
        raise ValueError('Eine unvollstaendige ReShade-Vorbereitung ist vorhanden.')
    staging.mkdir()
    (staging / 'ReShade32.dll').write_bytes(dll)
    (staging / 'ReShade32.json').write_bytes(manifest)
    (staging / 'NoAddons').mkdir()
    for name, data in _profile_files(options, root).items():
        target = staging / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    (staging / 'WePlay-Midsummer.ini').write_bytes(_preset(options))
    (staging / 'ReShade.ini').write_bytes(_config(options))
    staging.rename(folder)
    return verify(root, options)


def launch_environment(root, inherited, options=None):
    folder = verify(root, options)
    result = {key: value for key, value in inherited.items()
              if key.upper() != 'RESHADE_BASE_PATH_OVERRIDE'}
    # Explicit layer for this child only. Existing unrelated layer paths/names
    # remain usable. No implicit/global installation is performed.
    paths = result.get('VK_LAYER_PATH', '')
    layers = result.get('VK_INSTANCE_LAYERS', '')
    result['VK_LAYER_PATH'] = str(folder) + (os.pathsep + paths if paths else '')
    existing = [name for name in layers.split(os.pathsep) if name and name != 'VK_LAYER_reshade']
    result['VK_INSTANCE_LAYERS'] = os.pathsep.join([*existing, 'VK_LAYER_reshade'])
    result['RESHADE_BASE_PATH_OVERRIDE'] = str(folder)
    return result
