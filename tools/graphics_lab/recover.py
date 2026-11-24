"""Recover only three private graphics-lab settings files after interruption.

There is no general rollback, save restoration, game launch or process stopping.
A journal must exist before the run's first mutation; older cases cannot be
reconstructed safely from their final receipts alone.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import uuid

from tools.shadow_detail.profile_guard import snapshot_profile

ROOT = Path(__file__).resolve().parents[2]
JOURNAL = 'recovery-journal.private.json'
FORMAT = 's7-graphics-lab-recovery-v1'
MAX_JSON = 1024 * 1024
MAX_SETTING = 1024 * 1024
DXVK = '265888c31ca78dffa290c39cb7e50bfb02762590e41927906e46fb32f01497fa'
EXES = {
    '4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f',
    '8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410',
    '76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab',
}


def plain(path):
    path = Path(path).absolute()
    for part in (path, *path.parents):
        if part.is_symlink() or (hasattr(part, 'is_junction') and part.is_junction()):
            raise ValueError('Recovery does not follow links or junctions.')
    return path.resolve()


def lab(path):
    path, work = plain(path), plain(ROOT / 'work')
    if path == work or not path.is_relative_to(work):
        raise ValueError('Recovery requires explicit paths below repository work/.')
    return path


def _read(path, limit):
    with plain(path).open('rb') as stream:
        before = os.fstat(stream.fileno())
        raw = stream.read(limit + 1)
        after = os.fstat(stream.fileno())
    now = path.stat()
    identity = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns)
    if len(raw) > limit:
        raise ValueError('Recovery input exceeds its size bound.')
    if identity(before) != identity(after) or identity(after) != identity(now) or len(raw) != now.st_size:
        raise RuntimeError('Recovery input changed during its read.')
    return raw, {'exists': True, 'sha256': hashlib.sha256(raw).hexdigest(),
                 'size': len(raw), 'mtime_ns': now.st_mtime_ns}


def _current(path):
    path = plain(path)
    return _read(path, MAX_SETTING) if path.exists() else (None, {'exists': False})


def _write_new(path, raw, mtime_ns=None):
    path = plain(path)
    with path.open('xb') as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())
    if mtime_ns is not None:
        os.utime(path, ns=(mtime_ns, mtime_ns))
        with path.open('r+b') as stream:
            os.fsync(stream.fileno())


def _atomic(path, raw, mtime_ns=None):
    path = plain(path)
    temporary = path.with_name(path.name + '.recovery-' + uuid.uuid4().hex + '.tmp')
    try:
        _write_new(temporary, raw, mtime_ns)
        temporary.replace(path)
        # Directory fsync is available on POSIX; Windows uses the flushed file
        # and same-directory atomic replacement. Hardware/network durability
        # remains subject to the underlying filesystem and storage.
        if os.name != 'nt':
            directory = os.open(path.parent, os.O_RDONLY)
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
    finally:
        if temporary.exists():
            temporary.unlink()


def _save(case, value):
    raw = (json.dumps(value, indent=2, allow_nan=False) + '\n').encode('utf-8')
    if len(raw) > MAX_JSON:
        raise ValueError('Recovery journal exceeds its 1-MiB bound.')
    _atomic(case / JOURNAL, raw)


def _no_games():
    if os.name != 'nt':
        raise RuntimeError('Actual graphics recovery requires Windows.')
    sys.path.insert(0, str(ROOT / 'tools/installer'))
    import isolated_process
    if any(p.exe.casefold() == 'settlers7r.exe' for p in isolated_process.Windows().processes()):
        raise RuntimeError('Close all S7 clients before graphics-lab recovery.')


def _critical(root, critical):
    if not isinstance(critical, dict) or len(critical) != 5:
        raise ValueError('Recovery needs the five original guarded binary identities.')
    if any(not isinstance(pin, str) or not re.fullmatch('[0-9a-f]{64}', pin) for pin in critical.values()):
        raise ValueError('Recovery binary identity must be SHA256.')
    normal = 'Base/Bin/Settlers7R.exe'
    gold = 'Data/Base/_Dbg/Bin/Release/Settlers7R.exe'
    exe = normal if normal in critical else gold
    parent = str(Path(exe).parent).replace('\\', '/')
    adapter = 'Base/Bin/uplay_r1_loader.dll' if exe == normal else 'Launcher/weplay_gold_startup.dll'
    expected = {exe, adapter, *(parent + '/' + name for name in ('d3d9.dll', 'Vision90.dll', 'vBase90.dll'))}
    if set(critical) != expected or critical[exe] not in EXES or critical[parent + '/d3d9.dll'] != DXVK:
        raise ValueError('Recovery binary layout or executable/renderer identity is unsupported.')
    for relative, pin in critical.items():
        raw, _ = _read(root / relative, 64 * 1024 * 1024)
        if hashlib.sha256(raw).hexdigest() != pin:
            raise RuntimeError('A guarded game binary changed; recovery refused.')
    return parent


def _targets(root, paths, binary_parent):
    if not isinstance(paths, dict) or set(paths) != {'options', 'runtime', 'config'}:
        raise ValueError('Recovery supports exactly options, runtime and DXVK config.')
    result = {}
    for label, path in paths.items():
        path = plain(path)
        if not path.is_relative_to(root):
            raise ValueError('A recovery target is outside the owned copy.')
        relative = path.relative_to(root).as_posix()
        allowed = {
            'options': ('LocalData/ProfileRoot/Profiles/Options.ini',
                        'LocalData/ProfileRoot/THE SETTLERS 7 - History Edition/Options.ini'),
            'runtime': ('LocalData/s7-local-runtime.ini',),
            'config': (binary_parent + '/dxvk.conf',),
        }
        if relative not in allowed[label]:
            raise ValueError('Recovery target is not an allowlisted private settings file.')
        result[label] = relative
    return result


def _shared(value):
    if not isinstance(value, dict) or not value or len(value) > 8:
        raise ValueError('Recovery needs explicit shared profile roots.')
    for label, item in value.items():
        if (not isinstance(label, str) or not re.fullmatch('[a-z][a-z0-9_-]{0,31}', label)
                or not isinstance(item, dict) or set(item) != {'root', 'snapshot'}
                or not isinstance(item['root'], str)):
            raise ValueError('Invalid shared-profile recovery record.')
        if snapshot_profile(plain(item['root'])) != item['snapshot']:
            raise RuntimeError('Shared profiles changed; preserve newer saves and refuse recovery.')


def prepare(root, case, *, critical, targets, shared):
    """Journal originals before the caller changes any private settings file."""
    root, case = lab(root), lab(case)
    if not root.is_dir() or not case.is_dir() or case.is_relative_to(root) or root.is_relative_to(case):
        raise ValueError('Use separate existing game and case directories under work/.')
    if (case / JOURNAL).exists():
        raise ValueError('A recovery journal already exists for this case.')
    _no_games()
    parent = _critical(root, critical)
    paths = _targets(root, targets, parent)
    if not isinstance(shared, dict):
        raise ValueError('Use named shared profile roots.')
    value = {'format': FORMAT, 'state': 'prepared', 'root': str(root), 'critical': dict(critical),
             'targets': {}, 'shared': {label: {'root': str(plain(path)), 'snapshot': snapshot_profile(path)}
                                       for label, path in shared.items()}, 'attempts': 0}
    _shared(value['shared'])
    originals = plain(case / 'recovery-original')
    originals.mkdir()
    for label, relative in paths.items():
        raw, record = _current(root / relative)
        value['targets'][label] = {'relative': relative, 'original': record}
        if raw is not None:
            _write_new(originals / (label + '.bin'), raw, record['mtime_ns'])
            if _current(root / relative)[1] != record:
                raise RuntimeError('Private settings changed before journal completion.')
    _save(case, value)
    return case / JOURNAL


def recover(case):
    """Preserve diagnostics, restore only journaled settings, never touch saves."""
    case = lab(case)
    journal = plain(case / JOURNAL)
    if not journal.is_file():
        raise ValueError('No interruption journal: this older case is not automatically recoverable.')
    raw, _ = _read(journal, MAX_JSON)
    value = json.loads(raw)
    if (not isinstance(value, dict) or set(value) != {'format', 'state', 'root', 'critical', 'targets', 'shared', 'attempts'}
            or value['format'] != FORMAT or value['state'] not in ('prepared', 'restoring', 'restored')
            or type(value['attempts']) is not int or not 0 <= value['attempts'] <= 16
            or not isinstance(value['targets'], dict) or set(value['targets']) != {'options', 'runtime', 'config'}):
        raise ValueError('Invalid or exhausted recovery journal.')
    root = lab(value['root'])
    if not root.is_dir() or case.is_relative_to(root) or root.is_relative_to(case):
        raise ValueError('Invalid journal game/case separation.')
    _no_games()
    parent = _critical(root, value['critical'])
    if any(not isinstance(item, dict) or set(item) != {'relative', 'original'}
           or not isinstance(item['relative'], str) for item in value['targets'].values()):
        raise ValueError('Invalid private-settings recovery record.')
    paths = _targets(root, {label: root / item['relative'] for label, item in value['targets'].items()}, parent)
    _shared(value['shared'])
    originals = {}
    for label, item in value['targets'].items():
        record = item['original']
        if not isinstance(record, dict) or type(record.get('exists')) is not bool:
            raise ValueError('Invalid original-settings record.')
        if record['exists']:
            raw, actual = _read(case / 'recovery-original' / (label + '.bin'), MAX_SETTING)
            if (set(record) != {'exists', 'sha256', 'size', 'mtime_ns'}
                    or type(record['mtime_ns']) is not int or record['mtime_ns'] < 0
                    or type(record['size']) is not int or not 0 <= record['size'] <= MAX_SETTING
                    or actual['sha256'] != record['sha256'] or actual['size'] != record['size']):
                raise RuntimeError('An original-settings backup changed; recovery refused.')
            originals[label] = raw
        elif record != {'exists': False}:
            raise ValueError('Invalid absent-settings record.')
    if value['state'] == 'restored':
        if any(_current(root / paths[label])[1] != item['original'] for label, item in value['targets'].items()):
            raise RuntimeError('A completed recovery has newer private settings; no second rollback is allowed.')
        return {'recovered': True, 'already_restored': True, 'saves_restored': False}
    attempt = value['attempts'] + 1
    # A power cut before the journal update can leave a partially preserved
    # directory behind. Retain it and choose a fresh bounded attempt.
    while attempt <= 16 and (case / 'recovery-preserved' / f'attempt-{attempt:02d}').exists():
        attempt += 1
    if attempt > 16:
        raise ValueError('Recovery attempts exhausted; retained diagnostics need assessment.')
    preserved = plain(case / 'recovery-preserved' / f'attempt-{attempt:02d}')
    preserved.mkdir(parents=True)
    current = {}
    for label, relative in paths.items():
        raw, record = _current(root / relative)
        current[label] = record
        if raw is not None:
            _write_new(preserved / (label + '.bin'), raw, record['mtime_ns'])
    _atomic(preserved / 'files.private.json', (json.dumps(current, indent=2) + '\n').encode('utf-8'))
    value.update(state='restoring', attempts=attempt)
    _save(case, value)
    for label, relative in paths.items():
        _no_games()
        path, record = root / relative, value['targets'][label]['original']
        if _current(path)[1] != current[label]:
            raise RuntimeError('Private diagnostic settings changed after preservation; no overwrite performed.')
        if record['exists']:
            path.parent.mkdir(parents=True, exist_ok=True)
            _atomic(path, originals[label], record['mtime_ns'])
        elif path.exists():
            path.unlink()
        if _current(path)[1] != record:
            raise RuntimeError('Restored private settings did not verify.')
    _shared(value['shared'])
    _critical(root, value['critical'])
    value['state'] = 'restored'
    _save(case, value)
    return {'recovered': True, 'already_restored': False, 'saves_restored': False,
            'shared_profiles_unchanged': True, 'game_binaries_unchanged': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('case', type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(recover(args.case), indent=2))
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(2, f'Graphics recovery refused: {error}\n')


if __name__ == '__main__':
    main()
