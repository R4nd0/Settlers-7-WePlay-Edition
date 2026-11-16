"""Strict shared catalog; only independently approved catalogs admit a launch."""
from dataclasses import dataclass, field
import hashlib
import json
import re

import compatibility
import crossplay_session

FORMAT = 'weplay-crossplay-content-v1'
MAX_BYTES = 16 * 1024 * 1024
MAX_FILES = 30000
APPROVED_DIGESTS = frozenset()
REQUIRED = {
    1: ('Data/Base/_Dbg/Bin/Release/Settlers7R.exe', 'Launcher/weplay_gold_startup.dll'),
    2: ('Base/Bin/Settlers7R.exe', 'Base/Bin/uplay_r1_loader.dll'),
    3: ('Base/Bin/Settlers7R.exe', 'Base/Bin/uplay_r1_loader.dll'),
}
PRIVATE_COMPONENTS = frozenset(('localdata', 'profiles', 'savegames', 'saves', '.agent', '.git'))


def file_id(value):
    if (not isinstance(value, str) or not 1 <= len(value) <= 240 or value.startswith('/')
            or any(not 32 <= ord(c) <= 126 or c in '\\:*?"<>|' for c in value)):
        raise ValueError('crossplay_catalog_invalid')
    for part in value.split('/'):
        if (not part or part in ('.', '..') or part.endswith(('.', ' '))
                or part.casefold() in PRIVATE_COMPONENTS
                or re.fullmatch(r'(?i:con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\..*)?', part)):
            raise ValueError('crossplay_catalog_invalid')
    return value


def unique(items):
    result = {}
    for key, value in items:
        if key in result: raise ValueError('crossplay_catalog_invalid')
        result[key] = value
    return result


@dataclass(frozen=True)
class Catalog:
    canonical: bytes = field(repr=False)
    digest: bytes = field(init=False)
    profiles: tuple = field(init=False, repr=False)

    def __post_init__(self):
        if type(self.canonical) is not bytes or not 1 <= len(self.canonical) <= MAX_BYTES:
            raise ValueError('crossplay_catalog_invalid')
        try:
            data = json.loads(self.canonical.decode('utf-8'), object_pairs_hook=unique)
            if (not isinstance(data, dict) or set(data) != {'format', 'native_protocol', 'neo_features', 'editions'}
                    or data['format'] != FORMAT or type(data['native_protocol']) is not int
                    or data['native_protocol'] != 1 or type(data['neo_features']) is not int
                    or data['neo_features'] != 19 or not isinstance(data['editions'], list)
                    or len(data['editions']) != 3):
                raise ValueError('crossplay_catalog_invalid')
            profiles = []
            for edition, profile in enumerate(data['editions'], 1):
                if (not isinstance(profile, dict) or set(profile) != {'edition', 'files'}
                        or type(profile['edition']) is not int or profile['edition'] != edition
                        or not isinstance(profile['files'], list) or not 1 <= len(profile['files']) <= MAX_FILES):
                    raise ValueError('crossplay_catalog_invalid')
                records, seen, previous = [], set(), ''
                for row in profile['files']:
                    if not isinstance(row, dict) or set(row) != {'id', 'sha256'}:
                        raise ValueError('crossplay_catalog_invalid')
                    name = file_id(row['id']); digest = row['sha256']
                    if (name <= previous or name.casefold() in seen or not isinstance(digest, str)
                            or not re.fullmatch('[0-9a-f]{64}', digest) or digest == '0' * 64):
                        raise ValueError('crossplay_catalog_invalid')
                    records.append((name, digest)); seen.add(name.casefold()); previous = name
                files = dict(records)
                executable, adapter = REQUIRED[edition]
                if (not all(name in files for name in (executable, adapter))
                        or crossplay_session.EXECUTABLE_EDITIONS.get(files[executable]) != edition):
                    raise ValueError('crossplay_catalog_invalid')
                profiles.append(tuple(records))
            encoded = json.dumps(data, ensure_ascii=True, sort_keys=True, separators=(',', ':')).encode('utf-8')
            if encoded != self.canonical: raise ValueError('crossplay_catalog_invalid')
        except (ValueError, UnicodeError, TypeError, KeyError, RecursionError) as error:
            raise ValueError('crossplay_catalog_invalid') from error
        object.__setattr__(self, 'digest', hashlib.sha256(encoded).digest())
        object.__setattr__(self, 'profiles', tuple(profiles))

    def verify(self, root, edition):
        if self.digest.hex() not in APPROVED_DIGESTS:
            raise ValueError('crossplay_catalog_unapproved')
        if type(edition) is not int or edition not in (1, 2, 3):
            raise ValueError('crossplay_edition_mismatch')
        root = compatibility.plain(root)
        files = self.profiles[edition - 1]
        adapter = dict(files)[REQUIRED[edition][1]]
        if adapter not in crossplay_session.ADAPTER_PINS.get(edition, ()):
            raise ValueError('crossplay_adapter_unavailable')
        for name, digest in files:
            path = compatibility.plain(root / name)
            if not path.is_file() or compatibility.digest(path) != digest:
                raise ValueError('crossplay_content_changed')
        return self.digest


def read(path):
    path = compatibility.plain(path)
    if not path.is_file() or not 1 <= path.stat().st_size <= MAX_BYTES:
        raise ValueError('crossplay_catalog_invalid')
    with path.open('rb') as stream:
        raw = stream.read(MAX_BYTES + 1)
    return Catalog(raw)
