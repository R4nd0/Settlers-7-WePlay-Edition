"""Gold-only offline installation and integrity contract. No History adapter."""
import argparse
import importlib.util
import json
from pathlib import Path, PurePosixPath
import shutil
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parent))
spec = importlib.util.spec_from_file_location('gold_installer_prepare', Path(__file__).with_name('prepare.py'))
gold = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gold)

ADAPTER_SHA256 = '5ecdcf1f3c20ddf2088f342606bf0de77594e04820810e95ba15e72cabd2e840'
EXE_SHA256 = '76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab'
PYTHON_SHA256 = '90905e64a840557d8c1bc4f5b74db3d7ee85e3ffb217611cac1c7813acea4159'
FORMAT = 'weplay-gold-package-v1'
INSTALLED = 'weplay-gold-install-v1'


def inventory(root, folders):
    result = {}
    for folder in folders:
        start = gold.plain(root / folder)
        if not start.is_dir():
            raise ValueError('Gold-Paket unvollständig: ' + folder)
        for path in start.rglob('*'):
            path = gold.plain(path)
            if path.is_file():
                name = path.relative_to(root).as_posix()
                if name.casefold() in {key.casefold() for key in result}:
                    raise ValueError('Mehrdeutiger Gold-Dateiname.')
                result[name] = gold.digest(path)
    return result


def read_json(path):
    path = gold.plain(path)
    if path.stat().st_size > 4 * 1024 * 1024:
        raise ValueError('Gold-Manifest zu groß.')
    return json.loads(path.read_text(encoding='utf8'))


def verified_files(root, files, folders):
    if not isinstance(files, dict) or not 1 <= len(files) <= 20000:
        raise ValueError('Ungültiges Gold-Dateimanifest.')
    for name, digest in files.items():
        if not isinstance(name, str):
            raise ValueError('Ungültiger Gold-Manifestpfad.')
        parts = PurePosixPath(name).parts
        if (not parts or parts[0] not in folders or '..' in parts or '\\' in name
                or ':' in name or name.startswith('/') or not isinstance(digest, str)
                or len(digest) != 64 or any(c not in '0123456789abcdef' for c in digest)):
            raise ValueError('Ungültiger Gold-Manifestpfad oder Fingerabdruck.')
        if gold.digest(root / name) != digest:
            raise ValueError('Gold-Datei fehlt oder wurde verändert: ' + name)


def verify_package(app):
    app = gold.plain(app)
    value = read_json(app / 'package.json')
    if (value.get('format') != FORMAT or value.get('menu_sha256') != gold.MENU_SHA256
            or type(value.get('platform_independent',False)) is not bool
            or value.get('multiplayer') is not False):
        raise ValueError('Unbekanntes Gold-Installationspaket.')
    verified_files(app, value.get('files'), ('Tools', 'Runtime', 'Payload', 'Definitions', 'Licenses'))
    if inventory(app, ('Tools', 'Runtime', 'Payload', 'Definitions', 'Licenses')) != value['files']:
        raise ValueError('Gold-Paketinventar verändert.')
    adapter=ADAPTER_SHA256
    if value.get('platform_independent',False):
        import platform_standalone
        adapter=platform_standalone.GOLD
        platform_standalone.payload(app/'Payload/Platform')
        if any(('Tools/gold/'+name) not in value['files'] for name in (*platform_standalone.TOOLS,'compatibility.py')):
            raise ValueError('Die Gold-Standalone-Werkzeuge fehlen.')
    for name, expected in (
        ('Payload/weplay_gold_startup.dll', adapter),
        ('Payload/d3d9.dll', gold.DXVK_SHA256), ('Definitions/Definitions.bin', gold.DEFINITIONS_SHA256)):
        if value['files'].get(name) != expected:
            raise ValueError('Gold-Paket enthält einen ungeprüften Laufzeitstand.')
    return value


def verify_game(app):
    app = gold.plain(app)
    return verify_root(app/'Game')


def verify_root(root):
    root = gold.plain(root)
    value = read_json(root / 'gold-install.json')
    if value.get('format') != INSTALLED or value.get('multiplayer') is not False:
        raise ValueError('Gold-Installation unvollständig.')
    if (root / 'PREPARATION-INCOMPLETE.txt').exists():
        raise ValueError('Gold-Vorbereitung unvollständig.')
    verified_files(root, value.get('files'), ('Data', 'Launcher', 'Backup'))
    adapter=ADAPTER_SHA256
    if value.get('platform_independent',False):
        import platform_standalone
        platform_standalone.load(root)
        adapter=platform_standalone.GOLD
    for name, expected in ((gold.EXE.as_posix(), EXE_SHA256),
                           (gold.INTERFACE.as_posix(), gold.MENU_SHA256),
                           (gold.SHARED.as_posix(), gold.SHARED_SHA256),
                           ('Launcher/weplay_gold_startup.dll', adapter),
                           (gold.EXE.with_name('d3d9.dll').as_posix(), gold.DXVK_SHA256)):
        if value['files'].get(name) != expected:
            raise ValueError('Gold-Installation hat einen ungeprüften Spielstand.')
    return value


def repair_inputs(root):
    root=gold.plain(root)
    report=read_json(root/'gold-development.json')
    files=report.get('source_files')
    if report.get('format')!='s7-gold-development-copy-v1' or not isinstance(files,dict) or not 1<=len(files)<=20000:
        raise ValueError('Gold repair inventory missing.')
    for name,hashed in files.items():
        parts=PurePosixPath(name).parts
        if (not parts or parts[0]!='Data' or '..' in parts or '\\' in name or ':' in name
                or not isinstance(hashed,str) or len(hashed)!=64 or any(c not in '0123456789abcdef' for c in hashed)):
            raise ValueError('Invalid Gold repair inventory.')
        gold.plain(root/name)
    import platform_standalone
    miles=gold.EXE.with_name('mss32.dll').as_posix()
    retained_miles=gold.plain(root/gold.EXE.with_name('weplay_miles_original.dll'))
    # Older Gold installers kept the original DLL at its original name.
    # An existing retained backup always wins and must pass its own hash guard.
    if not retained_miles.exists():retained_miles=gold.plain(root/miles)
    originals={gold.EXE.as_posix():gold.plain(root/'Backup/Settlers7R.original.exe').read_bytes(),
               gold.INTERFACE.as_posix():gold.plain(root/'Backup/Interface.original.bba').read_bytes(),
               miles:retained_miles.read_bytes()}
    shared_backup=gold.plain(root/'Backup/Shr.original.bba')
    if shared_backup.exists():originals[gold.SHARED.as_posix()]=shared_backup.read_bytes()
    else:originals[gold.SHARED.as_posix()]=gold.plain(root/gold.SHARED).read_bytes()
    if gold.sha256(originals[gold.SHARED.as_posix()])!=gold.SHARED_SOURCE_SHA256:
        raise ValueError('Gold retained shared archive changed.')
    for name,expected in ((gold.EXE.as_posix(),gold.GOLD_SHA256),(gold.INTERFACE.as_posix(),gold.INTERFACE_SHA256),(miles,platform_standalone.MILES_ORIGINAL)):
        if gold.sha256(originals[name])!=expected or files.get(name)!=expected:
            raise ValueError('Gold retained original changed.')
    return originals,files


def prepare_copy(app, source, target, *, managed=False, on_created=None, tracker=None, progress=None):
    """Prepare a new main-launcher instance, retaining originals for future repair."""
    app,source,target=map(gold.plain,(app,source,target))
    package=verify_package(app)
    if package.get('platform_independent') is not True: raise ValueError('platform_isolation_required')
    from bootstrap import ensure_closed
    ensure_closed(app)
    import platform_standalone
    platform_standalone.validate_location(target,gold=True)
    options={}
    if managed:
        originals,files=repair_inputs(source)
        options=dict(originals=originals,source_files=files)
    report=gold.prepare(source,target,definitions=app/'Definitions/Definitions.bin',dxvk=app/'Payload/d3d9.dll',
                        on_created=on_created,tracker=tracker,progress=progress,**options)
    (target/'Launcher').mkdir()
    shutil.copyfile(app/'Payload/weplay_gold_startup.dll',target/'Launcher/weplay_gold_startup.dll')
    platform_standalone.prepare(target,app/'Payload/Platform',gold=True,tracker=tracker)
    report.update(starter_created=True,shared_profile_isolated=True,offline_startup_adapter=True)
    (target/'gold-development.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    (target/'README.txt').write_text('WePlay Gold – Einzelspieler\nStart über den gemeinsamen WePlay-Starter.\n'
        'Spielstände und Einstellungen: LocalData/ProfileRoot/Profiles.\n'
        'Steam und Ubisoft Connect werden nicht benötigt.\n',encoding='utf-8')
    value={'format':INSTALLED,'files':inventory(target,('Data','Launcher','Backup')),'multiplayer':False,
           'platform_independent':True,'quiet_interface':True,'all_campaign_maps_selectable':True,
           'profile_isolation':'portable','source_files_verified':len(report['source_files'])}
    (target/'gold-install.json').write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
    if tracker:
        for name,hashed in {**value['files'],**{n:gold.digest(target/n) for n in ('gold-install.json','gold-development.json','README.txt')}}.items():
            tracker(target/name,hashed)
    ensure_closed(app)
    return verify_root(target)


def install(app, source, *, emit=print):
    app = gold.plain(app)
    package=verify_package(app)
    # Never overwrite a loaded game/adapter or reuse an existing game directory.
    from bootstrap import ensure_closed
    ensure_closed(app)
    source = gold.source_root(source)
    target = gold.plain(app / 'Game')
    if package.get('platform_independent',False):
        import platform_standalone
        platform_standalone.validate_location(target,gold=True)
    if target.exists():
        raise ValueError('Die Gold-Spielkopie existiert bereits. Einen neuen Zielordner wählen.')
    if app.is_relative_to(source) or source.is_relative_to(app):
        raise ValueError('Gold-Quelle und Installationsziel müssen getrennt sein.')
    gold.inspect(source, app / 'Definitions/Definitions.bin')
    stage = app / ('GoldPreparation-' + uuid.uuid4().hex)
    emit('Gold wird kopiert und das bereinigte Interface vorbereitet ...')
    report = gold.prepare(source, stage, definitions=app / 'Definitions/Definitions.bin', dxvk=app / 'Payload/d3d9.dll')
    launcher = stage / 'Launcher'
    launcher.mkdir()
    shutil.copy2(app / 'Payload/weplay_gold_startup.dll', launcher / 'weplay_gold_startup.dll')
    independent=package.get('platform_independent',False)
    if independent:
        import platform_standalone
        platform_standalone.prepare(stage,app/'Payload/Platform',gold=True)
    report.update(starter_created=True, shared_profile_isolated=True, offline_startup_adapter=True)
    (stage / 'gold-development.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    (stage / 'README.txt').write_text(
        'WePlay Gold – Einzelspieler\n\n'
        'Start über die WePlay-Gold-Verknüpfung im Startmenü.\n'
        'Bereinigtes Interface und vollständige Kampagnenauswahl sind installiert.\n'
        'Original-EXE und Original-Interface liegen unverändert in Backup.\n'
        + ('Spielstände/Einstellungen: innerhalb der Kopie unter LocalData/ProfileRoot/Profiles.\n'
           'Steam und Ubisoft Connect werden nicht benötigt; Windows-Netzisolation beim ersten Start bestätigen.\n'
           if independent else 'Spielstände: isoliertes Profil unter Documents/WePlay/Gold.\n'), encoding='utf8')
    files = inventory(stage, ('Data', 'Launcher', 'Backup'))
    value = {'format': INSTALLED, 'files': files, 'multiplayer': False,'platform_independent':independent,
             'quiet_interface': True, 'all_campaign_maps_selectable': True,
             'profile_isolation': 'portable' if independent else 'native-path-derived', 'source_files_verified': len(report['source_files'])}
    (stage / 'gold-install.json').write_text(json.dumps(value, indent=2) + '\n', encoding='utf8')
    ensure_closed(app)
    stage.rename(target)
    verify_game(app)
    emit('Gold bereit: bereinigtes Interface und vollständige Kampagnenauswahl.')
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True, type=Path)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--quiet', action='store_true')
    args = parser.parse_args()
    try:
        install(args.app, args.source)
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        if sys.stdout: print(str(error), flush=True)
        if sys.platform == 'win32' and not args.quiet and sys.stdout is None:
            import ctypes
            ctypes.windll.user32.MessageBoxW(None, str(error), 'WePlay Gold – Installation', 0x10)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
