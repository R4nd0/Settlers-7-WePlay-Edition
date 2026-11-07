"""Opt-in, per-copy ReShade tone runtime for the Modern (DXVK) renderer.

Download the official signed setup only on explicit enablement. Extract the
pinned x86 Vulkan layer into LocalData; never run setup, touch the registry,
replace d3d9.dll or activate a system-wide layer. No third-party DLL is bundled.
"""
import hashlib
import configparser
import io
import json
import os
from pathlib import Path
import urllib.request
import zipfile

URL = 'https://reshade.me/downloads/ReShade_Setup_6.8.0.exe'
SETUP_SHA256 = '207aea16205fbf952bc8fe1879966672454cf04002e7ad34237c7990a5b3c0b4'
DLL_SHA256 = '538998f66c0197adcdeadfe8e5ea19dd6ca3253e40399476d291f385ce518b88'
FOLDER = 'LocalData/ReShade-Midsummer'
CONFIG = b'''[INSTALL]
Logging=1

[GENERAL]
EffectSearchPaths=.\\Shaders
PresetPath=.\\WePlay-Midsummer.ini
PerformanceMode=1
NoDebugInfo=1
EffectCachePath=.\\Cache

[OVERLAY]
TutorialProgress=4
ShowPresetName=0
'''


def _asset(name):
    here = Path(__file__).resolve().parent
    flat = here / name
    return flat if flat.is_file() else here.parent / 'midsummer' / name


def _plain(path):
    import graphics_profile
    return graphics_profile._plain(path)


def verify(root):
    folder = _plain(Path(root) / FOLDER)
    dll = _plain(folder / 'ReShade32.dll')
    if (not dll.is_file() or dll.stat().st_size > 16 * 1024 * 1024
            or hashlib.sha256(dll.read_bytes()).hexdigest() != DLL_SHA256):
        raise ValueError('Das gepruefte ReShade-Laufzeitmodul fehlt. Grafikauswahl erneut speichern.')
    manifest = json.loads(_plain(folder / 'ReShade32.json').read_text(encoding='utf-8'))
    layer = manifest.get('layer', {})
    if layer.get('name') != 'VK_LAYER_reshade' or layer.get('library_path') != '.\\ReShade32.dll':
        raise ValueError('Das ReShade-Layerprofil ist unbekannt.')
    for name, path in (('WePlay-Midsummer.fx', folder / 'Shaders/WePlay-Midsummer.fx'),
                       ('WePlay-Midsummer.ini', folder / 'WePlay-Midsummer.ini')):
        if _plain(path).read_bytes() != _asset(name).read_bytes():
            raise ValueError('Das Mittsommer-Farbprofil wurde veraendert. Grafikauswahl erneut speichern.')
    # ReShade writes UI/style defaults on a clean exit. Compare managed values,
    # not the complete byte stream, so the next ordinary launch still works.
    config = _plain(folder / 'ReShade.ini').read_bytes()
    if len(config) > 65536:
        raise ValueError('Die lokale ReShade-Konfiguration ist zu gross.')
    actual = configparser.ConfigParser(interpolation=None)
    expected = configparser.ConfigParser(interpolation=None)
    try:
        actual.read_string(config.decode('utf-8-sig'))
        expected.read_string(CONFIG.decode('utf-8'))
        if any(actual.get(section, key, fallback=None) != value
               for section in expected.sections() for key, value in expected[section].items()):
            raise ValueError('Die verwalteten ReShade-Einstellungen wurden veraendert.')
    except (UnicodeError, configparser.Error) as error:
        raise ValueError('Die lokale ReShade-Konfiguration ist ungueltig.') from error
    return folder


def prepare(root, *, setup=None):
    import graphics_profile
    root = _plain(Path(root))
    folder = _plain(root / FOLDER)
    if folder.exists():
        return verify(root)
    graphics_profile.ensure_closed()
    if setup is None:
        request = urllib.request.Request(URL, headers={'User-Agent': 'Mozilla/5.0', 'Referer': 'https://reshade.me/'})
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                raw = response.read(8 * 1024 * 1024 + 1)
        except OSError as error:
            raise ValueError('ReShade konnte nicht von der offiziellen Website geladen werden.') from error
    else:
        raw = _plain(Path(setup)).read_bytes()
    if len(raw) > 8 * 1024 * 1024 or hashlib.sha256(raw).hexdigest() != SETUP_SHA256:
        raise ValueError('Der offizielle ReShade-Download stimmt nicht mit der geprueften Version ueberein.')
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        dll, manifest = archive.read('ReShade32.dll'), archive.read('ReShade32.json')
    if hashlib.sha256(dll).hexdigest() != DLL_SHA256:
        raise ValueError('ReShade-Laufzeitpruefung fehlgeschlagen.')
    folder.parent.mkdir(exist_ok=True)
    staging = _plain(folder.with_name(folder.name + '.preparing'))
    if staging.exists():
        raise ValueError('Eine unvollstaendige ReShade-Vorbereitung ist vorhanden.')
    staging.mkdir()
    (staging / 'ReShade32.dll').write_bytes(dll)
    (staging / 'ReShade32.json').write_bytes(manifest)
    (staging / 'Shaders').mkdir()
    (staging / 'Shaders/WePlay-Midsummer.fx').write_bytes(_asset('WePlay-Midsummer.fx').read_bytes())
    (staging / 'WePlay-Midsummer.ini').write_bytes(_asset('WePlay-Midsummer.ini').read_bytes())
    (staging / 'ReShade.ini').write_bytes(CONFIG)
    staging.rename(folder)
    return verify(root)


def launch_environment(root, inherited):
    folder = verify(root)
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
