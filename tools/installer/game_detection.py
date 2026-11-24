"""Bounded read-only discovery of supported original S7 installations."""
import os
from pathlib import Path

import compatibility as content
import install

NAMES = ('The Settlers 7 - History Edition', 'The Settlers 7 History Edition', 'thesettlers7','S7Gold','Settlers 7')


def registry_locations():
    import winreg
    found = []
    for hive in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
        for view in (winreg.KEY_WOW64_32KEY, winreg.KEY_WOW64_64KEY):
            try:
                with winreg.OpenKey(hive, r'Software\Microsoft\Windows\CurrentVersion\Uninstall', 0,
                                    winreg.KEY_READ | view) as parent:
                    count = min(winreg.QueryInfoKey(parent)[0], 4096)
                    for index in range(count):
                        try:
                            with winreg.OpenKey(parent, winreg.EnumKey(parent, index)) as key:
                                name = winreg.QueryValueEx(key, 'DisplayName')[0]
                                if isinstance(name, str) and ('settlers' in name.casefold() or 'siedler' in name.casefold()) and '7' in name:
                                    location, kind = winreg.QueryValueEx(key, 'InstallLocation')
                                    if isinstance(location, str) and 0 < len(location) < 32768:
                                        if kind == winreg.REG_EXPAND_SZ:
                                            location = winreg.ExpandEnvironmentStrings(location)
                                        location = location.strip()
                                        if len(location) >= 2 and location.startswith('"') and location.endswith('"'):
                                            location = location[1:-1]
                                        path = Path(location)
                                        if path.is_absolute():
                                            found.append(path)
                        except OSError:
                            continue
            except OSError:
                continue
    return found


def candidates():
    """Only public install locations; no account files or recursive disk scans."""
    found = registry_locations() if os.name == 'nt' else []
    found = [path for path in found if not path.drive.startswith('\\\\')]
    roots = []
    for key in ('ProgramFiles', 'ProgramFiles(x86)'):
        if os.environ.get(key):
            base = Path(os.environ[key])
            roots += [base/'Ubisoft/Ubisoft Game Launcher/games', base/'Steam/steamapps/common']
    for letter in 'CDEFGHIJKLMNOPQRSTUVWXYZ':
        root = Path(letter + ':/')
        if os.name=='nt':
            import ctypes
            if ctypes.windll.kernel32.GetDriveTypeW(str(root))!=3:continue
        roots += [root/'Spiele', root/'Games', root/'SteamLibrary/steamapps/common',
                  root/'Ubisoft Games',root/'Ubisoft/Ubisoft Game Launcher/games']
    found += [root/name for root in roots for name in NAMES if (root/name).is_dir()]
    # A Steam installation's public library directory list has no login data.
    # It is intentionally not read in RC2: registry/common locations and the
    # single folder-picker fallback remain the bounded discovery boundary.
    unique = {}
    for path in found:
        unique.setdefault(os.path.normcase(str(path)),path)
        if len(unique) >= 256:
            break
    return list(unique.values())


def detect(locations=None, *, steam_supported=False,gold_package=None):
    good, seen = [], set()
    for location in (candidates() if locations is None else locations):
        try:
            root = content.plain(location)
            key = str(root).casefold()
            if key in seen:
                continue
            seen.add(key)
            if gold_package is not None:
                import gold_support
                if gold_support.is_gold(root):
                    gold_support.inspect(gold_package,root)
                    good.append({'path':str(root),'name':'The Settlers 7 - Gold Edition'})
                    continue
            install.source_base(root,steam_supported=steam_supported)
            good.append({'path':str(root),'name':'The Settlers 7 - History Edition'})
        except (OSError, ValueError):
            continue
    return good
