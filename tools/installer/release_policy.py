"""Monotonic platform-isolation contract for package swaps and game repairs."""
import json
from pathlib import Path
import compatibility as content

CONTRACT = {'revision': 1, 'editions': ['gold', 'history-steam', 'history-ubisoft']}
FLOOR = 'platform-isolation.json'


def valid(value):
    return (isinstance(value, dict) and set(value) == set(CONTRACT)
            and type(value.get('revision')) is int and value == CONTRACT)


def read(path):
    path = content.plain(path)
    if not path.is_file(): return {}
    if path.stat().st_size > 4 * 1024 * 1024: raise ValueError('platform_isolation_required')
    value = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(value, dict): raise ValueError('platform_isolation_required')
    return value


def required(app):
    app = content.plain(app)
    floor = app/'State'/FLOOR
    if floor.exists():
        if not valid(read(floor)): raise ValueError('platform_isolation_required')
        return True
    return (read(app/'desktop-release.json').get('platform_independent', False) is True
            or read(app/'Package/package.json').get('platform_independent', False) is True)


def check_package(app, value):
    if value.get('main_package', False) is True:
        if (value.get('platform_independent') is not True or value.get('gold_standalone') is not True
                or not valid(value.get('platform_isolation'))):
            raise ValueError('platform_isolation_required')
    if required(app) and (value.get('platform_independent') is not True or value.get('main_package') is not True
                         or value.get('gold_standalone') is not True or not valid(value.get('platform_isolation'))):
        raise ValueError('platform_isolation_required')


def check_update(app, value):
    if required(app) and not valid(value.get('platform_isolation')):
        raise ValueError('platform_isolation_required')


def protected_game(root):
    root = content.plain(root)
    if (root/'Launcher/platform-standalone.json').exists(): return True
    for name in ('local-bundle.json', 'gold-install.json'):
        if read(root/name).get('platform_independent', False) is True: return True
    # A missing descriptor or damaged adapter must not make a repair a downgrade.
    if any((root/name).exists() for name in
           ('Base/Bin/.weplay-isolation', 'Data/Base/_Dbg/Bin/Release/.weplay-isolation',
            'Base/Bin/weplay_miles_original.dll','Data/Base/_Dbg/Bin/Release/weplay_miles_original.dll',
            'Launcher/weplay_network_policy.exe')):return True
    import platform_standalone
    for name in ('Base/Bin/uplay_r1_loader.dll','Launcher/weplay_gold_startup.dll'):
        path=content.plain(root/name)
        if path.is_file() and content.digest(path) in (*platform_standalone.HISTORY_PINS.values(),platform_standalone.GOLD):
            return True
    return False


def preserve_game(root, package):
    if protected_game(root) and package.get('platform_independent') is not True:
        raise ValueError('platform_isolation_required')
