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
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compatibility as content

FORMAT = 's7-patch-package-v1'
EXPECTED_EXE = '4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f'
# Identified by a read-only comparison of the owner's original Steam copy.
# Its different native layout needs the exact Steam-capable adapter below.
UNSUPPORTED_STEAM_EXE = '8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410'
STEAM_EXE = UNSUPPORTED_STEAM_EXE
STEAM_ADAPTER_SHA256 = '28a4d90a97e816b02f7f4d6fbba074fb1c5a6564bf2402712e400e7fb60acfef'
DIRECTORY_ADAPTER_SHA256 = '6196de74455a9e33365c32833ec6fe80f5bc31b5514643dc475a879546399855'
EXPECTED_ARCHIVE = '766ae21ef06709309a68fb5bb792b0fd56373c668f02140122eef181917b0b94'
GRAPHICS_TOOLS = tuple(('installer/' + name, name) for name in
    ('graphics_profile.py', 'graphics_choice.py', 'Choose-Graphics.ps1', 'update_join_menu.py')) + (
    ('lan/bba.py', 'bba.py'), ('lan/prepare.py', 'prepare.py'))
DISPLAY_TOOLS = tuple(('installer/' + name, name) for name in ('display_settings.py', 'window_mode.py'))
DESKTOP_TOOLS = (('installer/quality_settings.py','quality_settings.py'),)
DESKTOP_PACKAGE_TOOLS = ('installer/desktop_backend.py','installer/game_detection.py','d3d11/memory_guard.py')
STANDALONE_PACKAGE_TOOLS = ('installer/standalone.py',)
DISCORD_PACKAGE_TOOLS = ('installer/discord_credentials.py','installer/discord_directory.py')
PUBLIC_DIRECTORY_PACKAGE_TOOLS = ('installer/public_directory.py',)
DIRECTORY_PACKAGE_TOOLS = ('installer/game_directory.py','installer/join_crypto.py','installer/directory-service.json',
    'installer/ingame_directory.py','installer/ingame_invitation.py','game-directory/interface_patch.py',
    'game-directory/session_list.lua','game-directory/host_invitation.lua')
UPDATE_PACKAGE_TOOLS = ('installer/update_client.py','installer/update_handoff.ps1','installer/update-key.json')
ACHIEVEMENT_PACKAGE_TOOLS = ('installer/achievement_notifications.py','offline-ui/achievement-notifications.json')
CARTOGRAPHER_PACKAGE_TOOLS = ('installer/cartographer_context.py','offline-ui/cartographer-context.json')
MULTIPLAYER_RESUME_TOOLS = tuple('multiplayer-resume/' + name for name in
    ('interface_patch.py', 'runtime.lua', 'session_list.lua', 'host_lobby.lua'))
WINDOWS_API_TOOLS = (('installer/windows_system.py','windows_system.py'),)
LAN_LOBBY_TOOLS = tuple(('installer/' + name, name) for name in (
    'launch_auto_lan.py', 'lan_control.py', 'lan_hub.py', 'lan_child_job.py', 'lan_interfaces.py',
    'lan_discovery.py', 'lan_autoconnect.py', 'lobby_directory.py', 'lobby_picker.py', 'Choose-Lobby.ps1',
    'player_settings.py', 'Configure-LAN-Firewall.ps1'))
INTERNET_TOOLS = tuple(('installer/' + name, name) for name in (
    'internet_room.py', 'easytier_runtime.py', 'easytier_elevation.py', 'easytier-runtime.json',
    'launch_internet.py', 'internet_private_channel.py', 'Configure-Internet-Firewall.ps1'))


def lan_tools(manifest):
    # RC1 used an external picker. Its unchanged package remains valid.
    return script_tools(tuple(item for item in LAN_LOBBY_TOOLS if item[0]!='installer/lan_autoconnect.py' or
                 'tools/installer/lan_autoconnect.py' in manifest['files']),manifest)


def script_tools(items,manifest):
    return tuple(item for item in items if not (manifest.get('windows_helpers',False) and item[0].endswith('.ps1')))


def update_package_tools(manifest):
    return tuple(name for name in UPDATE_PACKAGE_TOOLS if not (manifest.get('windows_helpers',False) and name.endswith('.ps1')))


def launcher_sources(manifest):
    files=[('offline-ui/launch_local.py','launch_local.py'),('installer/compatibility.py','compatibility.py'),
           ('installer/launch_lan.py','launch_lan.py'),('installer/start.py','start.py')]
    for capability,items in (('graphics_choice',GRAPHICS_TOOLS),('display_selection',DISPLAY_TOOLS),
                             ('desktop_launcher',DESKTOP_TOOLS),('automatic_lan',lan_tools(manifest)),
                             ('internet_easytier',INTERNET_TOOLS)):
        if manifest.get(capability,False):files.extend(script_tools(items,manifest))
    if manifest.get('windows_helpers',False):files.extend(WINDOWS_API_TOOLS)
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
    if type(manifest.get('discord_directory',False)) is not bool:
        raise ValueError('Unbekannte Discord-Verzeichnisversion.')
    for capability in ('update_patcher','achievement_notifications_disabled','cartographer_context','windows_helpers','multiplayer_castles','standalone_export','multiplayer_resume','game_directory','public_game_directory'):
        if type(manifest.get(capability,False)) is not bool:raise ValueError('Unbekannte Paketfunktion.')
    actual = content.inventory(root, ('Payload', 'Runtime', 'Tools'))
    expected = manifest['files']
    if actual != expected:
        raise ValueError('Das Installationspaket ist unvollstaendig oder veraendert.')
    if manifest.get('multiplayer_resume', False) and (
            not manifest.get('cartographer_context', False) or
            any(('tools/' + name).lower() not in expected for name in MULTIPLAYER_RESUME_TOOLS)):
        raise ValueError('Die Mehrspieler-Sicherungswerkzeuge im Paket sind unvollstaendig.')
    if manifest.get('standalone_export',False) and (not manifest.get('desktop_launcher',False) or
            any(('tools/'+name).lower() not in expected for name in STANDALONE_PACKAGE_TOOLS)):
        raise ValueError('Die Standalone-Werkzeuge im Paket sind unvollstaendig.')
    if manifest.get('internet_easytier',False):
        if not manifest.get('automatic_lan',False) or any(
                ('tools/'+original).lower() not in expected for original,_ in script_tools(INTERNET_TOOLS,manifest)):
            raise ValueError('Die EasyTier-Integration im Paket ist unvollstaendig.')
        from easytier_runtime import verify_runtime
        verify_runtime(root)
    if type(manifest.get('steam_history',False)) is not bool:
        raise ValueError('Unbekannte Steam-Adapterversion.')
    required_adapter=DIRECTORY_ADAPTER_SHA256 if manifest.get('game_directory',False) else STEAM_ADAPTER_SHA256
    if manifest.get('steam_history',False) and expected.get('payload/uplay_r1_loader.dll') != required_adapter:
        raise ValueError('Der Steam-Adapter im Paket stimmt nicht mit dem geprueften Build ueberein.')
    if manifest.get('multiplayer_castles',False) and expected.get('payload/uplay_r1_loader.dll') != required_adapter:
        raise ValueError('Der Castle-Adapter im Paket stimmt nicht mit dem geprueften Build ueberein.')
    if manifest.get('discord_directory',False) and (not manifest.get('desktop_launcher',False) or
            not manifest.get('internet_easytier',False) or any(('tools/'+name).lower() not in expected for name in DISCORD_PACKAGE_TOOLS)):
        raise ValueError('Die Discord-Verzeichnisintegration im Paket ist unvollstaendig.')
    if manifest.get('game_directory',False) and (not manifest.get('discord_directory',False) or not manifest.get('windows_helpers',False)
            or not manifest.get('multiplayer_resume',False) or expected.get('payload/uplay_r1_loader.dll')!=DIRECTORY_ADAPTER_SHA256
            or any(('tools/'+name).lower() not in expected for name in DIRECTORY_PACKAGE_TOOLS)):
        raise ValueError('Das gemeinsame Spieleverzeichnis im Paket ist unvollstaendig.')
    if manifest.get('public_game_directory',False) and (not manifest.get('game_directory',False) or
            any(('tools/'+name).lower() not in expected for name in PUBLIC_DIRECTORY_PACKAGE_TOOLS)):
        raise ValueError('Die oeffentliche Lobby im Paket ist unvollstaendig.')
    if manifest.get('update_patcher',False) and (not manifest.get('desktop_launcher',False) or
            any(('tools/'+name).lower() not in expected for name in update_package_tools(manifest))):
        raise ValueError('Der Update-Patcher im Paket ist unvollstaendig.')
    if manifest.get('windows_helpers',False):
        from windows_system import SYSTEM_FILES
        required=['tools/'+original for original,_ in WINDOWS_API_TOOLS]
        required+=['runtime/dotnet/'+name.lower() for name in SYSTEM_FILES]
        if manifest.get('update_patcher',False):required+=['payload/helpers/weplay.updatehelper.exe']
        if any(name not in expected for name in required) or any(name.endswith('.ps1') for name in expected):
            raise ValueError('Die Windows-Hilfsprogramme im Paket sind unvollstaendig.')
    if manifest.get('achievement_notifications_disabled',False) and (not manifest.get('lan_public_creation',False) or
            any(('tools/'+name).lower() not in expected for name in ACHIEVEMENT_PACKAGE_TOOLS)):
        raise ValueError('Die Errungenschaftsanpassung im Paket ist unvollstaendig.')
    if manifest.get('cartographer_context',False) and (not manifest.get('achievement_notifications_disabled',False) or
            any(('tools/'+name).lower() not in expected for name in CARTOGRAPHER_PACKAGE_TOOLS)):
        raise ValueError('Die Kartograph-Anpassung im Paket ist unvollstaendig.')
    if manifest.get('lan_public_creation',False) and (not manifest.get('join_menu_update',False) or
            not manifest.get('automatic_lan',False) or any(name not in expected for name in
            ('tools/installer/lan_public_game.py','tools/offline-ui/lan-public-creation.json'))):
        raise ValueError('Die LAN-Spielerstellung im Paket ist unvollstaendig.')
    if manifest.get('graphics_choice', False) and any(
            ('tools/' + original).lower() not in expected for original, _ in script_tools(GRAPHICS_TOOLS,manifest)):
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


class UnsupportedSteamBuild(ValueError):
    code = 'source_steam_unsupported'

    def __init__(self):
        super().__init__('Dieses Installationspaket enthaelt noch keinen passenden Steam-Adapter. '
                         'Bitte ein neueres Paket mit Steam-Unterstuetzung oder die unterstuetzte '
                         'Ubisoft-Connect-Installation verwenden. Die Originaldateien bleiben unveraendert.')


def source_base(root, *, steam_supported=False):
    root = content.plain(root)
    bases = [root / name for name in ('Base', 'Data/Base') if (root / name / 'Bin/Settlers7R.exe').is_file()]
    if len(bases) != 1:
        raise ValueError('Bitte den S7-Installationsordner mit genau einem Base-Ordner auswaehlen.')
    base = content.plain(bases[0])
    exe = content.plain(base / 'Bin/Settlers7R.exe')
    archive = content.plain(base / 'BBA/Interface.bba')
    exe_hash = content.digest(exe)
    if exe_hash == STEAM_EXE and steam_supported:
        if content.digest(archive) != EXPECTED_ARCHIVE:
            raise ValueError('Diese Spielversion ist veraendert oder wird noch nicht unterstuetzt.')
        return base
    if exe_hash == UNSUPPORTED_STEAM_EXE:
        raise UnsupportedSteamBuild()
    if exe_hash != EXPECTED_EXE or content.digest(archive) != EXPECTED_ARCHIVE:
        raise ValueError('Diese Spielversion ist veraendert oder wird noch nicht unterstuetzt.')
    return base


REPAIR = 'RepairSource'
REPAIR_INPUTS = ('base/bin/settlers7r.exe', 'base/bba/interface.bba')
GENERATED_BASE = ('base/bin/uplay_r1_loader.dll', 'base/bin/d3d9.dll', 'base/bin/dxvk.conf')


def repair_source(root, *, steam_supported=False, tools=None):
    """Return original inputs and retained assets of a managed copy, read-only.

    Old exports retain the pristine archive body in ArchiveStage: BBA edits
    append payloads/directories and replace only the header. Recover that header
    from the retained original directory, then require the exact stock SHA-256.
    No original installation, drive search or relaxed executable guard is used.
    """
    root = content.plain(root)
    saved = content.plain(root / REPAIR)
    record = content.plain(saved / 'files.json')
    if record.exists():
        files = content.load(record)['files']
        if any(not name.startswith('base/') or name in GENERATED_BASE
               or Path(name).as_posix()!=name for name in files):
            raise ValueError('Ungueltige lokale Reparaturdaten.')
        inputs = {name: content.plain(saved / name).read_bytes() for name in REPAIR_INPUTS}
    else:
        files = {name: digest for name, digest in content.load(root / 'compatibility.json')['files'].items()
                 if name.startswith('base/') and name not in GENERATED_BASE}
        inputs = {REPAIR_INPUTS[0]: content.plain(root / 'Base/Bin/Settlers7R.exe').read_bytes()}
        sys.path.insert(0, str(content.plain(Path(tools or Path(__file__).parents[1]) / 'lan')))
        from bba import Archive, HEAD_KEYS, crypt, crc32
        specification = json.loads(content.plain(Path(tools or Path(__file__).parents[1]) / 'offline-ui/patch-spec.json').read_text(encoding='utf-8'))
        candidates = [content.plain(root / 'ArchiveStage' / layout / 'BBA/Interface.bba')
                      for layout in ('Base', 'Data/Base')]
        candidates = [path for path in candidates if path.is_file()]
        if len(candidates) != 1:raise ValueError('Lokale Reparaturdaten fehlen. Originalquelle einmal erneut vorbereiten.')
        archive = Archive(candidates[0].read_bytes())
        size = min(archive.entries[item['path']].values[6] for item in specification['files'])
        directory_size = len(archive.directory)
        offset = size - directory_size
        if offset < 16 + archive.header_size or size > len(archive.raw):
            raise ValueError('Ungueltige lokale Archiv-Sicherung.')
        original = bytearray(archive.raw[:size])
        header = bytearray(archive.header)
        struct.pack_into('<4I', header, 0, offset, 0, directory_size, crc32(original[offset:size]))
        original[16:16 + archive.header_size] = crypt(header, HEAD_KEYS, 1231853475, 7, True)
        inputs[REPAIR_INPUTS[1]] = bytes(original)
        files[REPAIR_INPUTS[1]] = EXPECTED_ARCHIVE
    accepted = (EXPECTED_EXE, STEAM_EXE) if steam_supported else (EXPECTED_EXE,)
    if (hashlib.sha256(inputs[REPAIR_INPUTS[0]]).hexdigest() not in accepted
            or hashlib.sha256(inputs[REPAIR_INPUTS[1]]).hexdigest() != EXPECTED_ARCHIVE
            or any(files.get(name) != hashlib.sha256(raw).hexdigest() for name, raw in inputs.items())):
        raise ValueError('Lokale Reparaturdaten stimmen nicht mit der unterstuetzten History Edition ueberein.')
    return files, inputs


def _rollback_copies(records):
    """Remove only this copy operation's unchanged files, never unknown data."""
    for path,identity,digest in reversed(records):
        try:
            path=content.plain(path);info=path.stat()
            if (info.st_dev,info.st_ino)==identity and content.digest(path)==digest:
                path.unlink()
        except (OSError,ValueError):
            pass  # A changed/unreadable file is retained for recovery.


def _copy_sources(files,base,destination,emit,progress,overrides=None,expected=None,tracker=None):
    total=sum((overrides or {}).get(path,path).stat().st_size for path in files);copied=0;owned=[];verified={}
    try:
        for index,original in enumerate(files,1):
            path=(overrides or {}).get(original,original)
            content.plain(path)
            target=content.plain(destination/'Base'/original.relative_to(base))
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
            copied_digest=content.digest(target)
            if copied_digest!=content.digest(path) or (expected and original in expected and copied_digest!=expected[original]):
                raise ValueError('Spieldatei hat sich waehrend des Kopierens geaendert.')
            verified[('base/'+original.relative_to(base).as_posix()).lower()]=copied_digest
            if tracker:tracker(target,copied_digest)
            emit(f'Spieldateien kopiert: {index}/{len(files)}')
        # The last cancellable boundary precedes patching/renderer transactions.
        if progress:progress('patch',0,0)
        return verified
    except BaseException:
        if progress:_rollback_copies(owned)
        raise


def prepare(package, source, destination, emit=print, *, progress=None, publish_starters=True, managed=False,
            additional_bytes=0,on_created=None,tracker=None):
    package, source, destination = map(content.plain, (package, source, destination))
    for protected in (source, package):
        if destination.is_relative_to(protected) or protected.is_relative_to(destination):
            raise ValueError('Der neue Testordner muss getrennt von Spiel und Installer liegen.')
    if destination.exists() or not destination.parent.is_dir():
        raise ValueError('Bitte einen neuen Testordner in einem vorhandenen Ordner angeben.')
    if progress: progress('package',0,0)
    manifest = package_manifest(package)
    if progress: progress('source',0,0)
    recovery = repair_source(source,steam_supported=manifest.get('steam_history',False),tools=package/'Tools') if managed else None
    base = content.plain(source/'Base') if managed else source_base(source,steam_supported=manifest.get('steam_history',False))
    source_exe_hash = hashlib.sha256(recovery[1][REPAIR_INPUTS[0]]).hexdigest() if managed else content.digest(base/'Bin/Settlers7R.exe')
    accepted_executables = (EXPECTED_EXE, STEAM_EXE) if manifest.get('steam_history', False) else (EXPECTED_EXE,)
    if source_exe_hash not in accepted_executables:
        raise ValueError('Spielprogramm hat sich nach der Versionspruefung geaendert.')
    emit('Pruefe die Spieldateien. Das kann etwas dauern ...')
    # Validate every source path before creating output; no links/hardlinks used.
    files = []
    stack = [] if managed else [base]
    if managed:
        for name,digest in recovery[0].items():
            path=content.plain(source/name)
            if name not in REPAIR_INPUTS and not path.is_file():
                raise ValueError('Unveraenderte Spieldatei fehlt oder ist beschaedigt: '+name)
            files.append(path)
    while stack:
        for path in sorted(stack.pop().iterdir()):
            content.plain(path)
            if path.is_dir(): stack.append(path)
            elif path.is_file(): files.append(path)
            else: raise ValueError('Unbekannter Dateityp in der Spielinstallation.')
    if type(additional_bytes) is not int or additional_bytes<0:raise ValueError('Ungueltige Speicherreservierung.')
    input_sizes={name:len(recovery[1][name]) if managed else (base/name.removeprefix('base/')).stat().st_size
                 for name in REPAIR_INPUTS}
    required = (sum(len(recovery[1][name]) if managed and name in REPAIR_INPUTS else p.stat().st_size
                    for p in files for name in [('base/'+p.relative_to(base).as_posix()).lower()])
                + sum(p.stat().st_size for p in package.rglob('*') if p.is_file())
                + sum(input_sizes.values()) + 2*input_sizes[REPAIR_INPUTS[1]]
                + additional_bytes + 512 * 1024 * 1024)
    if shutil.disk_usage(destination.parent).free < required:
        raise ValueError('Nicht genug freier Speicher fuer eine unabhaengige Testkopie.')
    destination.mkdir()
    if on_created:on_created(destination)
    incomplete = destination / 'INSTALLATION-INCOMPLETE.txt'
    incomplete.write_text('Vorbereitung nicht abgeschlossen. Diesen Ordner nicht starten.\n', encoding='utf-8')
    if tracker:tracker(incomplete,content.digest(incomplete))
    saved=destination/REPAIR
    input_paths=dict(zip(REPAIR_INPUTS,(base/'Bin/Settlers7R.exe',base/'BBA/Interface.bba')))
    inputs=recovery[1] if managed else {name:content.plain(path).read_bytes() for name,path in input_paths.items()}
    for name,raw in inputs.items():
        target=content.plain(saved/name);target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(raw)
        if tracker:tracker(target,hashlib.sha256(raw).hexdigest())
    source_base(saved,steam_supported=manifest.get('steam_history',False))
    overrides={source/name:saved/name for name in REPAIR_INPUTS} if managed else None
    # Missing/corrupt generated files are rebuilt from the package/repair inputs.
    expected={source/name:digest for name,digest in recovery[0].items()} if managed else {
        input_paths[name]:hashlib.sha256(raw).hexdigest() for name,raw in inputs.items()}
    copied=_copy_sources(files,base,destination,emit,progress,overrides,expected,tracker)
    original_files={name:digest for name,digest in copied.items() if name not in GENERATED_BASE}
    write_json(saved/'files.json',{'format':content.FORMAT,'files':original_files,'content_id':content.fingerprint(original_files)})
    if tracker:tracker(saved/'files.json',content.digest(saved/'files.json'))
    # Reuse the existing hash-locked patcher; patch the copied archive only.
    tools = package / 'Tools'
    sys.path.insert(0, str(tools / 'lan'))
    spec = importlib.util.spec_from_file_location('s7_installer_archive', tools / 'lan/prepare.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    stage = destination / 'ArchiveStage'
    # Patch the retained pristine input; staging is its separate sibling.
    module.prepare(saved, stage, tools / 'offline-ui/patch-spec.json', profile='offline-ui')
    staged = stage / 'Base/BBA/Interface.bba'
    if tracker:
        tracker(staged,content.digest(staged))
        if (stage/'manifest.json').is_file():tracker(stage/'manifest.json',content.digest(stage/'manifest.json'))
    def record_archive():
        if tracker:
            path=destination/'Base/BBA/Interface.bba';tracker(path,content.digest(path))
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
    record_archive()
    if manifest.get('lan_public_creation',False):
        target=destination/'Base/BBA/Interface.bba'
        public_spec=importlib.util.spec_from_file_location('s7_installer_public_creation',tools/'installer/lan_public_game.py')
        public_menu=importlib.util.module_from_spec(public_spec)
        public_spec.loader.exec_module(public_menu)
        recipe=json.loads((tools/'offline-ui/lan-public-creation.json').read_text(encoding='utf-8'))
        target.write_bytes(public_menu.build_archive(target.read_bytes(),recipe))
        record_archive()
    if manifest.get('achievement_notifications_disabled',False):
        target=destination/'Base/BBA/Interface.bba'
        achievement_spec=importlib.util.spec_from_file_location('s7_installer_achievements',tools/'installer/achievement_notifications.py')
        achievements=importlib.util.module_from_spec(achievement_spec);achievement_spec.loader.exec_module(achievements)
        recipe=json.loads((tools/'offline-ui/achievement-notifications.json').read_text(encoding='utf-8'))
        target.write_bytes(achievements.build_archive(target.read_bytes(),recipe))
        record_archive()
    if manifest.get('cartographer_context',False):
        target=destination/'Base/BBA/Interface.bba'
        context_spec=importlib.util.spec_from_file_location('s7_installer_cartographer',tools/'installer/cartographer_context.py')
        context=importlib.util.module_from_spec(context_spec);context_spec.loader.exec_module(context)
        recipe=json.loads((tools/'offline-ui/cartographer-context.json').read_text(encoding='utf-8'))
        target.write_bytes(context.build_archive(target.read_bytes(),recipe))
        record_archive()
    if manifest.get('multiplayer_resume', False):
        target = destination / 'Base/BBA/Interface.bba'
        resume_spec = importlib.util.spec_from_file_location('s7_installer_mp_resume', tools / 'multiplayer-resume/interface_patch.py')
        resume = importlib.util.module_from_spec(resume_spec); resume_spec.loader.exec_module(resume)
        target.write_bytes(resume.build(target.read_bytes()))
        record_archive()
    if manifest.get('game_directory',False):
        target=destination/'Base/BBA/Interface.bba'
        directory_spec=importlib.util.spec_from_file_location('s7_installer_global_directory',tools/'game-directory/interface_patch.py')
        directory=importlib.util.module_from_spec(directory_spec);directory_spec.loader.exec_module(directory)
        target.write_bytes(directory.build(target.read_bytes()))
        record_archive()
    if content.digest(destination / 'Base/BBA/Interface.bba') != manifest['patched_archive_sha256']:
        raise ValueError('Der vorbereitete Menuepatch stimmt nicht mit dem Paket ueberein.')
    def copy_payload(source,target):
        source,target=map(content.plain,(source,target))
        shutil.copyfile(source,target)
        expected=manifest['files'][Path(source).relative_to(package).as_posix().lower()]
        if content.digest(target)!=expected:raise ValueError('Paketkopie stimmt nicht ueberein.')
        if tracker:tracker(Path(target),expected)
        return target
    copy_payload(package / 'Payload/uplay_r1_loader.dll', destination / 'Base/Bin/uplay_r1_loader.dll')
    shutil.copytree(package / 'Payload/Server', destination / 'Server',copy_function=copy_payload)
    shutil.copytree(package / 'Runtime', destination / 'Runtime',copy_function=copy_payload)
    (destination / 'Launcher').mkdir()
    for original, target in launcher_sources(manifest):
        copy_payload(tools / original, destination / 'Launcher' / target)
    copy_payload(tools / 'LICENSE.txt', destination / 'Launcher/LICENSE.txt')
    bundle = {'format': 's7-local-experimental-v1', 'runtime_verified': False, 'lan_verified': False,
              'files': {name: content.digest(destination / name) for name in
                        ('Base/Bin/Settlers7R.exe', 'Base/Bin/uplay_r1_loader.dll', 'Base/BBA/Interface.bba')}}
    if bundle['files']['Base/Bin/Settlers7R.exe'] != source_exe_hash:
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
    launcher_tools = launcher_sources(manifest)
    for original, target in launcher_tools:
        if content.digest(tools / original) != content.digest(destination / 'Launcher' / target):
            raise ValueError('Starterkopie stimmt nicht ueberein.')
    write_json(destination / 'local-bundle.json', bundle)
    write_json(destination / 'compatibility.json', content.create(destination))
    if tracker:
        for name in ('local-bundle.json','compatibility.json'):tracker(destination/name,content.digest(destination/name))
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
            if manifest.get('windows_helpers',False):
                stream.write('@echo off\n"%~dp0Runtime\\dotnet\\WePlay.WindowsHelper.exe" lan-firewall "%~dp0." apply\npause\n')
            else:stream.write('@echo off\npowershell.exe -NoLogo -NoProfile -File "%~dp0Launcher\\Configure-LAN-Firewall.ps1" -Bundle "%~dp0."\npause\n')
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
