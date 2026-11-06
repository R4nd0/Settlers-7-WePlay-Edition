"""Deterministic content manifest. No account, path, player or machine identifiers.

The full inventory detects local file differences, not simulation divergence.
LAN admission verifies it before deriving the separate network identity.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath

FORMAT = 's7-content-v1'
ROOTS = ('Base', 'Server', 'Launcher')
NETWORK_FORMAT = 's7-play-content-v2'
# Change this when incompatible service/carrier semantics change. Native ABI
# and every game asset remain identified by their actual bytes below.
NETWORK_PROTOCOL = 'prudp7-neo3'
# These installers are never loaded by S7. They remain in the full local
# inventory and repair inputs, but do not determine whether two games match.
NETWORK_AUXILIARIES = frozenset(('base/support/vc_redist.x64.exe',
                               'base/support/vc_redist.x86.exe'))


def digest(path):
    with path.open('rb') as stream:
        result = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
        return result.hexdigest()


def member(root, relative):
    """Resolve a lower-case manifest name on a case-sensitive filesystem.

    Ambiguous names and links are rejected, just as in the full inventory.
    This does not rename or rewrite the user's source installation.
    """
    parts = PurePosixPath(relative).parts
    if not parts or '..' in parts or relative.startswith('/') or '\\' in relative or ':' in relative:
        raise ValueError('Ungueltiger Dateipfad.')
    current = plain(root)
    for part in parts:
        matches = [p for p in current.iterdir() if p.name.casefold() == part.casefold()]
        if len(matches) != 1:
            raise ValueError('Datei fehlt oder hat einen mehrdeutigen Namen: ' + relative)
        current = plain(matches[0])
    return current


def plain(path):
    path = Path(path).absolute()
    for part in (path, *path.parents):
        if part.is_symlink() or (hasattr(part, 'is_junction') and part.is_junction()):
            raise ValueError('Verknuepfungen sind im Testordner nicht erlaubt.')
    return path.resolve(strict=False)


def inventory(root, directories=ROOTS):
    root = plain(root)
    records = {}
    for directory in directories:
        start = plain(root / directory)
        if not start.is_dir():
            raise ValueError('Ein benoetigter Ordner fehlt: ' + directory)
        stack = [start]
        while stack:
            for path in sorted(stack.pop().iterdir()):
                plain(path)
                if path.is_dir():
                    stack.append(path)
                elif path.is_file():
                    key = path.relative_to(root).as_posix().lower()
                    if key in records:
                        raise ValueError('Mehrdeutiger Dateiname im Testordner.')
                    records[key] = digest(path)
                else:
                    raise ValueError('Unbekannter Dateityp im Testordner.')
    return records


def fingerprint(records):
    return hashlib.sha256(json.dumps(records, sort_keys=True, separators=(',', ':'), ensure_ascii=True).encode('ascii')).hexdigest()


def create(root):
    files = inventory(root)
    return {'format': FORMAT, 'content_id': fingerprint(files), 'files': files,
            'simulation_sync_verified': False, 'lan_join_verified': False}


def validate(value):
    if not isinstance(value, dict) or value.get('format') != FORMAT or not isinstance(value.get('files'), dict):
        raise ValueError('Unbekanntes Vergleichsformat.')
    files = value['files']
    if not files or len(files) > 20000:
        raise ValueError('Ungueltige Dateiliste.')
    for name, hash_value in files.items():
        if not isinstance(name, str) or name != name.lower() or '\\' in name or ':' in name:
            raise ValueError('Ungueltiger Dateipfad.')
        parts = PurePosixPath(name).parts
        if not parts or parts[0] not in tuple(x.lower() for x in ROOTS) or '..' in parts or name.startswith('/'):
            raise ValueError('Dateipfad ausserhalb der Vergleichsordner.')
        if not isinstance(hash_value, str) or len(hash_value) != 64 or any(c not in '0123456789abcdef' for c in hash_value):
            raise ValueError('Ungueltige Dateipruefsumme.')
    if value.get('content_id') != fingerprint(files):
        raise ValueError('Die Vergleichsdatei ist beschaedigt.')
    return value


def load(path):
    if Path(path).stat().st_size > 4 * 1024 * 1024:
        raise ValueError('Vergleichsdatei ist zu gross.')
    return validate(json.loads(Path(path).read_text(encoding='utf-8')))


def network_id(value):
    """Identify verified gameplay files separately from the full local inventory.

    Call verify() before admission as usual. The domain separates this contract
    from legacy full-file IDs; there is deliberately no legacy-ID fallback.
    """
    files = validate(value)['files']
    gameplay = {name: digest for name, digest in files.items()
                if name.startswith('base/') and name not in NETWORK_AUXILIARIES}
    if not gameplay:
        raise ValueError('Keine Spiel- oder Patchdateien fuer den Netzwerkvergleich.')
    return fingerprint({'format': NETWORK_FORMAT, 'protocol': NETWORK_PROTOCOL, 'files': gameplay})


def compare(left, right):
    validate(left); validate(right)
    return sorted(name for name in left['files'].keys() | right['files'].keys()
                  if left['files'].get(name) != right['files'].get(name))


def verify(root):
    expected = load(plain(Path(root) / 'compatibility.json'))
    changed = compare(expected, create(root))
    if changed:
        raise ValueError('Spieldateien wurden veraendert; bitte Testkopie neu vorbereiten: ' + ', '.join(changed[:8]))
    return expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', required=True)
    parser.add_argument('--compare')
    args = parser.parse_args()
    try:
        local = verify(Path(args.bundle))
        if args.compare:
            other = load(args.compare)
            changed = compare(local, other)
            if changed:
                print('Unterschiedliche lokale Dateien:\n' + '\n'.join(changed[:20]))
                if network_id(local) == network_id(other):
                    print('Die Netzwerkkennung stimmt ueberein; nur Installationshilfen unterscheiden sich.')
                return 2
        print('Dateistand stimmt ueberein. Das ist kein Nachweis einer desyncfreien Partie.')
        print('Vergleichskennung: ' + local['content_id'])
        print('Netzwerkkennung: ' + network_id(local))
        return 0
    except (OSError, ValueError, TypeError, KeyError) as exc:
        print(str(exc)); return 2


if __name__ == '__main__':
    raise SystemExit(main())
