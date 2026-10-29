"""Prepare an independent S7 test copy using the recipient's own game files.

Offline preparation only. No game launch, firewall rule, account access or
working-LAN claim. Failed copies retain INSTALLATION-INCOMPLETE.txt and no
starter. Existing output is never overwritten or automatically deleted.
"""
import argparse
import importlib.util
import hashlib
import json
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compatibility as content

FORMAT = 's7-patch-package-v1'
EXPECTED_EXE = '4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f'
EXPECTED_ARCHIVE = '766ae21ef06709309a68fb5bb792b0fd56373c668f02140122eef181917b0b94'
GRAPHICS_TOOLS = tuple(('installer/' + name, name) for name in
    ('graphics_profile.py', 'graphics_choice.py', 'Choose-Graphics.ps1', 'update_join_menu.py')) + (
    ('lan/bba.py', 'bba.py'), ('lan/prepare.py', 'prepare.py'))
DISPLAY_TOOLS = tuple(('installer/' + name, name) for name in ('display_settings.py', 'window_mode.py'))
DESKTOP_TOOLS = (('installer/quality_settings.py','quality_settings.py'),)
DESKTOP_PACKAGE_TOOLS = ('installer/desktop_backend.py','installer/game_detection.py','d3d11/memory_guard.py')
LAN_LOBBY_TOOLS = tuple(('installer/' + name, name) for name in (
    'launch_auto_lan.py', 'lan_control.py', 'lan_hub.py', 'lan_child_job.py', 'lan_interfaces.py',
    'lan_discovery.py', 'lan_autoconnect.py', 'lobby_directory.py', 'lobby_picker.py', 'Choose-Lobby.ps1',
    'player_settings.py', 'Configure-LAN-Firewall.ps1'))
INTERNET_TOOLS = tuple(('installer/' + name, name) for name in (
    'internet_room.py', 'easytier_runtime.py', 'easytier_elevation.py', 'easytier-runtime.json',
    'launch_internet.py', 'internet_private_channel.py', 'Configure-Internet-Firewall.ps1'))


def lan_tools(manifest):
    # RC1 used an external picker. Its unchanged package remains valid.
    return tuple(item for item in LAN_LOBBY_TOOLS if item[0]!='installer/lan_autoconnect.py' or
                 'tools/installer/lan_autoconnect.py' in manifest['files'])


def launcher_sources(manifest):
    files=[('offline-ui/launch_local.py','launch_local.py'),('installer/compatibility.py','compatibility.py'),
           ('installer/launch_lan.py','launch_lan.py'),('installer/start.py','start.py')]
    for capability,items in (('graphics_choice',GRAPHICS_TOOLS),('display_selection',DISPLAY_TOOLS),
                             ('desktop_launcher',DESKTOP_TOOLS),('automatic_lan',lan_tools(manifest)),
                             ('internet_easytier',INTERNET_TOOLS)):
        if manifest.get(capability,False):files.extend(items)
    return files


def write_json(path, value):
    with path.open('x', encoding='utf-8') as stream:
        json.dump(value, stream, indent=2, sort_keys=True); stream.write('\n')


def package_manifest(root):
    root = content.plain(root)
    manifest_path = content.plain(root / 'package.json')
    if manifest_path.stat().st_size > 4 * 1024 * 1024:
        raise ValueError('Installationsmanifest zu gross.')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if (manifest.get('format') != FORMAT or manifest.get('capabilities') != {'local_test': True, 'lan_host': False, 'lan_join': False}
            or not isinstance(manifest.get('files'), dict) or not manifest['files']):
        raise ValueError('Unbekanntes Installationspaket.')
    if type(manifest.get('join_menu_update', False)) is not bool:
        raise ValueError('Unbekannte Menuepatch-Version.')
    if type(manifest.get('lan_public_creation',False)) is not bool:
        raise ValueError('Unbekannte LAN-Spielerstellung-Version.')
    if type(manifest.get('graphics_choice', False)) is not bool:
        raise ValueError('Unbekannte Grafikauswahl-Version.')
    if type(manifest.get('display_selection', False)) is not bool:
        raise ValueError('Unbekannte Bildschirmauswahl-Version.')
    if type(manifest.get('automatic_lan', False)) is not bool:
        raise ValueError('Unbekannte LAN-Lobby-Version.')
    if type(manifest.get('desktop_launcher',False)) is not bool:
        raise ValueError('Unbekannte Desktop-Starter-Version.')
    if type(manifest.get('internet_easytier',False)) is not bool:
        raise ValueError('Unbekannte Internet-Transportversion.')
    actual = content.inventory(root, ('Payload', 'Runtime', 'Tools'))
    expected = manifest['files']
    if actual != expected:
        raise ValueError('Das Installationspaket ist unvollstaendig oder veraendert.')
    if manifest.get('internet_easytier',False):
        if not manifest.get('automatic_lan',False) or any(
                ('tools/'+original).lower() not in expected for original,_ in INTERNET_TOOLS):
            raise ValueError('Die EasyTier-Integration im Paket ist unvollstaendig.')
        from easytier_runtime import verify_runtime
        verify_runtime(root)
    if manifest.get('lan_public_creation',False) and (not manifest.get('join_menu_update',False) or
            not manifest.get('automatic_lan',False) or any(name not in expected for name in
            ('tools/installer/lan_public_game.py','tools/offline-ui/lan-public-creation.json'))):
        raise ValueError('Die LAN-Spielerstellung im Paket ist unvollstaendig.')
    if manifest.get('graphics_choice', False) and any(
            ('tools/' + original).lower() not in expected for original, _ in GRAPHICS_TOOLS):
        raise ValueError('Die Grafikauswahl im Installationspaket ist unvollstaendig.')
    if manifest.get('display_selection', False) and (not manifest.get('graphics_choice', False) or any(
            ('tools/' + original).lower() not in expected for original, _ in DISPLAY_TOOLS)):
        raise ValueError('Die Bildschirmauswahl im Installationspaket ist unvollstaendig.')
    if manifest.get('automatic_lan', False) and (not manifest.get('graphics_choice', False) or any(
            ('tools/' + original).lower() not in expected for original, _ in lan_tools(manifest))):
        raise ValueError('Die automatische LAN-Lobby im Installationspaket ist unvollstaendig.')
    if manifest.get('desktop_launcher',False) and (not all(manifest.get(key,False) for key in
            ('graphics_choice','display_selection','automatic_lan')) or any(
            ('tools/'+name).lower() not in expected for name in (*DESKTOP_PACKAGE_TOOLS,*(original for original,_ in DESKTOP_TOOLS)))
            or 'payload/graphics/s7graphicsprobe.exe' not in expected
            or 'tools/installer/lan_autoconnect.py' not in expected):
        raise ValueError('Der Desktop-Starter im Installationspaket ist unvollstaendig.')
    return manifest


def source_base(root):
    root = content.plain(root)
    bases = [root / name for name in ('Base', 'Data/Base') if (root / name / 'Bin/Settlers7R.exe').is_file()]
    if len(bases) != 1:
        raise ValueError('Bitte den S7-Installationsordner mit genau einem Base-Ordner auswaehlen.')
    base = content.plain(bases[0])
    exe = content.plain(base / 'Bin/Settlers7R.exe')
    archive = content.plain(base / 'BBA/Interface.bba')
    if content.digest(exe) != EXPECTED_EXE or content.digest(archive) != EXPECTED_ARCHIVE:
        raise ValueError('Diese Spielversion ist veraendert oder wird noch nicht unterstuetzt.')
    return base


def _rollback_copies(records):
    """Remove only this copy operation's unchanged files, never unknown data."""
    for path,identity,digest in reversed(records):
        try:
            path=content.plain(path);info=path.stat()
            if (info.st_dev,info.st_ino)==identity and content.digest(path)==digest:
                path.unlink()
        except (OSError,ValueError):
            pass  # A changed/unreadable file is retained for recovery.


def _copy_sources(files,base,destination,emit,progress):
    total=sum(path.stat().st_size for path in files);copied=0;owned=[]
    try:
        for index,path in enumerate(files,1):
            content.plain(path)
            target=content.plain(destination/'Base'/path.relative_to(base))
            target.parent.mkdir(parents=True,exist_ok=True)
            hasher=hashlib.sha256();identity=None
            try:
                with path.open('rb') as src,target.open('xb') as dst:
                    info=target.stat();identity=(info.st_dev,info.st_ino)
                    if progress is None:shutil.copyfileobj(src,dst,1024*1024)
                    else:
                        while chunk:=src.read(1024*1024):
                            dst.write(chunk);hasher.update(chunk);copied+=len(chunk)
                            progress('copy',copied,total)
            except BaseException:
                if progress and identity is not None:_rollback_copies([(target,identity,hasher.hexdigest())])
                raise
            if progress:
                owned.append((target,identity,hasher.hexdigest()))
                progress('copy_check',index,len(files))
            if content.digest(target)!=content.digest(path):
                raise ValueError('Spieldatei hat sich waehrend des Kopierens geaendert.')
            emit(f'Spieldateien kopiert: {index}/{len(files)}')
        # The last cancellable boundary precedes patching/renderer transactions.
        if progress:progress('patch',0,0)
    except BaseException:
        if progress:_rollback_copies(owned)
        raise


def prepare(package, source, destination, emit=print, *, progress=None, publish_starters=True):
    package, source, destination = map(content.plain, (package, source, destination))
    for protected in (source, package):
        if destination.is_relative_to(protected) or protected.is_relative_to(destination):
            raise ValueError('Der neue Testordner muss getrennt von Spiel und Installer liegen.')
    if destination.exists() or not destination.parent.is_dir():
        raise ValueError('Bitte einen neuen Testordner in einem vorhandenen Ordner angeben.')
    if progress: progress('package',0,0)
    manifest = package_manifest(package)
    if progress: progress('source',0,0)
    base = source_base(source)
    emit('Pruefe die Spieldateien. Das kann etwas dauern ...')
    # Validate every source path before creating output; no links/hardlinks used.
    files = []
    stack = [base]
    while stack:
        for path in sorted(stack.pop().iterdir()):
            content.plain(path)
            if path.is_dir(): stack.append(path)
            elif path.is_file(): files.append(path)
            else: raise ValueError('Unbekannter Dateityp in der Spielinstallation.')
    required = sum(p.stat().st_size for p in files) + sum(p.stat().st_size for p in package.rglob('*') if p.is_file()) + 512 * 1024 * 1024
    if shutil.disk_usage(destination.parent).free < required:
        raise ValueError('Nicht genug freier Speicher fuer eine unabhaengige Testkopie.')
    destination.mkdir()
    incomplete = destination / 'INSTALLATION-INCOMPLETE.txt'
    incomplete.write_text('Vorbereitung nicht abgeschlossen. Diesen Ordner nicht starten.\n', encoding='utf-8')
    _copy_sources(files,base,destination,emit,progress)
    # Reuse the existing hash-locked patcher; patch the copied archive only.
    tools = package / 'Tools'
    sys.path.insert(0, str(tools / 'lan'))
    spec = importlib.util.spec_from_file_location('s7_installer_archive', tools / 'lan/prepare.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    stage = destination / 'ArchiveStage'
    # Archive preparation requires an output outside the input root. Use original
    # source, with a new staging directory inside this separate destination.
    module.prepare(source, stage, tools / 'offline-ui/patch-spec.json', profile='offline-ui')
    staged = stage / base.relative_to(source) / 'BBA/Interface.bba'
    if manifest.get('join_menu_update', False):
        # Reuse the already verified pure archive transformation, without running
        # the installed-copy updater, process checks or its old adapter policy.
        menu_spec = importlib.util.spec_from_file_location('s7_installer_join_menu', tools / 'installer/update_join_menu.py')
        menu = importlib.util.module_from_spec(menu_spec)
        sys.modules[menu_spec.name] = menu  # Required by the module's dataclasses.
        try:
            menu_spec.loader.exec_module(menu)
            recipe = json.loads((tools / 'offline-ui/join-menu-update.json').read_text(encoding='utf-8'))
            patched = menu.build_archive(staged.read_bytes(), recipe)
        finally:
            sys.modules.pop(menu_spec.name, None)
        (destination / 'Base/BBA/Interface.bba').write_bytes(patched)
    else:
        shutil.copyfile(staged, destination / 'Base/BBA/Interface.bba')
    if manifest.get('lan_public_creation',False):
        target=destination/'Base/BBA/Interface.bba'
        public_spec=importlib.util.spec_from_file_location('s7_installer_public_creation',tools/'installer/lan_public_game.py')
        public_menu=importlib.util.module_from_spec(public_spec)
        public_spec.loader.exec_module(public_menu)
        recipe=json.loads((tools/'offline-ui/lan-public-creation.json').read_text(encoding='utf-8'))
        target.write_bytes(public_menu.build_archive(target.read_bytes(),recipe))
    if content.digest(destination / 'Base/BBA/Interface.bba') != manifest['patched_archive_sha256']:
        raise ValueError('Der vorbereitete Menuepatch stimmt nicht mit dem Paket ueberein.')
    shutil.copyfile(package / 'Payload/uplay_r1_loader.dll', destination / 'Base/Bin/uplay_r1_loader.dll')
    shutil.copytree(package / 'Payload/Server', destination / 'Server')
    shutil.copytree(package / 'Runtime', destination / 'Runtime')
    (destination / 'Launcher').mkdir()
    for original, target in [('offline-ui/launch_local.py', 'launch_local.py'), ('installer/compatibility.py', 'compatibility.py'), ('installer/launch_lan.py', 'launch_lan.py')]:
        shutil.copyfile(tools / original, destination / 'Launcher' / target)
    shutil.copyfile(tools / 'installer/start.py', destination / 'Launcher/start.py')
    if manifest.get('graphics_choice', False):
        for original, target in GRAPHICS_TOOLS:
            shutil.copyfile(tools / original, destination / 'Launcher' / target)
    if manifest.get('display_selection', False):
        for original, target in DISPLAY_TOOLS:
            shutil.copyfile(tools / original, destination / 'Launcher' / target)
    if manifest.get('desktop_launcher',False):
        for original,target in DESKTOP_TOOLS:
            shutil.copyfile(tools/original,destination/'Launcher'/target)
    if manifest.get('automatic_lan', False):
        for original, target in lan_tools(manifest):
            shutil.copyfile(tools / original, destination / 'Launcher' / target)
    if manifest.get('internet_easytier',False):
        for original,target in INTERNET_TOOLS:
            shutil.copyfile(tools/original,destination/'Launcher'/target)
    shutil.copyfile(tools / 'LICENSE.txt', destination / 'Launcher/LICENSE.txt')
    bundle = {'format': 's7-local-experimental-v1', 'runtime_verified': False, 'lan_verified': False,
              'files': {name: content.digest(destination / name) for name in
                        ('Base/Bin/Settlers7R.exe', 'Base/Bin/uplay_r1_loader.dll', 'Base/BBA/Interface.bba')}}
    if bundle['files']['Base/Bin/Settlers7R.exe'] != EXPECTED_EXE:
        raise ValueError('Spielprogramm hat sich waehrend der Vorbereitung geaendert.')
    # Detect modified payloads during copying, before publishing any starter.
    if progress: progress('verify',0,0)
    package_manifest(package)
    if content.digest(destination / 'Base/Bin/uplay_r1_loader.dll') != manifest['files']['payload/uplay_r1_loader.dll']:
        raise ValueError('Adapterkopie stimmt nicht ueberein.')
    for folder in ('Server',):
        if content.inventory(destination, (folder,)) != content.inventory(package / 'Payload', (folder,)):
            raise ValueError('Dienstkopie stimmt nicht ueberein.')
    if content.inventory(destination, ('Runtime',)) != content.inventory(package, ('Runtime',)):
        raise ValueError('Laufzeitkopie stimmt nicht ueberein.')
    launcher_tools = [('offline-ui/launch_local.py', 'launch_local.py'),
                      ('installer/compatibility.py', 'compatibility.py'), ('installer/launch_lan.py', 'launch_lan.py'), ('installer/start.py', 'start.py')]
    if manifest.get('graphics_choice', False):
        launcher_tools += list(GRAPHICS_TOOLS)
    if manifest.get('display_selection', False):
        launcher_tools += list(DISPLAY_TOOLS)
    if manifest.get('desktop_launcher',False):
        launcher_tools += list(DESKTOP_TOOLS)
    if manifest.get('automatic_lan', False):
        launcher_tools += list(lan_tools(manifest))
    if manifest.get('internet_easytier',False):
        launcher_tools += list(INTERNET_TOOLS)
    for original, target in launcher_tools:
        if content.digest(tools / original) != content.digest(destination / 'Launcher' / target):
            raise ValueError('Starterkopie stimmt nicht ueberein.')
    write_json(destination / 'local-bundle.json', bundle)
    write_json(destination / 'compatibility.json', content.create(destination))
    if not publish_starters:
        incomplete.unlink()
        emit('Spielkopie ist fuer den gemeinsamen Starter vorbereitet.')
        return destination
    (destination / 'LIESMICH.txt').write_text(
        'S7 lokale Testkopie\n\nStart-S7-Test.cmd startet den lokalen Test.\n'
        'Test-LAN-Verbindung.cmd bietet Host/Mitspieler fuer den Sitzungstest.\n'
        'Host zuerst starten, dann den Mitspieler. Beide Spiele vor einem gemeinsamen Neustart schliessen.\n'
        'Dateistand-pruefen.cmd prueft Spiel und Patch auf Veraenderungen.\n'
        'compatibility.json kann zum Vergleich auf den zweiten PC kopiert werden.\n'
        'Gleiche Dateien allein garantieren keine desyncfreie Simulation.\n'
        'Die Originalinstallation wurde nicht veraendert. Zum Entfernen diese getrennte Kopie loeschen.\n', encoding='utf-8')
    starter = '@echo off\n"%~dp0Runtime\\python\\python.exe" -B "%~dp0Launcher\\start.py" --bundle "%~dp0."\nif errorlevel 1 pause\n'
    with (destination / 'Start-S7-Test.cmd').open('x', encoding='ascii') as stream: stream.write(starter)
    with (destination / 'Dateistand-pruefen.cmd').open('x', encoding='ascii') as stream:
        stream.write('@echo off\n"%~dp0Runtime\\python\\python.exe" -B "%~dp0Launcher\\compatibility.py" --bundle "%~dp0."\npause\n')
    with (destination / 'Test-LAN-Verbindung.cmd').open('x', encoding='ascii') as stream:
        stream.write('@echo off\n"%~dp0Runtime\\python\\python.exe" -B "%~dp0Launcher\\launch_lan.py" --bundle "%~dp0."\npause\n')
    if manifest.get('automatic_lan', False):
        with (destination / 'Start-S7-LAN.cmd').open('x', encoding='ascii') as stream:
            stream.write('@echo off\n"%~dp0Runtime\\python\\python.exe" -B "%~dp0Launcher\\launch_auto_lan.py" --bundle "%~dp0."\nif errorlevel 1 pause\n')
        with (destination / 'LAN-Netzwerk-freigeben.cmd').open('x', encoding='ascii') as stream:
            stream.write('@echo off\npowershell.exe -NoLogo -NoProfile -File "%~dp0Launcher\\Configure-LAN-Firewall.ps1" -Bundle "%~dp0."\npause\n')
        with (destination / 'LIESMICH.txt').open('a', encoding='utf-8') as stream:
            stream.write('\nAutomatische LAN-Lobby: Start-S7-LAN.cmd. Spielernamen eingeben und Lobby auswaehlen oder erstellen.\n'
                'Keine IP-Eingabe erforderlich. Bei mehreren Netzwerken das gemeinsame Heimnetz auswaehlen.\n'
                'Falls die Lobby oder Mitspieler nicht erreichbar sind: LAN-Netzwerk-freigeben.cmd einmal starten.\n'
                'Das richtet nach Windows-Bestaetigung nur private Freigaben fuer diese Kopie im lokalen Subnetz ein.\n'
                'Der Lobbyrechner muss erreichbar bleiben; sein Spiel darf beendet werden, waehrend andere spielen.\n'
                'Im LAN ist der Entwicklermodus immer deaktiviert.\n')
    incomplete.unlink()
    emit('Testkopie ist vorbereitet. Start: ' + ('Start-S7-LAN.cmd.' if manifest.get('automatic_lan') else 'Test-LAN-Verbindung.cmd.'))
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', default=str(Path(__file__).resolve().parents[2]))
    parser.add_argument('--source'); parser.add_argument('--destination')
    args = parser.parse_args()
    try:
        print('S7 LAN-Vorbereitung: getrennte Testkopie.')
        source = args.source or input('S7-Installationsordner: ').strip().strip('"')
        destination = args.destination or input('Neuer Ziel-Unterordner (vorher nicht anlegen): ').strip().strip('"')
        if not source or not destination: raise ValueError('Beide Ordner werden benoetigt.')
        prepare(Path(args.package), Path(source), Path(destination))
        return 0
    except (OSError, ValueError, KeyError, TypeError, EOFError) as exc:
        print('Vorbereitung abgebrochen: ' + str(exc)); return 2


if __name__ == '__main__':
    raise SystemExit(main())
