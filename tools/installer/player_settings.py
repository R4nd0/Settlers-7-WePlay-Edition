"""Local display name only; never changes the platform's stable identity."""
from pathlib import Path
import json
import unicodedata
import os
import tempfile

import compatibility as content
from lobby_directory import json_record

FORMAT = 's7-player-settings-v1'
FILE = 'LocalData/player-settings.json'
DEFAULT_NAME = 'LocalPlayer'
MAX_NAME_BYTES = 24


def player_name(value):
    if (not isinstance(value, str) or not value.strip() or value != value.strip()
            or not 1 <= len(value.encode('utf-8')) <= MAX_NAME_BYTES
            or any(unicodedata.category(c).startswith('C') for c in value)
            or any(c in value for c in '\r\n\0\u2028\u2029')):
        raise ValueError('Spielername: 1 bis24 UTF-8-Bytes, ohne Steuerzeichen.')
    return value


def load(root):
    path = content.plain(Path(root) / FILE)
    if not path.exists():
        return DEFAULT_NAME
    if not path.is_file() or path.stat().st_size > 4096:
        raise ValueError('Die lokalen Spielereinstellungen sind ungueltig.')
    with path.open('rb') as source:
        value = json_record(source.read(4097), 4096)
    if not isinstance(value, dict) or set(value) != {'format', 'display_name'} or value['format'] != FORMAT:
        raise ValueError('Unbekannte lokale Spielereinstellungen.')
    return player_name(value['display_name'])


def save(root, name):
    name = player_name(name)
    root = content.plain(root)
    destination = content.plain(root / FILE)
    # Preserve unknown files instead of blessing them by overwriting.
    load(root)
    destination.parent.mkdir(exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, prefix='player-', suffix='.tmp', delete=False) as output:
            temporary = Path(output.name)
            output.write(json.dumps({'format': FORMAT, 'display_name': name}, ensure_ascii=True).encode('ascii'))
            output.flush(); os.fsync(output.fileno())
        os.replace(temporary, destination)
    finally:
        if temporary and temporary.exists():
            temporary.unlink()
    return name
