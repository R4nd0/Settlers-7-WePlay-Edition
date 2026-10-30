"""Opt-in DXVK D3D9 experiment for a manifest-verified, closed S7 test copy.

No downloads or game launches. Each replacement is atomic; a persistent journal
recovers an interrupted multi-file operation to its previous committed state.
This changes the content ID and establishes no performance or multiplayer result.
The first installation requires absent d3d9.dll/dxvk.conf; only our recorded
profile is idempotent. Local manifests are integrity records, not signatures.
"""
import argparse
import copy
import json
import os
from pathlib import Path
import re
import stat
import struct
import sys
import tempfile
import unicodedata

sys.path[:0] = [str(Path(__file__).resolve().parent),
                str(Path(__file__).resolve().parent.parent / 'lan')]
import compatibility as content
from update_join_menu import atomic_write, encoded, ensure_closed, session_lock

PROFILE = 'dxvk-3.1.1-d3d9-60-v1'
STATE = 'LocalData/GraphicsProfile-' + PROFILE
EXE = 'Base/Bin/Settlers7R.exe'
LOADER = 'Base/Bin/uplay_r1_loader.dll'
MENU = 'Base/BBA/Interface.bba'
DLL = 'Base/Bin/d3d9.dll'
CONF = 'Base/Bin/dxvk.conf'
COMPAT = 'compatibility.json'
BUNDLE = 'local-bundle.json'
FILES = (DLL, CONF, COMPAT)
EXE_SHA256 = '4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f'
STEAM_EXE_SHA256 = '8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410'
DXVK_SHA256 = '265888c31ca78dffa290c39cb7e50bfb02762590e41927906e46fb32f01497fa'
CONFIG = b'[Settlers7R.exe]\nd3d9.maxFrameRate = 60\n'
# PC1 Tanholm A/B/A: Auto renders black water; Strict restores the water path.
# Keep this fixed and child-only; the owned on-disk profile remains reversible.
SHADER_COMPATIBILITY = 'd3d9.floatEmulation = Strict'
MAX_DLL = 32 * 1024 * 1024
MAX_JSON = 4 * 1024 * 1024


def _plain(path):
    path = Path(path).absolute()
    # Also reject reparse-point kinds other than symlinks/junctions, including
    # when the Python version has no Path.is_junction().
    for part in (path, *path.parents):
        try:
            info = part.lstat()
        except FileNotFoundError:
            continue
        if getattr(info, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError('Verknuepfungen sind im Grafikprofil nicht erlaubt.')
    return content.plain(path)


def _read(path, limit):
    path = _plain(path)
    if not path.is_file() or path.stat().st_size > limit:
        raise ValueError('Eine Grafikprofil-Datei fehlt oder ist zu gross.')
    with path.open('rb') as stream:
        value = stream.read(limit + 1)
    if len(value) > limit:
        raise ValueError('Eine Grafikprofil-Datei wurde waehrend der Pruefung veraendert.')
    return value


def _hash(value):
    import hashlib
    return hashlib.sha256(value).hexdigest()


def _json(value):
    def unique(pairs):
        result = {}
        for key, item in pairs:
            if key in result:
                raise ValueError('Doppeltes Feld im Grafikprofil-Manifest.')
            result[key] = item
        return result
    result = json.loads(value, object_pairs_hook=unique)
    if not isinstance(result, dict):
        raise ValueError('Ungueltiges Grafikprofil-Manifest.')
    return result


def _pe32_dll(value):
    if len(value) < 64 or value[:2] != b'MZ':
        raise ValueError('Eine gueltige 32-Bit-DLL wird benoetigt.')
    offset = struct.unpack_from('<I', value, 0x3c)[0]
    if offset < 64 or offset + 26 > len(value) or value[offset:offset + 4] != b'PE\0\0':
        raise ValueError('Ungueltiger DLL-Dateikopf.')
    machine, = struct.unpack_from('<H', value, offset + 4)
    optional_size, flags = struct.unpack_from('<HH', value, offset + 20)
    magic, = struct.unpack_from('<H', value, offset + 24)
    if (machine != 0x14c or magic != 0x10b or not flags & 0x2000
            or optional_size < 96 or offset + 24 + optional_size > len(value)):
        raise ValueError('Das Profil braucht eine PE32-x86-DLL.')


def _bundle(root, expected):
    value = _json(_read(root / BUNDLE, MAX_JSON))
    hashes = value.get('files')
    if (value.get('format') != 's7-local-experimental-v1'
            or not isinstance(hashes, dict) or set(hashes) != {EXE, LOADER, MENU}
            or hashes.get(EXE) not in (EXE_SHA256, STEAM_EXE_SHA256)):
        raise ValueError('Dieses Grafikprofil braucht eine vorbereitete lokale S7-Testkopie.')
    for name, digest in hashes.items():
        if (not isinstance(digest, str) or not re.fullmatch('[0-9a-f]{64}', digest)
                or content.digest(_plain(root / name)) != digest
                or expected['files'].get(name.lower()) != digest):
            raise ValueError('Spielprogramm, lokaler Loader und Manifeste stimmen nicht ueberein.')
    _pe32_dll(_read(root / LOADER, MAX_DLL))


def _preflight(root):
    root = _plain(root)
    if not root.is_dir() or _plain(root / 'INSTALLATION-INCOMPLETE.txt').exists():
        raise ValueError('Die lokale Testkopie ist unvollstaendig.')
    # These must exist before session_lock is allowed to create LocalData.
    for name in (BUNDLE, COMPAT):
        _json(_read(root / name, MAX_JSON))
    _plain(root / 'LocalData')
    _plain(root / 'LocalData/session.lock')
    return root


def _current(root):
    result = {}
    for name in FILES:
        path = _plain(root / name)
        if path.exists():
            if not path.is_file():
                raise ValueError('Ein Grafikprofil-Ziel ist keine normale Datei.')
            result[name] = content.digest(path)
        else:
            result[name] = None
    return result


def _expected(before):
    value = copy.deepcopy(content.validate(_json(before)))
    if any(name.lower() in value['files'] for name in (DLL, CONF)) or 'graphics_profile' in value:
        raise ValueError('Die Sicherung enthaelt bereits ein fremdes Grafikprofil.')
    value['files'][DLL.lower()] = DXVK_SHA256
    value['files'][CONF.lower()] = _hash(CONFIG)
    value['content_id'] = content.fingerprint(value['files'])
    value['graphics_profile'] = PROFILE
    value['graphics_runtime_verified'] = False
    value['simulation_sync_verified'] = False
    value['lan_join_verified'] = False
    return encoded(value)


def _load_state(root, *, full_inventory=True):
    folder = _plain(root / STATE)
    if not folder.is_dir():
        raise ValueError('Die Grafikprofil-Sicherung fehlt.')
    state = _json(_read(folder / 'state.json', 4096))
    if set(state) != {'format', 'bundle_sha256', 'compatibility_sha256'} or state['format'] != PROFILE:
        raise ValueError('Unbekannte Grafikprofil-Sicherung.')
    bundle = _read(folder / 'bundle.before.json', MAX_JSON)
    before = _read(folder / 'compatibility.before.json', MAX_JSON)
    if (_hash(bundle) != state['bundle_sha256'] or _hash(before) != state['compatibility_sha256']
            or _read(root / BUNDLE, MAX_JSON) != bundle):
        raise ValueError('Startmanifest oder Grafikprofil-Sicherung wurde veraendert.')
    baseline = content.validate(_json(before))
    _bundle(root, baseline)
    after = _expected(before)
    dll = _read(folder / 'd3d9.payload', MAX_DLL)
    if _hash(dll) != DXVK_SHA256:
        raise ValueError('Die gesicherte DXVK-DLL wurde veraendert.')
    _pe32_dll(dll)
    status = _json(_read(folder / 'status.json', 4096))
    if set(status) != {'active'} or type(status['active']) is not bool:
        raise ValueError('Ungueltiger Grafikprofil-Zustand.')
    # Validate every original inventory entry. A journal is never permission
    # to regenerate the inventory and bless unrelated game/adapter changes.
    if full_inventory:
        actual = content.inventory(root)
        for name in (DLL, CONF):
            actual.pop(name.lower(), None)
        if actual != baseline['files']:
            raise ValueError('Spieldateien ausserhalb des Grafikprofils wurden veraendert.')
    for name in ('logs', 'cache'):
        if not _plain(folder / name).is_dir():
            raise ValueError('Ein lokaler Grafikprofil-Ordner fehlt.')
    return folder, {False: {DLL: None, CONF: None, COMPAT: before},
                    True: {DLL: dll, CONF: CONFIG, COMPAT: after}}, status['active']


def _hashes(values):
    return {name: None if value is None else _hash(value) for name, value in values.items()}


def _assert_state(root, values):
    if _current(root) != _hashes(values):
        raise ValueError('Grafikprofil-Dateien wurden ausserhalb des Werkzeugs veraendert.')


def atomic_create(path, value, staging):
    """Publish a complete new file without replacing a concurrently created file."""
    with tempfile.NamedTemporaryFile(dir=staging, prefix='graphics-', delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(value); stream.flush(); os.fsync(stream.fileno())
    try:
        # On Windows os.rename is same-volume, atomic and fails if the target
        # already exists; os.replace would overwrite it. This module already
        # requires Windows through the shared msvcrt launcher lock.
        if os.name != 'nt':
            raise RuntimeError('Das Grafikprofil benoetigt Windows-Dateisemantik.')
        os.rename(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _write_values(root, folder, values, permitted):
    for name, value in values.items():
        # Check immediately before each mutation; unknown files survive even
        # when inserted between preparation and rollback.
        current = _current(root)[name]
        if current not in permitted[name]:
            raise ValueError('Eine unbekannte Datei verhindert die sichere Wiederherstellung.')
        if current == (None if value is None else _hash(value)):
            continue
        target = _plain(root / name)
        if value is None:
            target.unlink()
        elif current is None:
            atomic_create(target, value, folder)
        else:
            atomic_write(target, value, folder)


def _recover(root, folder, versions, active):
    journal_path = _plain(folder / 'transaction.json')
    if not journal_path.exists():
        _assert_state(root, versions[active])
        return active
    journal = _json(_read(journal_path, 4096))
    if (set(journal) != {'format', 'from', 'to'} or journal['format'] != PROFILE
            or type(journal['from']) is not bool or type(journal['to']) is not bool
            or journal['from'] == journal['to']):
        raise ValueError('Ungueltiges Grafikprofil-Transaktionsjournal.')
    permitted = {n: {_hashes(versions[False])[n], _hashes(versions[True])[n]} for n in FILES}
    if any(value not in permitted[n] for n, value in _current(root).items()):
        raise ValueError('Unbekannte Grafikdateien bleiben erhalten; Wiederherstellung abgebrochen.')
    ensure_closed()
    if active == journal['to']:
        # Status is the commit point. Never interpret a mixed state as committed.
        _assert_state(root, versions[active])
    else:
        _write_values(root, folder, versions[active], permitted)
        _assert_state(root, versions[active])
    content.verify(root)
    journal_path.unlink()
    return active


def _prepare(root, source):
    expected = content.verify(root)
    _bundle(root, expected)
    if any(_plain(root / name).exists() for name in (DLL, CONF)):
        raise ValueError('Vorhandene d3d9.dll oder dxvk.conf wird nicht uebernommen oder ersetzt.')
    dll = _read(source, MAX_DLL)
    if _hash(dll) != DXVK_SHA256:
        raise ValueError('Die Quelle ist nicht die bekannte DXVK-3.1.1-x32-DLL.')
    _pe32_dll(dll)
    bundle = _read(root / BUNDLE, MAX_JSON)
    before = _read(root / COMPAT, MAX_JSON)
    _expected(before)
    folder = _plain(root / STATE)
    # Publish a complete backup directory with one rename; an interruption
    # during preparation leaves only a harmless private LocalData scratch dir.
    with tempfile.TemporaryDirectory(prefix='graphics-prepare-', dir=root / 'LocalData') as temporary:
        staging = _plain(Path(temporary) / 'profile')
        staging.mkdir()
        for name, value in {'bundle.before.json': bundle, 'compatibility.before.json': before,
                            'd3d9.payload': dll, 'status.json': encoded({'active': False}),
                            'state.json': encoded({'format': PROFILE, 'bundle_sha256': _hash(bundle),
                                                  'compatibility_sha256': _hash(before)})}.items():
            with (staging / name).open('xb') as stream:
                stream.write(value); stream.flush(); os.fsync(stream.fileno())
        for name in ('logs', 'cache'):
            (staging / name).mkdir()
        if folder.exists():
            raise ValueError('Der Grafikprofil-Sicherungsordner ist bereits belegt.')
        staging.rename(folder)


def _change(root, enable, source=None):
    root = _preflight(root)
    ensure_closed()
    with session_lock(root):
        folder = _plain(root / STATE)
        prepared = False
        if not folder.exists():
            if not enable:
                expected = content.verify(root)
                _bundle(root, expected)
                if any(_plain(root / name).exists() for name in (DLL, CONF)):
                    raise ValueError('Unbekannte Grafikdateien werden nicht entfernt.')
                return 'already-disabled'
            _prepare(root, _plain(source))
            prepared = True
        folder, versions, active = _load_state(root, full_inventory=not prepared)
        active = _recover(root, folder, versions, active)
        if enable and source is not None:
            supplied = _read(source, MAX_DLL)
            if _hash(supplied) != DXVK_SHA256:
                raise ValueError('Die angegebene DXVK-Quelle wurde veraendert.')
            _pe32_dll(supplied)
        if active == enable:
            return 'already-enabled' if enable else 'already-disabled'
        ensure_closed()
        _assert_state(root, versions[active])
        journal = {'format': PROFILE, 'from': active, 'to': enable}
        atomic_write(folder / 'transaction.json', encoded(journal), folder)
        permitted = {n: {_hashes(versions[False])[n], _hashes(versions[True])[n]} for n in FILES}
        try:
            _write_values(root, folder, versions[enable], permitted)
            _assert_state(root, versions[enable])
            content.verify(root)
            atomic_write(folder / 'status.json', encoded({'active': enable}), folder)
            (folder / 'transaction.json').unlink()
        except BaseException:
            # Includes Ctrl-C. A process kill/power loss is handled on next run.
            folder, versions, committed = _load_state(root)
            _recover(root, folder, versions, committed)
            raise
        return 'enabled' if enable else 'disabled'


def install(root, dll_source):
    """Enable from an explicit, hash-locked local DLL. Never downloads."""
    return _change(root, True, dll_source)


def restore(root):
    """Restore the exact original compatibility manifest and remove owned files."""
    return _change(root, False)


def launch_environment(root, inherited=None, *, device_name=None, anisotropic_filtering=False):
    """Return a child environment for a verified active profile; perform no writes.

    The caller must hold its launcher session lock until its game exits. Logs
    and caches stay in LocalData, outside Base/Server/Launcher fingerprints.
    GPU selection is per process/PC, not part of cross-PC content identity.
    A fixed shader compatibility setting avoids black water on the tested GPU.
    It is not a free-form environment or user-supplied configuration override.
    """
    if type(anisotropic_filtering) is not bool:
        raise ValueError('Ungueltige lokale Texturfilterung.')
    root = _preflight(root)
    # Startup checks the owned profile and the three established launcher
    # files, not all multi-gigabyte archives. Install/restore use full inventory.
    folder, versions, active = _load_state(root, full_inventory=False)
    if not active or _plain(folder / 'transaction.json').exists():
        raise ValueError('Das Grafikprofil ist nicht vollstaendig aktiviert.')
    _assert_state(root, versions[True])
    if device_name is not None:
        if (not isinstance(device_name, str) or not device_name.strip()
                or any(unicodedata.category(c).startswith('C') for c in device_name)
                or len(device_name.encode('utf-8')) > 256):
            raise ValueError('Ungueltige lokale Grafikkartenauswahl.')
    result = dict(os.environ if inherited is None else inherited)
    owned = {'DXVK_CONFIG_FILE', 'DXVK_LOG_PATH', 'DXVK_SHADER_CACHE_PATH',
             'DXVK_STATE_CACHE_PATH', 'DXVK_FILTER_DEVICE_NAME', 'DXVK_CONFIG'}
    # Windows environment names are case-insensitive; remove inherited aliases.
    for name in list(result):
        if name.upper() in owned:
            del result[name]
    result.update(DXVK_CONFIG_FILE=str(_plain(root / CONF)),
                  DXVK_CONFIG=SHADER_COMPATIBILITY + (
                      '; d3d9.samplerAnisotropy = 16' if anisotropic_filtering else ''),
                  DXVK_LOG_PATH=str(_plain(folder / 'logs')),
                  DXVK_SHADER_CACHE_PATH=str(_plain(folder / 'cache')))
    if device_name is not None:
        result['DXVK_FILTER_DEVICE_NAME'] = device_name
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('install', 'restore'))
    parser.add_argument('--bundle', required=True)
    parser.add_argument('--dll-source')
    args = parser.parse_args()
    if args.action == 'install' and not args.dll_source:
        parser.error('install benoetigt --dll-source mit einer vorhandenen lokalen DLL.')
    try:
        result = install(Path(args.bundle), Path(args.dll_source)) if args.action == 'install' else restore(Path(args.bundle))
        print('Grafikprofil: ' + result + '. Kein Nachweis einer behobenen Leistungsstoerung.')
        return 0
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print('GRAFIKPROFIL ABGEBROCHEN: ' + str(exc))
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
