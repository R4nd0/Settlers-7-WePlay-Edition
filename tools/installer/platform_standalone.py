"""Fingerprint-bound, platform-independent Windows runtime and portable profile."""
import json
from pathlib import Path
import shutil
import subprocess
import os

import compatibility as content

FORMAT = 'weplay-platform-independent-v1'
RECORD = 'Launcher/platform-standalone.json'
TOOLS = ('platform_standalone.py', 'isolated_process.py', 'standalone_entry.py','network_elevation.py','network_peer_policy.py')
HISTORY = '3e400b5d108ec99ca05105fb7d9149e2a4978435691ba2d9143fc68078f55aac'
HISTORY_PINS = {(False,False):HISTORY,
                (True,False):'6386ddfbb3e91fccd19d858b39c21fb31dee9bb5a7dfb07efc1fcd9ca35c16b4',
                (False,True):'393b1cb398b09c47aa93839b512222afdb7375dbe31e373fd6dad2c91ccf895a',
                (True,True):'f4f2db08e92cd0229c2ac749a3cede76724955277c0c3ba68b5fdc8e76e33112'}
GOLD = 'fcd996c475585a3cae61180d883a5405d4b519414da6f17ea7a41b51da120ba2'
STEAM_API = '2e9836aeb19373dc1cc0743a9d8bdd5f322fbb426d077ea2718005c5a4bef140'
MILES_GATE = 'ac50ee39af6ce5b1932aee08df80461dc281255b7b29d9bb82dc76d8ab9d4a63'
MILES_ORIGINAL = '301f351311b96964ca0ec21df68cec38ccaa9c6ffc082987ca24ce35ca39f5bf'
STEAM_ORIGINAL = 'd99d425793f588fbc15f91c7765977cdd642b477be01dac41c0388ab6a5d492d'
POLICY = '3317aec4210f8214cb6fbe586502a4e26c23fb68182d8b00a1b0353848e49b41'
PAYLOAD = {'steam_api.dll': STEAM_API, 'mss32.dll': MILES_GATE, 'weplay_network_policy.exe': POLICY}


def enabled(root):
    root = content.plain(root)
    if content.plain(root/RECORD).exists():
        return True
    # A missing descriptor must never redirect an installed release back into
    # shared Documents before its guarded DLL refuses the game start.
    for adapter in (root/'Base/Bin/uplay_r1_loader.dll', root/'Launcher/weplay_gold_startup.dll'):
        if adapter.is_file() and content.digest(content.plain(adapter)) in (*HISTORY_PINS.values(), GOLD):
            raise ValueError('Das Standalone-Manifest fehlt. Die Kopie muss repariert werden.')
    return False


def layout(root, *, gold=False):
    root = content.plain(root)
    executable = root/('Data/Base/_Dbg/Bin/Release/Settlers7R.exe' if gold else 'Base/Bin/Settlers7R.exe')
    adapter = root/('Launcher/weplay_gold_startup.dll' if gold else 'Base/Bin/uplay_r1_loader.dll')
    return executable, adapter


def validate_location(root,*,gold=False):
    profile=Path(root)/'LocalData/ProfileRoot'/('Profiles' if gold else 'THE SETTLERS 7 - History Edition')
    if len(str(profile).encode('utf-16le'))//2+64>=260:
        raise ValueError('Der Zielpfad ist für die Spielstände zu lang. Einen kürzeren lokalen Zielordner wählen.')


def marker(executable):
    return ('WePlay network scope v1\n'+content.digest(executable)+'\n').encode('ascii')


def payload(directory):
    directory = content.plain(directory)
    for name, expected in PAYLOAD.items():
        if content.digest(content.plain(directory/name)) != expected:
            raise ValueError('Ungeprüfte Standalone-Laufzeit: '+name)
    return directory


def load(root):
    root = content.plain(root)
    path = content.plain(root/RECORD)
    if path.stat().st_size > 16384:
        raise ValueError('Standalone-Manifest ist zu groß.')
    value = json.loads(path.read_text(encoding='utf-8'))
    if (not isinstance(value, dict) or set(value) != {'format', 'edition', 'files'}
            or value['format'] != FORMAT or value['edition'] not in ('history', 'gold')
            or not isinstance(value['files'], dict)):
        raise ValueError('Unbekanntes Standalone-Manifest.')
    gold = value['edition'] == 'gold'
    executable, adapter = layout(root, gold=gold)
    import isolated_process
    expected_exe = content.digest(executable)
    if expected_exe not in isolated_process.PROFILES or (isolated_process.PROFILES[expected_exe][0] == 3) != gold:
        raise ValueError('Unbekannte Standalone-Spielversion.')
    adapter_name=adapter.relative_to(root).as_posix()
    expected_adapter=value['files'].get(adapter_name)
    if expected_adapter not in ((GOLD,) if gold else tuple(HISTORY_PINS.values())):
        raise ValueError('Ungeprüftes Standalone-Adapterprofil.')
    files = {adapter_name: expected_adapter,
             (executable.parent/'mss32.dll').relative_to(root).as_posix(): MILES_GATE,
             (executable.parent/'weplay_miles_original.dll').relative_to(root).as_posix(): MILES_ORIGINAL,
             'Launcher/weplay_network_policy.exe': POLICY}
    if isolated_process.PROFILES[expected_exe][0] == 2:
        files[(executable.parent/'steam_api.dll').relative_to(root).as_posix()] = STEAM_API
    if value['files'] != files:
        raise ValueError('Die Standalone-Laufzeit ist unvollständig oder ungeprüft.')
    for name, expected in files.items():
        if content.digest(content.plain(root/name)) != expected:
            raise ValueError('Die Standalone-Datei wurde verändert: '+name)
    if content.plain(executable.with_name('.weplay-isolation')).read_bytes() != marker(executable):
        raise ValueError('Die Windows-Netzisolation gehört nicht zu dieser Kopie.')
    return value, executable, adapter


def prepare(root, runtime, *, gold=False, tracker=None):
    """Install only in the installer's freshly created, closed separate copy."""
    root, runtime = content.plain(root), payload(runtime)
    validate_location(root,gold=gold)
    executable, adapter = layout(root, gold=gold)
    import isolated_process
    profile = isolated_process.PROFILES.get(content.digest(executable))
    adapter_digest=content.digest(adapter)
    if not profile or (profile[0] == 3) != gold or adapter_digest not in ((GOLD,) if gold else tuple(HISTORY_PINS.values())):
        raise ValueError('Ungeprüfter Standalone-Adapter oder Spielstand.')
    miles = content.plain(executable.with_name('mss32.dll'))
    retained = content.plain(executable.with_name('weplay_miles_original.dll'))
    if content.digest(miles) != MILES_ORIGINAL or retained.exists() or (root/RECORD).exists():
        raise ValueError('Die separate Kopie hat keine unveränderte Miles-Bibliothek.')
    if profile[0] == 2 and content.digest(executable.with_name('steam_api.dll')) != STEAM_ORIGINAL:
        raise ValueError('Die originale Steam-SDK-Bibliothek stimmt nicht überein.')

    def copy(source, target):
        source, target = map(content.plain, (source, target))
        shutil.copyfile(source, target)
        if content.digest(source) != content.digest(target):
            raise ValueError('Standalonedatei hat sich beim Kopieren verändert.')
        if tracker: tracker(target, content.digest(target))

    copy(miles, retained)
    copy(runtime/'mss32.dll', miles)
    if profile[0] == 2:
        copy(runtime/'steam_api.dll', executable.with_name('steam_api.dll'))
    copy(runtime/'weplay_network_policy.exe', root/'Launcher/weplay_network_policy.exe')
    scope = executable.with_name('.weplay-isolation')
    scope.write_bytes(marker(executable))
    if tracker: tracker(scope, content.digest(scope))
    files = {adapter.relative_to(root).as_posix(): adapter_digest,
             miles.relative_to(root).as_posix(): MILES_GATE,
             retained.relative_to(root).as_posix(): MILES_ORIGINAL,
             'Launcher/weplay_network_policy.exe': POLICY}
    if profile[0] == 2:
        files[(executable.parent/'steam_api.dll').relative_to(root).as_posix()] = STEAM_API
    record = root/RECORD
    record.write_text(json.dumps({'format': FORMAT, 'edition': 'gold' if gold else 'history', 'files': files}, indent=2)+'\n', encoding='utf-8')
    if tracker: tracker(record, content.digest(record))
    for folder in (root/'LocalData', root/'LocalData/ProfileRoot', options_path(root).parent):
        content.plain(folder).mkdir(exist_ok=True)
    load(root)


def options_path(root):
    value = load(root)[0]
    suffix = 'Profiles' if value['edition'] == 'gold' else 'THE SETTLERS 7 - History Edition'
    return content.plain(Path(root)/'LocalData/ProfileRoot'/suffix/'Options.ini')


def launch(root, *, environment=None, arguments=(),lan=None,on_initialized=None,graphics_options=None):
    root = content.plain(root)
    value, executable, adapter = load(root)
    environment = launch_environment(value, executable, os.environ if environment is None else environment,graphics_options=graphics_options)
    helper=root/'Launcher/weplay_network_policy.exe'
    result=subprocess.run([str(helper),'check',str(executable)],capture_output=True,timeout=15)
    if result.returncode and lan is None:
        print('Windows-Netzisolation für diese Kopie wird eingerichtet ...',flush=True)
        configure(root,'install')
    import isolated_process
    scope=None
    try:
        if lan is not None:
            import network_peer_policy
            scope=network_peer_policy.Session(root,lan)
        game=isolated_process.launch(executable, adapter,
            dll_sha256=value['files'][adapter.relative_to(root).as_posix()],
            policy=root/'Launcher/weplay_network_policy.exe', policy_sha256=POLICY,
            environment=environment, arguments=arguments,on_initialized=on_initialized)
        game.network_policy=scope
        return game
    except BaseException:
        if scope:scope.close()
        raise


def launch_environment(value, executable, inherited, *, graphics_options=None):
    environment = dict(inherited)
    if value['edition'] == 'gold':
        # Gold changes its working directory before D3D9 initializes. DXVK's
        # relative default then misses the verified configuration beside the
        # executable, including the strict float emulation needed by water.
        environment = {key: setting for key, setting in environment.items()
                       if key.upper() not in ('DXVK_CONFIG', 'DXVK_CONFIG_FILE')}
        environment['DXVK_CONFIG_FILE'] = str(content.plain(executable.with_name('dxvk.conf')))
        if graphics_options is not None:
            import unicodedata
            if (not isinstance(graphics_options,dict) or set(graphics_options)!={'device_name','anisotropic_filtering'}
                    or type(graphics_options['anisotropic_filtering']) is not bool):
                raise ValueError('gold_graphics_invalid')
            device=graphics_options['device_name']
            if device is not None and (not isinstance(device,str) or not device.strip() or device!=device.strip()
                    or len(device.encode('utf-8'))>256 or any(unicodedata.category(c).startswith('C') for c in device)):
                raise ValueError('gold_graphics_invalid')
            environment={k:v for k,v in environment.items() if k.upper()!='DXVK_FILTER_DEVICE_NAME'}
            if device is not None:environment['DXVK_FILTER_DEVICE_NAME']=device
            environment['DXVK_CONFIG']=('d3d9.floatEmulation = Strict; d3d9.samplerAnisotropy = '
                + ('16' if graphics_options['anisotropic_filtering'] else '-1'))
    elif graphics_options is not None:raise ValueError('gold_graphics_invalid')
    return environment


def configure(root, action='install'):
    root = content.plain(root)
    _, executable, _ = load(root)
    if action not in ('install', 'check', 'remove'):
        raise ValueError('Unbekannte Netzisolationsaktion.')
    helper = root/'Launcher/weplay_network_policy.exe'
    result = subprocess.run([str(helper), action, str(executable)], timeout=30,capture_output=True,
                            creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    if result.returncode == 5 and action in ('install','remove'):
        import network_elevation
        network_elevation.run(helper,executable,action)
        if action == 'install':configure(root,'check')
        return
    if result.returncode:
        raise RuntimeError('Windows-Netzisolation konnte nicht '+{'install':'eingerichtet','check':'geprüft','remove':'entfernt'}[action]+' werden.')


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', required=True, type=Path)
    parser.add_argument('action', choices=('install', 'check', 'remove'))
    args = parser.parse_args()
    configure(args.bundle, args.action)
