"""Start the installer-owned Gold copy with its verified offline adapter."""
import argparse
import json
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import installer
gold = installer.gold


def run(app, *, check_only=False, on_ready=None):
    app = gold.plain(app)
    installer.verify_package(app)
    installer.verify_game(app)
    if check_only:
        return {'verified': True, 'game_launched': False}
    from bootstrap import launch
    root = app / 'Game'
    logs = gold.plain(root / 'LocalData'); logs.mkdir(exist_ok=True)
    environment = {key: value for key, value in os.environ.items()
                   if not key.upper().startswith(('DXVK_', 'S7_', 'WEPLAY_'))}
    environment.update(DXVK_LOG_PATH=str(logs), DXVK_STATE_CACHE_PATH=str(logs))
    result = launch(root / gold.EXE, root / 'Launcher/weplay_gold_startup.dll',
                    environment=environment, on_ready=on_ready)
    (logs / 'last-launch.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--check-only', action='store_true')
    args = parser.parse_args()
    try:
        result = run(args.app, check_only=args.check_only)
        if sys.stdout: print(json.dumps(result), flush=True)
        return result.get('exit_code', 0)
    except (OSError, ValueError, RuntimeError) as error:
        if sys.stdout: print(str(error), flush=True)
        if sys.platform == 'win32' and sys.stdout is None:
            import ctypes
            ctypes.windll.user32.MessageBoxW(None, str(error), 'WePlay Gold – Start', 0x10)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
