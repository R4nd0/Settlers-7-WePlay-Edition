"""Read-only archive-entry comparison for explicit crossplay audit scopes.

This is an audit, not a complete dependency manifest or permission to start a
world. Inputs are fingerprinted before parsing; archive bytes are mapped read-only.
Only names, sizes and digests are returned. Game files are never opened for writing.
"""
import argparse
import hashlib
import json
import mmap
import os
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'lan'))
from bba import Archive

FORMAT = 's7-crossplay-content-audit-v1'
MAX_ENTRY = 128 * 1024 * 1024


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'),
                                     ensure_ascii=True).encode('ascii')).hexdigest()


def path_name(value, *, prefix=False):
    if not isinstance(value, str) or not value or len(value) > 512 or any(not 32 <= ord(c) <= 126 for c in value):
        raise ValueError('Invalid archive entry path')
    parts = value.split('/')
    if prefix and parts[-1] == '':
        parts.pop()
    if not parts or any(part in ('', '.', '..') for part in parts) or any(c in value for c in '\\:\0'):
        raise ValueError('Invalid archive entry path')
    return value


def inventory(path, expected_sha256, prefix=None):
    if not isinstance(expected_sha256, str) or not re.fullmatch('[0-9a-f]{64}', expected_sha256):
        raise ValueError('Expected an exact archive SHA256')
    if prefix is not None:
        path_name(prefix, prefix=True)
        if not prefix.endswith('/'):
            raise ValueError('Archive prefix must end at a directory boundary')
    with Path(path).open('rb') as file:
        before = os.fstat(file.fileno())
        if not 32 <= before.st_size <= 8 * 1024**3:
            raise ValueError('Archive size outside audit bounds')
        actual = hashlib.sha256()
        for chunk in iter(lambda: file.read(1024 * 1024), b''):
            actual.update(chunk)
        if actual.hexdigest() != expected_sha256:
            raise ValueError('Archive fingerprint mismatch')
        with mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as mapped:
            archive = Archive(mapped, copy=False)
            files = {}
            seen = set()
            for name, entry in archive.entries.items():
                # Production v6 archives include a root-directory metadata row.
                # A file named '.' remains invalid and is never excluded here.
                if name == '.' and entry.values[4] == 256:
                    continue
                path_name(name, prefix=entry.values[4] == 256)
                key = name.casefold()
                if key in seen:
                    raise ValueError('Case-ambiguous archive entries')
                seen.add(key)
                if entry.values[4] == 256 or (prefix is not None and not name.startswith(prefix)):
                    continue
                if entry.values[2] > MAX_ENTRY:
                    raise ValueError('Entry exceeds audit size bound')
                data = archive.read(name)
                files[name] = {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                del data
            if not files:
                raise ValueError('Selected archive scope is empty')
            after = os.fstat(file.fileno())
            fields = ('st_dev', 'st_ino', 'st_size', 'st_mtime_ns', 'st_ctime_ns')
            if any(getattr(before, key) != getattr(after, key) for key in fields):
                raise ValueError('Archive changed during audit')
    return {'archive_sha256': expected_sha256, 'prefix': prefix, 'files': files,
            'entry_digest': digest({'format': FORMAT, 'files': files})}


def validate_shape(profiles):
    if not isinstance(profiles, dict) or not 2 <= len(profiles) <= 8:
        raise ValueError('Audit needs two to eight profiles')
    scopes = None
    for label, groups in profiles.items():
        if not re.fullmatch('[a-z][a-z0-9_-]{0,31}', label) or not isinstance(groups, dict) or not 1 <= len(groups) <= 8:
            raise ValueError('Invalid audit profile')
        if scopes is None:
            scopes = set(groups)
        if set(groups) != scopes:
            raise ValueError('Audit profiles must select the same scopes')
        for name in groups:
            if not re.fullmatch('[a-z][a-z0-9_-]{0,31}', name):
                raise ValueError('Invalid scope name')
    return scopes


def compare(profiles):
    scopes = validate_shape(profiles)
    result = {}
    for scope in sorted(scopes):
        values = [groups[scope] for groups in profiles.values()]
        if any(value['prefix'] != values[0]['prefix'] for value in values):
            raise ValueError('Cannot compare different archive prefixes')
        names = sorted(set.union(*(set(value['files']) for value in values)))
        different = [name for name in names if any(value['files'].get(name) != values[0]['files'].get(name)
                                                  for value in values[1:])]
        result[scope] = {'union_entry_count': len(names), 'different_entries': different,
                         'entries_equal': not different}
    return result


def run(spec):
    if not isinstance(spec, dict) or set(spec) != {'profiles'}:
        raise ValueError('Expected explicit profiles only')
    # Validate profile/scope shape before opening any input.
    validate_shape(spec['profiles'])
    profiles = {}
    for label, groups in spec['profiles'].items():
        profiles[label] = {}
        for group, value in groups.items():
            if not isinstance(value, dict) or set(value) - {'path', 'sha256', 'prefix'} or not {'path', 'sha256'} <= set(value):
                raise ValueError('Expected archive path, SHA256 and optional prefix')
            profiles[label][group] = inventory(value['path'], value['sha256'], value.get('prefix'))
    return {'format': FORMAT, 'profiles': profiles, 'comparison': compare(profiles),
            'complete_dependency_closure': False, 'world_permission': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('spec', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if not output.is_relative_to((root / 'work').resolve()) or output.exists() or not output.parent.is_dir():
        parser.error('Use a fresh output file under an existing repository work directory')
    if args.spec.stat().st_size > 65536:
        parser.error('Audit specification is too large')
    result = run(json.loads(args.spec.read_text()))
    with output.open('x') as file:
        json.dump(result, file, indent=2)
    print(json.dumps({'comparison': result['comparison'], 'complete_dependency_closure': False,
                      'world_permission': False}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
