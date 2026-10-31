"""Private History Edition standalone export and relocation helper.

The export contains the owner's game files. Never publish it as a release asset.
The normal release App and its hash-checked Package are copied unchanged.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import uuid

FORMAT = 'weplay-private-standalone-v1'
RECORD = 'standalone.json'


def modules(app):
    sys.path[:0] = [str(app / 'Package/Tools/installer')]
    import desktop_backend
    return desktop_backend


def descriptor(root):
    path = root / RECORD
    if path.stat().st_size > 4096:
        raise ValueError('Standalone-Manifest ist zu gross.')
    value = json.loads(path.read_text(encoding='utf-8'))
    if (not isinstance(value, dict) or set(value) != {'format', 'instance', 'package_id'}
            or value['format'] != FORMAT
            or not isinstance(value['instance'], str)
            or not re.fullmatch('[0-9a-f]{32}', value['instance'])
            or not isinstance(value['package_id'], str)
            or not re.fullmatch('[0-9a-f]{64}', value['package_id'])):
        raise ValueError('Ungueltiges Standalone-Manifest.')
    return value


def accepted_executables(backend, manifest):
    return ((backend.install.EXPECTED_EXE, backend.install.STEAM_EXE)
            if manifest.get('steam_history', False) else (backend.install.EXPECTED_EXE,))


def recover_renderer_overlay(backend, desktop, game, package_id):
    """Retire only known renderer files after a pristine ZIP was overlaid.

    All game files must match the supplied baseline except the pinned DXVK
    DLL/config. Keep the old profile, caches and logs as a local backup. Saves,
    identity and preferences stay in place. A small journal makes interruption
    between profile rename and renderer removal recoverable.
    """
    graphics = backend.graphics
    baseline_raw = graphics._read(game / graphics.COMPAT, graphics.MAX_JSON)
    baseline = backend.content.validate(graphics._json(baseline_raw))
    if any(name.lower() in baseline['files'] for name in (graphics.DLL, graphics.CONF)):
        raise ValueError('Die Standalone-Sicherung enthaelt kein natives Grafik-Basismanifest.')
    owned = {graphics.DLL: graphics.DXVK_SHA256, graphics.CONF: graphics._hash(graphics.CONFIG)}
    for name, expected in owned.items():
        path = graphics._plain(game / name)
        if path.exists() and backend.content.digest(path) != expected:
            raise ValueError('Ein unbekanntes Grafikprofil wird nicht veraendert.')
    actual = backend.content.inventory(game)
    for name in owned:
        actual.pop(name.lower(), None)
    if actual != baseline['files']:
        raise ValueError('Die Spieldateien stimmen nicht mit der Standalone-Sicherung ueberein.')
    journal_path = game / 'standalone-renderer-recovery.json'
    journal = desktop.read(journal_path, limit=4096)
    folder = graphics._plain(game / graphics.STATE)
    if journal is None:
        if graphics._plain(folder / 'transaction.json').exists():
            raise ValueError('Eine laufende Grafiktransaktion muss zuerst abgeschlossen werden.')
        backup = folder.name + '-standalone-' + uuid.uuid4().hex
        journal = {'format': 'weplay-standalone-renderer-recovery-v1', 'package_id': package_id,
                   'baseline_sha256': graphics._hash(baseline_raw), 'backup': backup,
                   'had_profile': folder.exists()}
        desktop.write(journal_path, journal)
    if (set(journal) != {'format', 'package_id', 'baseline_sha256', 'backup', 'had_profile'}
            or journal['format'] != 'weplay-standalone-renderer-recovery-v1'
            or journal['package_id'] != package_id or journal['baseline_sha256'] != graphics._hash(baseline_raw)
            or type(journal['had_profile']) is not bool or not isinstance(journal['backup'], str)
            or not re.fullmatch(re.escape(folder.name) + '-standalone-[0-9a-f]{32}', journal['backup'])):
        raise ValueError('Die Standalone-Grafikwiederherstellung ist ungueltig.')
    backup = graphics._plain(folder.parent / journal['backup'])
    if journal['had_profile']:
        if folder.exists():
            if backup.exists():
                raise ValueError('Die Grafikprofilsicherung ist bereits belegt.')
            for path in folder.rglob('*'):
                graphics._plain(path)
            folder.rename(backup)
        if not backup.is_dir():
            raise ValueError('Die Grafikprofilsicherung fehlt.')
    for name, expected in owned.items():
        path = graphics._plain(game / name)
        if path.exists():
            if backend.content.digest(path) != expected:
                raise ValueError('Eine Grafikdatei wurde waehrend der Wiederherstellung veraendert.')
            path.unlink()
    backend.content.verify(game)
    graphics._plain(journal_path).unlink()


def bind(root):
    """Adopt only the one bundled instance; never follow an old absolute path."""
    backend = modules(root / 'App')
    root = backend.graphics._plain(root)
    value = descriptor(root)
    desktop = backend.Desktop(root / 'App')
    game = backend.graphics._plain(desktop.app / 'Instances' / value['instance'])
    with desktop.lock():
        package_id = desktop.package_id(full=True)
        raw = desktop.read(desktop.state / 'installation.json')
        repair_overlay = False
        # An already bound, valid launcher remains in charge of later updates.
        # Do not resurrect an old bundled instance after a managed game update.
        if raw is not None:
            try:
                installed = desktop.installed()
            except (ValueError, OSError):
                installed = None
            if installed is not None:
                if installed['container'] != str(desktop.app / 'Instances'):
                    raise ValueError('Standalone verwendet einen externen Spielordner.')
                if installed['current'] != value['instance']:
                    return desktop
                if installed['package_id'] == package_id:
                    try:
                        backend.choice._renderer(game, {})
                    except (OSError, ValueError):
                        repair_overlay = True
                    else:
                        if not (game / 'standalone-renderer-recovery.json').exists():
                            return desktop
            # A relocation can repair only this export's single managed copy.
            if (not isinstance(raw, dict)
                    or set(raw) != {'format', 'source', 'container', 'current', 'previous', 'package_id', 'renderer_policy'}
                    or raw['format'] != backend.INSTALL_FORMAT
                    or raw['current'] != value['instance'] or raw['previous'] != []
                    or not isinstance(raw['package_id'], str)
                    or not re.fullmatch('[0-9a-f]{64}', raw['package_id'])):
                raise ValueError('Spielstand der Verwaltung passt nicht zum Export. Neu exportieren.')
        if package_id != value['package_id']:
            raise ValueError('Standalone-Spiel und Launcher-Paket passen nicht zusammen.')
        if not game.is_dir():
            raise ValueError('Die mitgelieferte Spielkopie fehlt. Gesamten Standalone-Ordner kopieren.')
        backend.graphics.ensure_closed()
        package = desktop.read(desktop.package / 'package.json')
        baseline = backend.content.load(game / 'compatibility.json')
        backend.graphics._bundle(game, baseline)
        if backend.content.digest(game / 'Base/Bin/Settlers7R.exe') not in accepted_executables(backend, package):
            raise ValueError('Diese History Edition wird vom Standalone-Paket nicht unterstuetzt.')
        has_profile = (game / backend.graphics.STATE).exists()
        if has_profile and not repair_overlay:
            try:
                backend.choice._renderer(game, {})
            except (OSError, ValueError):
                repair_overlay = True
        if repair_overlay or (game / 'standalone-renderer-recovery.json').exists():
            with backend.graphics.session_lock(game):
                recover_renderer_overlay(backend, desktop, game, package_id)
        verified = backend.content.verify(game)
        expected = {'base/bin/uplay_r1_loader.dll': package['files']['payload/uplay_r1_loader.dll'],
                    'base/bba/interface.bba': package['patched_archive_sha256']}
        expected.update({name.removeprefix('payload/'): digest for name, digest in package['files'].items()
                         if name.startswith('payload/server/')})
        expected.update({'launcher/' + target.lower(): package['files']['tools/' + original.lower()]
                         for original, target in backend.install.launcher_sources(package)})
        if any(verified['files'].get(name) != digest for name, digest in expected.items()):
            raise ValueError('Die Spielkopie gehoert nicht zum mitgelieferten Release-Paket.')
        if backend.content.inventory(game, ('Runtime',)) != backend.content.inventory(desktop.package, ('Runtime',)):
            raise ValueError('Die mitgelieferte Spiellaufzeit ist beschaedigt.')
        # Renderer transactions acquire their own game lock.
        probe = desktop.probe(refresh=True)
        mode = desktop.renderer(game, 'auto', probe)
        with backend.graphics.session_lock(game):
            # Choose hardware on the destination, not the exporting PC.
            backend.choice.save_choice(game, desktop.default_choice(game, mode, probe))
            desktop.write(game / 'desktop-owner.json', {
                'format': backend.FORMAT, 'owner': desktop.owner, 'instance': value['instance']})
            # Renderer transactions publish their own verified content manifest.
            desktop.record_ownership(game)
            desktop.write(desktop.state / 'installation.json', {
                'format': backend.INSTALL_FORMAT, 'source': '',
                'container': str(game.parent), 'current': value['instance'], 'previous': [],
                'package_id': package_id, 'renderer_policy': 'auto'})
    return desktop


def build(app, source, output):
    backend = modules(app)
    app, source, output = map(backend.graphics._plain, (app, source, output))
    if output.exists() or not output.parent.is_dir():
        raise ValueError('Ein neuer Ausgabeordner in einem vorhandenen Ordner ist erforderlich.')
    for protected in (app, source):
        if output.is_relative_to(protected) or protected.is_relative_to(output):
            raise ValueError('Ausgabe muss getrennt von Spiel und Launcher liegen.')
    if (app / 'State').exists() or (app / 'Instances').exists():
        raise ValueError('Eine saubere Release-App ohne State/Instances verwenden.')
    manifest = backend.install.package_manifest(app / 'Package')
    base = backend.install.source_base(source, steam_supported=manifest.get('steam_history', False))
    executable = backend.content.digest(base / 'Bin/Settlers7R.exe')
    if executable not in accepted_executables(backend, manifest):
        raise ValueError('Nur eine unterstuetzte History Edition kann exportiert werden.')
    edition = 'Steam' if executable == backend.install.STEAM_EXE else 'Ubisoft'
    # Validate paths before copying; no links and no existing user app state.
    for path in app.rglob('*'):
        backend.graphics._plain(path)
    required = sum(p.stat().st_size for p in app.rglob('*') if p.is_file())
    required += sum(p.stat().st_size for p in base.rglob('*') if p.is_file()) + 1024**3
    if shutil.disk_usage(output.parent).free < required:
        raise ValueError('Nicht genug freier Speicher fuer den Standalone-Export.')
    output.mkdir()
    incomplete = output / 'EXPORT-INCOMPLETE.txt'
    incomplete.write_text('Export noch nicht abgeschlossen. Nicht kopieren oder starten.\n', encoding='utf-8')
    print('Kopiere sauberen WePlay-Starter ...', flush=True)
    shutil.copytree(app, output / 'App')
    desktop = backend.Desktop(output / 'App')
    instance = uuid.uuid4().hex
    container = desktop.app / 'Instances'
    container.mkdir()
    game = container / instance
    backend.install.prepare(desktop.package, source, game, publish_starters=False)
    backend.content.verify(game)
    if (game / 'LocalData').exists():
        raise ValueError('Der frische Export darf keine lokalen Identitaeten enthalten.')
    # No absolute source path, machine settings, accounts, rooms or identities.
    (output / RECORD).write_text(json.dumps({
        'format': FORMAT, 'instance': instance, 'package_id': desktop.package_id(full=True)}, indent=2) + '\n', encoding='utf-8')
    shutil.copyfile(Path(__file__), output / 'standalone.py')
    starter = ('@echo off\r\nsetlocal\r\n'
               '"%~dp0App\\Package\\Runtime\\python\\python.exe" -B "%~dp0standalone.py" '
               '--root "%~dp0."\r\nif errorlevel 1 pause\r\n')
    (output / 'WePlay-Starten.cmd').write_bytes(starter.encode('ascii'))
    (output / 'LIESMICH.txt').write_text(
        'WePlay - privater ' + edition + '-Standalone-Export\n\n'
        'Den GESAMTEN Ordner auf den anderen Windows-PC kopieren oder dort entpacken.\n'
        'In einen beschreibbaren lokalen Ordner legen, z.B. D:\\Spiele\\WePlay.\n'
        'WePlay-Starten.cmd starten. Beim ersten Start werden Dateien geprueft und\n'
        'Grafikkarte/Bildschirm auf diesem PC neu ausgewaehlt. Das kann dauern.\n'
        'Danach im normalen WePlay-Starter Namen eingeben und Spielmodus waehlen.\n'
        'Uplay/Ubisoft Connect und eine zweite Installation werden fuer diese\n'
        'vorbereitete Kopie nicht vom lokalen Adapter geladen.\n'
        + ('Bei der Steam-Version Steam geoeffnet lassen; die normale Steam-Spielpruefung bleibt aktiv.\n' if edition == 'Steam' else '') +
        'Python und .NET sind dabei; fuer LAN/Internet ggf. Windows-Freigabe bestaetigen.\n'
        'Windows 10 x64 ab Build 19045 oder Windows 11 erforderlich.\n\n'
        'Dies ist eine frische Kopie ohne deine Spielstaende, Konten, Discord-Verbindung\n'
        'oder lokalen Spielerkennungen. Jeden PC aus diesem unveraenderten Export\n'
        'einrichten, bevor darauf gespielt wird. Nicht laufend verschieben/kopieren.\n'
        'Die Originalinstallation bleibt unveraendert. Enthaltene Spieldateien\n'
        'sind eine private Sicherung; diesen Ordner nicht oeffentlich hochladen.\n\n'
        'Launcher-Updates bleiben verfuegbar. Ein spaeterer Update-Schritt, der\n'
        'Spieldateien neu vorbereitet, braucht wieder eine unterstuetzte Originalquelle.\n'
        'Nach einer solchen Neuinstallation fuer weitere Umzuege neu exportieren.\n'
        'Pruefung auf diesem PC ist kein Startnachweis auf deinem anderen PC\n'
        'und keine Garantie fuer desyncfreie Mehrspielerpartien.\n', encoding='utf-8')
    incomplete.unlink()
    print('Standalone-Export fertig: ' + str(output), flush=True)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path)
    parser.add_argument('--check-only', action='store_true')
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--app', type=Path)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        if args.build:
            if not all((args.app, args.source, args.output)):
                parser.error('--build requires --app, --source and --output')
            build(args.app, args.source, args.output)
        else:
            root = (args.root or Path(__file__).parent).absolute()
            if (root / 'EXPORT-INCOMPLETE.txt').exists():
                raise ValueError('Standalone-Export ist nicht abgeschlossen.')
            print('Pruefe Standalone-Verwaltung ...', flush=True)
            desktop = bind(root)
            if args.check_only:
                desktop.package_id(full=True)
                modules(desktop.app).content.verify(desktop.game())
                print('Standalone-Spielkopie und Starter sind gueltig.', flush=True)
            else:
                environment = {key: value for key, value in os.environ.items()
                               if not key.upper().startswith(('DOTNET_', 'CORECLR_', 'COR_', 'COMPLUS_'))}
                return subprocess.call([str(desktop.app / 'DesktopRuntime/dotnet.exe'),
                                        str(desktop.app / 'S7Launcher.dll')], cwd=desktop.app, env=environment)
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        print('Standalone abgebrochen: ' + str(exc), flush=True)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
