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
import queue
import threading
import time
import uuid
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
SMAA_ASSETS = ('SMAA.fx', 'SMAA.fxh', 'ReShade.fxh', 'ReShadeUI.fxh',
               'AreaTex.png', 'SearchTex.png', 'SweetFX-LICENSE.txt', 'THIRD-PARTY.md')
SMAA_PRESET = '''
[SMAA.fx]
EdgeDetectionType=1
EdgeDetectionThreshold=0.075000
MaxSearchSteps=64
MaxSearchStepsDiagonal=20
CornerRounding=25
PredicationEnabled=0
DebugOutput=0
'''
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
    tone, aa, contacts, maximum = _options(options)
    if aa and not (tone or contacts or maximum):
        return CONFIG.replace(b'DepthCopyBeforeClears=1', b'DepthCopyBeforeClears=0')
    return CONFIG.replace(b'AddonPath=.\\NoAddons', b'AddonPath=.\\ManagedAddons') if maximum else CONFIG


def _clock(root):
    data = _read(Path(root) / CLOCK_FILE, 4 * 1024 * 1024)
    if hashlib.sha256(data).hexdigest() != CLOCK_SHA256:
        raise ValueError('Der gepruefte WePlay-Sonnenzeitgeber fehlt oder wurde veraendert. WePlay aktualisieren.')
    return data


def _preset(options):
    tone, aa, contacts, maximum = _options(options)
    if not (tone or contacts or maximum):
        technique = 'SMAA@SMAA.fx' if aa else ''
        return ('Techniques=' + technique + '\nTechniqueSorting=' + technique
                + '\n' + (SMAA_PRESET if aa else '')).encode('utf-8')
    raw = _asset('WePlay-Midsummer.ini').read_text(encoding='utf-8')
    raw = raw.replace('Strength=0.650000', 'Strength=' + ('0.650000' if tone else '0.000000'))
    techniques = ['WePlay_Midsummer@WePlay-Midsummer.fx']
    if maximum:
        techniques.extend(['DH_UBER_MOTION_020@dh_uber_motion.fx', 'DH_UBER_RT@dh_uber_rt.fx',
                           'WePlay_Maximum@WePlay-Midsummer.fx'])
    if aa:
        techniques.append('SMAA@SMAA.fx')
    raw = raw.replace('WePlay_Midsummer@WePlay-Midsummer.fx', ','.join(techniques))
    raw = (raw.rstrip() + '\nEdgeStrength=0.000000'
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

'''
    if aa:
        raw += SMAA_PRESET
    return raw.encode('utf-8')


def _profile_files(options, root):
    tone, aa, contacts, maximum = _options(options)
    files = {}
    if tone or contacts or maximum:
        files = {'Shaders/WePlay-Midsummer.fx': _asset('WePlay-Midsummer.fx').read_bytes(),
                 'Shaders/WePlay-World.fxh': _asset('effects/WePlay-World.fxh').read_bytes()}
    assets = SMAA_ASSETS if aa else ()
    if maximum:
        files['ManagedAddons/WePlayAtmosphere.addon32'] = _clock(root)
        assets = MAXIMUM_ASSETS
    for name in assets:
        directory = 'Textures' if name.endswith('.png') else ('Shaders' if name.endswith(('.fx', '.fxh')) else 'Licenses')
        files[directory + '/' + name] = _asset('effects/' + name).read_bytes()
    return files


def verify(root, options=None):
    return _verify(root, options, check_preset=True)

def _verify(root, options, *, check_preset):
    """Keep executable/assets/configuration checks strict during preset repair."""
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
            or check_preset and _read(folder / 'WePlay-Midsummer.ini', 65536) != _preset(options)):
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


def _download(url, *, cancelled=None, progress=None, timeout=120, opener=None, clock=time.monotonic):
    """Bound the entire read, including DNS/headers; the worker never mutates disk.

    A daemon read worker isolates platform DNS/header stalls from cancellation.
    It owns/closes its response and cannot publish files or invoke UI callbacks.
    On cancellation the command may return immediately; at most one bounded
    in-memory response remains until its socket timeout or process exit.
    """
    stop=threading.Event();completed=queue.Queue(maxsize=1);received=[0]
    limit=8*1024*1024;deadline=clock()+timeout
    request=urllib.request.Request(url,headers={'User-Agent':'Mozilla/5.0','Referer':'https://reshade.me/'})
    def worker():
        try:
            with (opener or urllib.request.urlopen)(request,timeout=min(5,timeout)) as response:
                raw=bytearray()
                while not stop.is_set():
                    if clock()>=deadline:raise ValueError('reshade_download_timeout')
                    # HTTPResponse.read1 performs one buffered/socket read; read
                    # may wait to fill a complete block from a trickling server.
                    block=response.read1(min(65536,limit+1-len(raw)))
                    if not block:completed.put(bytes(raw));return
                    raw.extend(block);received[0]=len(raw)
                    if len(raw)>limit:raise ValueError('reshade_download_invalid')
        except Exception as error:
            try:completed.put_nowait(error)
            except queue.Full:pass
    if cancelled and cancelled():raise ValueError('cancelled')
    thread=threading.Thread(target=worker,name='weplay-reshade-download',daemon=True);thread.start()
    last=-1.0
    try:
        while True:
            if cancelled and cancelled():raise ValueError('cancelled')
            now=clock()
            if now>=deadline:raise ValueError('reshade_download_timeout')
            if progress and now-last>=.2:progress('reshade_download',received[0],0);last=now
            try:result=completed.get(timeout=min(.1,max(.001,deadline-now)))
            except queue.Empty:continue
            if isinstance(result,Exception):
                if isinstance(result,ValueError):raise result
                raise ValueError('reshade_download_failed') from result
            return result
    finally:stop.set()


def _stage_valid(staging,receipt,files,directories,*,require_complete=False):
    """Only resume our exact, unchanged inventory. Preserve everything else."""
    try:
        if _read(staging/'preparation.json',65536)!=receipt:return False
        pending=[staging];seen=0;present=set()
        while pending:
            for path in pending.pop().iterdir():
                seen+=1
                if seen>256:return False
                path=_plain(path);name=path.relative_to(staging).as_posix()
                present.add(name)
                if path.is_dir():
                    if name not in directories:return False
                    pending.append(path)
                elif name=='preparation.json':continue
                elif name not in files or _read(path,len(files[name])+1)!=files[name]:return False
        return not require_complete or present==set(files)|directories|{'preparation.json'}
    except (OSError,ValueError):return False


def _publish_profile(folder,files,*,cancelled=None):
    import graphics_profile
    directories={'NoAddons'}
    for name in files:
        directories.update(parent.as_posix() for parent in Path(name).parents if parent!=Path('.'))
    receipt=graphics_profile.encoded(dict(format='weplay-reshade-preparation-v1',profile=folder.name,
        files={name:hashlib.sha256(data).hexdigest() for name,data in files.items()},directories=sorted(directories)))
    folder.parent.mkdir(parents=True,exist_ok=True)
    # A legacy/foreign/modified tree is never deleted or adopted. A fresh unique
    # staging tree lets the requested operation proceed while retaining evidence.
    candidates=[]
    for path in folder.parent.iterdir():
        if re.fullmatch(re.escape(folder.name)+r'\.preparing(?:-[0-9a-f]{32})?',path.name):
            candidates.append(path)
            if len(candidates)>=32:break
    staging=next((path for path in candidates if _stage_valid(path,receipt,files,directories)),None)
    if staging is None:
        staging=_plain(folder.with_name(folder.name+'.preparing-'+uuid.uuid4().hex));staging.mkdir()
        graphics_profile.atomic_create(staging/'preparation.json',receipt,staging)
    for name in sorted(directories,key=lambda value:(value.count('/'),value)):
        if cancelled and cancelled():raise ValueError('cancelled')
        _plain(staging/name).mkdir(exist_ok=True)
    for name,data in files.items():
        if cancelled and cancelled():raise ValueError('cancelled')
        target=_plain(staging/name)
        if target.exists():
            if _read(target,len(data)+1)!=data:raise ValueError('reshade_preparation_changed')
        else:graphics_profile.atomic_create(target,data,staging)
    if not _stage_valid(staging,receipt,files,directories,require_complete=True):raise ValueError('reshade_preparation_changed')
    if cancelled and cancelled():raise ValueError('cancelled')
    # No replacement of a concurrently published profile.
    if folder.exists():raise ValueError('reshade_preparation_changed')
    staging.rename(folder)


def prepare(root, *, setup=None, options=None, cancelled=None, progress=None):
    import graphics_profile
    root = _plain(Path(root))
    url, setup_sha256, dll_sha256 = _runtime(options)
    folder = _folder(root, options)
    if cancelled and cancelled():raise ValueError('cancelled')
    if folder.exists():
        # ReShade serializes this generated preset on normal exit (ordering,
        # transient uniforms, technique defaults). It is not the user's choice
        # record. Validate every immutable component before restoring the recipe.
        _verify(root, options, check_preset=False)
        preset = _plain(folder / 'WePlay-Midsummer.ini')
        previous = _read(preset, 65536) if preset.exists() else None
        expected = _preset(options)
        if previous != expected:
            graphics_profile.ensure_closed()
            # Retain the first changed preset once. Never replace an existing
            # backup or accumulate another file for every game exit.
            backup = _plain(folder / 'WePlay-Midsummer.before-repair.ini')
            if previous is not None and not backup.exists():
                graphics_profile.atomic_create(backup, previous, folder)
            graphics_profile.atomic_write(preset, expected, folder)
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
        raw=_download(url,cancelled=cancelled,progress=progress)
    elif dll is None:
        raw = _read(Path(setup),8*1024*1024)
    if dll is None:
        if len(raw) > 8 * 1024 * 1024 or hashlib.sha256(raw).hexdigest() != setup_sha256:
            raise ValueError('Der offizielle ReShade-Download stimmt nicht mit der geprueften Version ueberein.')
        with zipfile.ZipFile(io.BytesIO(raw)) as archive:
            dll, manifest = archive.read('ReShade32.dll'), archive.read('ReShade32.json')
    if (hashlib.sha256(dll).hexdigest() != dll_sha256
            or hashlib.sha256(manifest).hexdigest() != MANIFEST_SHA256):
        raise ValueError('ReShade-Laufzeitpruefung fehlgeschlagen.')
    files={'ReShade32.dll':dll,'ReShade32.json':manifest,**_profile_files(options,root),
           'WePlay-Midsummer.ini':_preset(options),'ReShade.ini':_config(options)}
    _publish_profile(folder,files,cancelled=cancelled)
    return verify(root, options)


def launch_environment(root, inherited, options=None):
    folder = prepare(root, options=options)
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
