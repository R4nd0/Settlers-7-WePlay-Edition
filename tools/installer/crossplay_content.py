"""Strict shared catalog; only independently approved catalogs admit a launch."""
from dataclasses import dataclass, field
import hashlib
import json
from pathlib import Path
import re
import stat

import compatibility
import crossplay_session

FORMAT = 'weplay-crossplay-content-v1'
MAX_BYTES = 16 * 1024 * 1024
MAX_FILES = 30000
# Exact official-input inventory with the independently built room adapters
# and reproducible mixed menus. The RC4 signer separately requires installed
# gameplay receipts for this exact package and every host edition.
APPROVED_DIGESTS = frozenset({
    '4ef33244ee5a6ed86c37f26b4629a3477c6aeeeb511ca81e59d897b52cdfb6b8',
    # Same input inventory with the verified History board recovery patch.
    '2e3e58624335713d7011914c89c167fdacae492784aa4ca16e18049d83e4e85a',
    # History board recovery using the synchronous coroutine guard.
    'e92606587a4be9d711ffa882cc460af010c7afd9c5bcf7cb08c6cf18469076ee',
})
PACKAGE_CATALOG = 'Payload/Crossplay/catalog.json'
REQUIRED = {
    1: ('Data/Base/_Dbg/Bin/Release/Settlers7R.exe', 'Launcher/weplay_gold_startup.dll'),
    2: ('Base/Bin/Settlers7R.exe', 'Base/Bin/uplay_r1_loader.dll'),
    3: ('Base/Bin/Settlers7R.exe', 'Base/Bin/uplay_r1_loader.dll'),
}
GAME_SCOPE = {1:'Data',2:'Base',3:'Base'}
PRIVATE_COMPONENTS = frozenset(('localdata', 'profile', 'profiles', 'savegames', 'saves',
                               'account', 'accounts', 'backup', 'backups', 'cache', 'caches', '.agent', '.git'))


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


def plain_asset(path):
    """Reject every reparse component before resolving or traversing it."""
    path=Path(path).absolute()
    for part in (path,*path.parents):
        info=part.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0)&0x400:
            raise ValueError('crossplay_content_changed')
    return compatibility.plain(path)


def asset_names(root,edition):
    """Enumerate the full game scope without reading any file contents.

    There are no suffix exclusions. Writable logs/caches belong outside assets;
    unknown private directories are rejected before they are traversed.
    """
    start=plain_asset(root/GAME_SCOPE[edition])
    if not start.is_dir():raise ValueError('crossplay_content_changed')
    pending=[start];seen=set();files=set()
    while pending:
        folder=plain_asset(pending.pop())
        for child in folder.iterdir():
            path=plain_asset(child)
            name=file_id(path.relative_to(root).as_posix()).casefold()
            if name in seen or len(seen)>=MAX_FILES*2:raise ValueError('crossplay_content_changed')
            seen.add(name)
            if path.is_dir():pending.append(path)
            elif path.is_file():files.add(name)
            else:raise ValueError('crossplay_content_changed')
            if len(files)>MAX_FILES:raise ValueError('crossplay_content_changed')
    return files


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
                    in_scope=name.casefold().startswith(GAME_SCOPE[edition].casefold()+'/')
                    if ((not in_scope and name!=REQUIRED[edition][1])
                            or name <= previous or name.casefold() in seen or not isinstance(digest, str)
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

    def admitted_files(self, edition):
        if self.digest.hex() not in APPROVED_DIGESTS:
            raise ValueError('crossplay_catalog_unapproved')
        if type(edition) is not int or edition not in (1, 2, 3):
            raise ValueError('crossplay_edition_mismatch')
        files = self.profiles[edition - 1]
        adapter = dict(files)[REQUIRED[edition][1]]
        if adapter not in crossplay_session.ADAPTER_PINS.get(edition, ()):
            raise ValueError('crossplay_adapter_unavailable')
        return files

    def verify(self, root, edition):
        files = self.admitted_files(edition)
        try:
            root=plain_asset(root)
            expected={name.casefold() for name,_ in files
                      if name.casefold().startswith(GAME_SCOPE[edition].casefold()+'/')}
            if asset_names(root,edition)!=expected:raise ValueError('crossplay_content_changed')
            for name,digest in files:
                path=plain_asset(root/name);before=path.stat()
                if not path.is_file():raise ValueError('crossplay_content_changed')
                actual=compatibility.digest(path)
                if actual!=digest and not self.renderer_adapter(edition,name,digest,actual):
                    raise ValueError('crossplay_content_changed')
                path=plain_asset(path);after=path.stat()
                if (before.st_size,before.st_mtime_ns)!=(after.st_size,after.st_mtime_ns):
                    raise ValueError('crossplay_content_changed')
            if asset_names(root,edition)!=expected:raise ValueError('crossplay_content_changed')
        except (OSError,ValueError):
            raise ValueError('crossplay_content_changed') from None
        return self.digest

    @staticmethod
    def renderer_adapter(edition,name,expected,actual):
        # Exact, render-only rebuilds retain the original room catalog ID.
        if edition not in REQUIRED or name!=REQUIRED[edition][1]:return False
        from platform_standalone import (GOLD_LOD400,GOLD_RC403,GOLD_RC405,GOLD_RC409,
                                         HISTORY_LOD400,HISTORY_RC405,HISTORY_RC409,LOD_GOLD,LOD_HISTORY)
        if edition==1:
            return expected==GOLD_RC403 and actual in (GOLD_RC405,LOD_GOLD,GOLD_RC409,GOLD_LOD400)
        return expected==HISTORY_RC405 and actual in (LOD_HISTORY,HISTORY_RC409,HISTORY_LOD400)


def read(path):
    path = compatibility.plain(path)
    if not path.is_file() or not 1 <= path.stat().st_size <= MAX_BYTES:
        raise ValueError('crossplay_catalog_invalid')
    with path.open('rb') as stream:
        raw = stream.read(MAX_BYTES + 1)
    return Catalog(raw)


def package_catalog(root, manifest):
    """A feature flag cannot promote an arbitrary catalog or ordinary adapter."""
    required = ('platform_independent', 'internet_easytier', 'gold_standalone',
                'steam_history', 'windows_helpers', 'game_directory', 'neo_logistics',
                'multiplayer_resume')
    if any(manifest.get(name) is not True for name in required):
        raise ValueError('crossplay_package_invalid')
    catalog = read(compatibility.plain(root) / PACKAGE_CATALOG)
    if catalog.digest.hex() not in APPROVED_DIGESTS:
        raise ValueError('crossplay_catalog_unapproved')
    files = manifest.get('files', {})
    for edition, profile in enumerate(catalog.profiles, 1):
        adapter = dict(profile)[REQUIRED[edition][1]]
        payload = ('payload/gold/payload/weplay_gold_startup.dll' if edition == 1
                   else 'payload/uplay_r1_loader.dll')
        actual=files.get(payload)
        if (adapter not in crossplay_session.ADAPTER_PINS.get(edition, ())
                or (actual!=adapter and not catalog.renderer_adapter(edition,REQUIRED[edition][1],adapter,actual))):
            raise ValueError('crossplay_adapter_unavailable')
    return catalog
