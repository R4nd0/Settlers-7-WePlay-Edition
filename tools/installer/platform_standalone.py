"""Fingerprint-bound, platform-independent Windows runtime and portable profile."""
import json
from pathlib import Path
import shutil
import subprocess
import os

import compatibility as content

FORMAT = 'weplay-platform-independent-v1'
RECORD = 'Launcher/platform-standalone.json'
TOOLS = ('platform_standalone.py', 'isolated_process.py', 'standalone_entry.py','network_elevation.py','network_peer_policy.py',
         'crossplay_session.py', 'crossplay_content.py', 'crossplay_broker.py', 'crossplay_runtime.py',
         'crossplay_room.py', 'crossplay_room_broker.py',
         'crossplay_invitation.py', 'crossplay_host.py', 'crossplay_network.py', 'history_session.py', 'internet_room.py', 'lobby_directory.py')
GOLD_SERVICE_TOOLS = ('gold_session.py', 'login_server.py', 'account_config.py', 'account_profile.py',
                      'local_gatherings.py', 'gathering_codec.py', 'nat_traversal.py', 'easytier_gateway.py')
HISTORY = '3e400b5d108ec99ca05105fb7d9149e2a4978435691ba2d9143fc68078f55aac'
HISTORY_RC405 = '847378000abeabd83b1d7a459db19e54b734c84b5694a9b3b797704ef4225017'
LOD_HISTORY = '1fbe68497696309900ee6306eb2cfd6cb0cbaf036b9a30ec322f7efb4e749ace'
HISTORY_RC409 = '6fd41b706088112672d7a8c196f20ddbf45945ec25a4c0acf26d133c692b3be9'
HISTORY_LOD400 = '58937e7c61ab38045352908cb857ce9d76acb3d2031881f8130c6312276cd02e'
HISTORY_PINS = {(False,False):HISTORY,
                (True,False):'6386ddfbb3e91fccd19d858b39c21fb31dee9bb5a7dfb07efc1fcd9ca35c16b4',
                (False,True):'393b1cb398b09c47aa93839b512222afdb7375dbe31e373fd6dad2c91ccf895a',
                (True,True):HISTORY_LOD400}
GOLD_RC405 = '72fa857ebcef73b35f611873c6daf1535dadfe24034e05b4ac3279500023b989'
LOD_GOLD = '92bd952f0efde03ae2e1357c019102854fb14df006b2fd7f0a91a11997a66dcd'
GOLD_RC409 = '49a8db97405f805ac02e57b98df9e6dea5603b292e5ddf23869249c6c6424798'
GOLD_LOD400 = '0b5e2d37040bd707ff44d954bc7644fb6115640852424108a8321d4e126a357e'
GOLD = GOLD_LOD400
GOLD_RC403 = 'a5e511148b04f99d05b99ea1605f435c805988ab1f2a1fc23d250769ae9d3453'
# Known previous release adapters remain readable for safe upgrade/repair.
# New preparation still requires GOLD/HISTORY_PINS, never this legacy set.
LEGACY_HISTORY_PINS = (HISTORY_RC409,LOD_HISTORY,HISTORY_RC405,'f4f2db08e92cd0229c2ac749a3cede76724955277c0c3ba68b5fdc8e76e33112',
                       '6193d8af149182a4d54909ac514acdc4bc47c85e054dd557ee4699d88363ab14',
                       '597803dccfc37885a7d60f0530f36130cfcb0c3adeef259c22aaa5ff853b0ed4')
LEGACY_GOLD_PINS = ('fcd996c475585a3cae61180d883a5405d4b519414da6f17ea7a41b51da120ba2',
                    'a6cdbd62c35eac039cf692de70b31fa9efed981ca5e0f1faa44f7fd5dcc18148',
                    '6b3b597138f223275c2dd3e02857b77e4e86ac653ab92b7318f2d03c0fa7e405',
                    '537385bff398cd9251e74ef769caf70aa6f86afcecab0fb52542d3149ac7a430')
STEAM_API = '2e9836aeb19373dc1cc0743a9d8bdd5f322fbb426d077ea2718005c5a4bef140'
MILES_GATE = 'ac50ee39af6ce5b1932aee08df80461dc281255b7b29d9bb82dc76d8ab9d4a63'
MILES_ORIGINAL = '301f351311b96964ca0ec21df68cec38ccaa9c6ffc082987ca24ce35ca39f5bf'
STEAM_ORIGINAL = 'd99d425793f588fbc15f91c7765977cdd642b477be01dac41c0388ab6a5d492d'
POLICY = '3317aec4210f8214cb6fbe586502a4e26c23fb68182d8b00a1b0353848e49b41'
PAYLOAD = {'steam_api.dll': STEAM_API, 'mss32.dll': MILES_GATE, 'weplay_network_policy.exe': POLICY}


def tool_sources():
    return tuple(('installer/'+name,name) for name in TOOLS) + tuple(('gold/'+name,name) for name in GOLD_SERVICE_TOOLS)


def adapter_pins(*, gold=False):
    current = (GOLD,GOLD_RC409,LOD_GOLD,GOLD_RC405,GOLD_RC403) if gold else tuple(HISTORY_PINS.values())
    legacy = LEGACY_GOLD_PINS if gold else LEGACY_HISTORY_PINS
    return tuple(dict.fromkeys((*current, *legacy)))


def enabled(root):
    root = content.plain(root)
    if content.plain(root/RECORD).exists():
        return True
    # A missing descriptor must never redirect an installed release back into
    # shared Documents before its guarded DLL refuses the game start.
    for adapter in (root/'Base/Bin/uplay_r1_loader.dll', root/'Launcher/weplay_gold_startup.dll'):
        if adapter.is_file() and content.digest(content.plain(adapter)) in (*adapter_pins(), *adapter_pins(gold=True)):
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
        raise ValueError('destination_too_long')


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
    if expected_adapter not in adapter_pins(gold=gold):
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


def launch(root, *, environment=None, arguments=(),lan=None,on_initialized=None,graphics_options=None,
           crossplay=None,crossplay_catalog=None,before_spawn=None,renderer=None):
    root = content.plain(root)
    value, executable, adapter = load(root)
    environment = launch_environment(value, executable, os.environ if environment is None else environment,graphics_options=graphics_options)
    if crossplay is not None:
        import crossplay_session
        if type(crossplay) not in (crossplay_session.Binding, crossplay_session.RoomBinding) or lan is not None:
            raise ValueError('crossplay_configuration_invalid')
        if value['files'][adapter.relative_to(root).as_posix()] not in crossplay_session.ADAPTER_PINS.get(crossplay.edition, ()):
            raise ValueError('crossplay_adapter_unavailable')
        import crossplay_content
        if type(crossplay_catalog) is not crossplay_content.Catalog:
            raise ValueError('crossplay_catalog_invalid')
        if crossplay.content != crossplay_catalog.verify(root, crossplay.edition):
            raise ValueError('crossplay_content_changed')
        # A mixed game keeps its ordinary loopback-only WFP policy. Remote
        # transport terminates in the broker, never in the game executable.
        environment = crossplay.environment(root, executable, environment)
    elif crossplay_catalog is not None:
        raise ValueError('crossplay_configuration_invalid')
    helper=root/'Launcher/weplay_network_policy.exe'
    result=subprocess.run([str(helper),'check',str(executable)],capture_output=True,timeout=15)
    if result.returncode and lan is None:
        print('Windows-Netzisolation für diese Kopie wird eingerichtet ...',flush=True)
        configure(root,'install')
    if before_spawn is not None:
        before_spawn()
    import isolated_process
    scope=restore_renderer=None
    try:
        if lan is not None:
            import network_peer_policy
            scope=network_peer_policy.Session(root,lan)
        if renderer is not None:
            import graphics_profile as graphics
            mode,package=renderer
            if mode not in (*graphics.ASYNC_RENDERERS,'native'):
                raise ValueError('renderer_payload_invalid')
            if value['edition']=='gold' and value['files'][adapter.relative_to(root).as_posix()] not in (GOLD,GOLD_RC409,LOD_GOLD,GOLD_RC405):
                raise ValueError('update_required')
            graphics.apply_launch_renderer(root,executable,package,mode)
            restore_renderer=lambda:graphics.recover_launch_renderer(root,executable,package)
            if mode=='native':
                environment={key: setting for key, setting in environment.items() if not key.upper().startswith('DXVK_')}
            else:
                environment['DXVK_CONFIG_FILE']=str(executable.with_name('dxvk.conf'))
                environment['DXVK_CONFIG']=graphics.SHADER_COMPATIBILITY+'; d3d9.samplerAnisotropy = -1'
            environment[graphics.LAUNCH_RENDERER]=mode
        game=isolated_process.launch(executable, adapter,
            dll_sha256=value['files'][adapter.relative_to(root).as_posix()],
            policy=root/'Launcher/weplay_network_policy.exe', policy_sha256=POLICY,
            environment=environment, arguments=arguments,on_initialized=on_initialized)
        game.network_policy=scope
        game.restore_renderer=restore_renderer
        return game
    except BaseException:
        try:
            if restore_renderer:restore_renderer()
        finally:
            if scope:scope.close()
        raise


def launch_environment(value, executable, inherited, *, graphics_options=None):
    import crossplay_session
    environment = {key: setting for key, setting in crossplay_session.clean_environment(inherited).items()
                   if key.upper() != 'WEPLAY_D3D9_RENDERER'}
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
