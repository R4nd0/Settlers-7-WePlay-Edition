"""Per-PC graphics selection before launch, without changing game content.

The caller owns the launch/session lock. A cancel result is None and must abort
launch. GPU filtering is supported only by the verified optional DXVK profile;
native Direct3D9 is presented honestly as the automatic standard renderer.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import unicodedata

sys.path.insert(0, str(Path(__file__).resolve().parent))
import graphics_profile as graphics
import compatibility as content

FORMAT = 's7-graphics-choice-v1'
CHOICE_FILE = 'LocalData/graphics-choice.json'
MAX_RECORD = 4096
CANCELLED = 10


def validate_choice(value):
    """Validate the complete minimal record; never accept IDs or extra fields."""
    if (not isinstance(value, dict) or set(value) != {'format', 'choice'}
            or value['format'] != FORMAT):
        raise ValueError('Die gespeicherte Grafikauswahl hat ein unbekanntes Format.')
    name = value['choice']
    if name is not None and (not isinstance(name, str) or not name.strip()
            or name != name.strip() or any(unicodedata.category(c).startswith('C') for c in name)
            or len(name.encode('utf-8')) > 256):
        raise ValueError('Die gespeicherte Grafikauswahl ist ungueltig.')
    return {'format': FORMAT, 'choice': name}


def _record(raw):
    if not isinstance(raw, bytes) or len(raw) > MAX_RECORD:
        raise ValueError('Die Grafikauswahl ist zu gross oder ungueltig.')
    return validate_choice(graphics._json(raw.decode('utf-8', errors='strict')))


def _load_choice(root):
    path = graphics._plain(root / CHOICE_FILE)
    if not path.exists():
        return {'format': FORMAT, 'choice': None}
    return _record(graphics._read(path, MAX_RECORD))


def _renderer(root, inherited):
    """Read-only startup checks; never scan every large game archive.

    Backend extension boundary: add a separately verified renderer here and
    its explicit choice-to-environment adapter below. The saved model record
    is backend-independent. Native screen numbers are not GPU selectors.
    """
    folder = graphics._plain(root / graphics.STATE)
    if folder.exists():
        folder, versions, active = graphics._load_state(root, full_inventory=False)
        if graphics._plain(folder / 'transaction.json').exists():
            raise ValueError('Das Grafikprofil hat eine unvollstaendige Aenderung.')
        graphics._assert_state(root, versions[active])
        if active:
            return 'dxvk', graphics.launch_environment(root, inherited)
    else:
        expected = content.validate(graphics._json(graphics._read(root / graphics.COMPAT, graphics.MAX_JSON)))
        graphics._bundle(root, expected)
        if any(graphics._plain(root / name).exists() for name in (graphics.DLL, graphics.CONF)):
            raise ValueError('Ein unbekannter Grafikrenderer kann nicht zur Kartenauswahl verwendet werden.')
        if any(name.lower() in expected['files'] for name in (graphics.DLL, graphics.CONF)):
            raise ValueError('Die Rendererdateien passen nicht zum Vergleichsmanifest.')
    # Native D3D9 ignores DXVK's filter; never claim a concrete model was applied.
    environment = dict(os.environ if inherited is None else inherited)
    for name in list(environment):
        if name.upper() == 'DXVK_FILTER_DEVICE_NAME':
            del environment[name]
    return 'native', environment


def choose_launch_environment(root, inherited=None):
    """Show one hidden-host GUI per call. Return child env, or None on cancel.

    Call this while holding the launcher's existing session_lock, before
    starting the game (and preferably before starting its local service).
    Only LocalData/graphics-choice.json may be written here. The PowerShell
    dialog receives no paths, identities or credentials; stdout is a minimal
    format/choice record. No global os.environ values are changed.
    """
    root = graphics._preflight(root)
    mode, environment = _renderer(root, inherited)
    previous = _load_choice(root)
    script = graphics._plain(Path(__file__).with_name('Choose-Graphics.ps1'))
    if not script.is_file():
        raise ValueError('Der grafische Auswahldialog fehlt.')
    result = subprocess.run(
        ['powershell.exe', '-NoLogo', '-NoProfile', '-NonInteractive', '-STA',
         '-WindowStyle', 'Hidden', '-File', str(script), '-RendererMode', mode],
        input=graphics.encoded(previous), capture_output=True,
        creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode == CANCELLED:
        return None
    if result.returncode != 0:
        raise ValueError('Der Grafikauswahldialog konnte nicht abgeschlossen werden.')
    selected = _record(result.stdout)
    if mode == 'native' and selected['choice'] is not None:
        raise ValueError('Der Standardrenderer unterstuetzt diesen Grafikkartenfilter nicht.')
    current_mode, environment = _renderer(root, inherited)
    if current_mode != mode:
        raise ValueError('Das Grafikprofil wurde waehrend des Dialogs veraendert.')
    if mode == 'dxvk':
        environment = graphics.launch_environment(root, environment, device_name=selected['choice'])
    data = graphics._plain(root / 'LocalData')
    data.mkdir(exist_ok=True)
    target = graphics._plain(root / CHOICE_FILE)
    # Recheck its saved record before replacement. A changed unknown file is
    # not adopted while the user has the dialog open.
    if _load_choice(root) != previous:
        raise ValueError('Die Grafikauswahl wurde waehrend des Dialogs veraendert.')
    graphics.atomic_write(target, graphics.encoded(selected), data)
    return environment
