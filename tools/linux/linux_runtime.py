"""Owned Wine/Proton prefixes and native Linux service lifecycle.

Only the original PE game and its native adapter run through the compatibility
layer. Python, the configuration service and launcher remain Linux processes.
"""
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid

_tools = Path(__file__).resolve().parent.parent
if (_tools / 'installer').is_dir():
    sys.path[:0] = [str(_tools / 'installer'), str(_tools / 'offline-ui')]
import compatibility
import host_runtime
import launch_local
import portable_lock
from lan_child_job import ServiceJob

FORMAT = 'weplay-linux-runtime-v1'


def require_linux_filesystem(path):
    if sys.platform != 'linux': return
    result = subprocess.run(['/usr/bin/stat', '-f', '--format=%T', '--', str(path)],
                            capture_output=True, text=True, timeout=10)
    if result.returncode or len(result.stdout) > 256:
        raise ValueError('Cannot determine the game filesystem')
    if result.stdout.strip() in ('9p', 'drvfs'):
        raise ValueError('Select a Linux filesystem for the game copy. WSL Windows mounts cannot reliably execute its PE code.')


def clean_environment(inherited=None):
    environment = dict(os.environ if inherited is None else inherited)
    for name in list(environment):
        if name.upper().startswith(('WINE', 'PROTON', 'STEAM_COMPAT', 'DOTNET_', 'CORECLR_', 'COMPLUS_', 'COR_')):
            del environment[name]
    environment['PYTHONDONTWRITEBYTECODE'] = '1'
    return environment


def settings(root, *, check_runner=True):
    path = compatibility.plain(Path(root) / 'LocalData/linux-runtime.json')
    default = '/opt/wine-stable/bin/wine' if Path('/opt/wine-stable/bin/wine').is_file() else '/usr/bin/wine'
    value = {'format': FORMAT, 'runner': 'wine', 'executable': default, 'steam': None}
    if path.exists():
        if not path.is_file() or path.stat().st_size > 4096:
            raise ValueError('Invalid Linux runtime settings')
        value = json.loads(path.read_text(encoding='utf-8'))
    if (not isinstance(value, dict) or set(value) != {'format', 'runner', 'executable', 'steam'}
            or value['format'] != FORMAT or value['runner'] not in ('wine', 'proton')
            or not isinstance(value['executable'], str) or not Path(value['executable']).is_absolute()
            or (value['steam'] is not None and (not isinstance(value['steam'], str) or not Path(value['steam']).is_absolute()))):
        raise ValueError('Invalid Linux runtime settings')
    if not check_runner:
        return value
    check_program(value)
    return value


def check_program(value):
    program = Path(value['executable']).resolve(strict=True)
    if not program.is_file() or not os.access(program, os.X_OK):
        raise ValueError('Install Wine with 32-bit support, or select an installed Proton runner')
    if value['runner'] == 'proton' and (value['steam'] is None or not Path(value['steam']).is_dir()):
        raise ValueError('Proton requires the selected Linux Steam client directory')
    if value['runner'] == 'wine':
        result = subprocess.run([str(program),'--version'],capture_output=True,timeout=10,env=clean_environment())
        version = re.match(r'wine-(\d+)\.',result.stdout.decode('utf-8',errors='replace').strip())
        if result.returncode or not version or int(version[1]) < 11:
            raise ValueError('Wine 11 or newer is required. Select an installed WineHQ runtime or Proton.')


def configure(root, *, runner='wine', executable='/usr/bin/wine', steam=None):
    root = compatibility.plain(root)
    data = compatibility.plain(root / 'LocalData'); data.mkdir(mode=0o700, exist_ok=True)
    value = dict(format=FORMAT, runner=runner, executable=str(executable), steam=None if steam is None else str(steam))
    path = data / 'linux-runtime.json'
    if path.exists():
        settings(root,check_runner=False)  # Unknown schemas are never overwritten; missing runners can be repaired.
    temporary = data / ('linux-' + uuid.uuid4().hex + '.tmp')
    try:
        with temporary.open('x', encoding='utf-8') as stream:
            json.dump(value, stream); stream.write('\n')
        # Validate the same schema before publishing, including executable access.
        if runner not in ('wine', 'proton') or not Path(executable).is_absolute() or not os.access(executable, os.X_OK):
            raise ValueError('Invalid or unavailable Linux runner')
        if runner == 'proton' and (steam is None or not Path(steam).is_absolute() or not Path(steam).is_dir()):
            raise ValueError('Proton requires a Linux Steam installation')
        check_program(value)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)
    return value


def graphics_environment(root):
    import graphics_profile
    environment = clean_environment()
    if (Path(root) / graphics_profile.STATE).exists():
        return graphics_profile.launch_environment(root, environment)
    if (Path(root) / 'Base/Bin/d3d9.dll').exists():
        raise ValueError('Unknown renderer DLL; repair the copy before starting')
    for key in list(environment):
        if key.startswith('DXVK_'):
            del environment[key]
    return environment


def initial_documents(root, home, runner):
    documents = compatibility.plain(home / 'Documents')
    documents.mkdir(mode=0o700, exist_ok=True)
    prefix = root / ('WinePrefix' if runner == 'wine' else 'ProtonData/pfx')
    if prefix.exists(): return
    game = compatibility.plain(documents / 'THE SETTLERS 7 - History Edition')
    game.mkdir(mode=0o700, exist_ok=True)
    try:
        # The WSLg qualification uses this bounded initial window. Preserve
        # imported settings byte-for-byte; never read or replace them here.
        with compatibility.plain(game / 'Options.ini').open('xb') as output:
            output.write(b'[Quality]\nFullScreen = 0\nScreenWidth = 1280\nScreenHeight = 720\n')
    except FileExistsError:
        pass


def game_command(root, arguments=(), inherited=None):
    root = compatibility.plain(root)
    require_linux_filesystem(root)
    value = settings(root)
    import linux_profiles
    linux_profiles.relocate(root)
    environment = clean_environment(inherited)
    data = compatibility.plain(root / 'LocalData'); data.mkdir(mode=0o700, exist_ok=True)
    home = compatibility.plain(data / 'WineHome'); home.mkdir(mode=0o700, exist_ok=True)
    initial_documents(root, home, value['runner'])
    # Wine's default Documents links must point at this private home, never the
    # invoking user's existing documents/profile directory.
    environment.update(HOME=str(home), XDG_CONFIG_HOME=str(home / '.config'),
                       XDG_DATA_HOME=str(home / '.local/share'), XDG_CACHE_HOME=str(home / '.cache'),
                       WINEDEBUG='-all,err+all', WINEDLLOVERRIDES='uplay_r1_loader=n,b;mscoree,mshtml=')
    if (root / 'Base/Bin/d3d9.dll').exists():
        environment['WINEDLLOVERRIDES'] += ';d3d9=n,b'
    exe = root / 'Base/Bin/Settlers7R.exe'
    if value['runner'] == 'wine':
        prefix = compatibility.plain(root / 'WinePrefix')
        environment['WINEPREFIX'] = str(prefix)
        if not prefix.exists():
            # The qualified WineHQ path includes its i386 runtime. Keep this
            # 32-bit game in the same prefix architecture as the ABI fixtures.
            environment['WINEARCH'] = 'win32'
        return [value['executable'], str(exe), *arguments], environment
    environment.update(STEAM_COMPAT_DATA_PATH=str(compatibility.plain(root / 'ProtonData')),
                       STEAM_COMPAT_CLIENT_INSTALL_PATH=value['steam'], SteamAppId='965320', SteamGameId='965320')
    return [value['executable'], 'waitforexitandrun', str(exe), *arguments], environment


def run_local(root, *, check_only=False, before_game=None, monitor=None):
    if not check_only and os.geteuid() == 0:
        raise ValueError('Start WePlay as your normal user, without sudo')
    root = compatibility.plain(root)
    require_linux_filesystem(root)
    if (root / 'INSTALLATION-INCOMPLETE.txt').exists():
        raise ValueError('Installation incomplete; repair before starting')
    data = compatibility.plain(root / 'LocalData'); data.mkdir(mode=0o700, exist_ok=True)
    with compatibility.plain(data / 'session.lock').open('a+b') as lock:
        if lock.tell() == 0:
            lock.write(b'0'); lock.flush()
        lock.seek(0)
        try:
            portable_lock.locking(lock.fileno(), portable_lock.LK_NBLCK, 1)
        except OSError:
            raise RuntimeError('This game copy is already running') from None
        service = runtime = job = None
        try:
            print('Checking complete game and native tool inventory ...', flush=True)
            compatibility.verify(root)
            launch_local.verify_bundle(root)
            if not check_only:
                environment = launch_local.game_launch_environment(root, graphics_environment(root))
                command, environment = game_command(root, ('-vSynch', '-noMapInMainMenu'), environment)
            run = time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8]
            with (data / (run + '-server.jsonl')).open('xb') as output, (data / (run + '-server-errors.txt')).open('xb') as errors:
                job = ServiceJob()
                service = job.start([*host_runtime.server_command(root), '--local-config-server',
                    '--local-keepalive-probe', '--http-port', '0', '--udp-port', '0', '--seconds', '3600'],
                    stdout=output, stderr=errors, env=clean_environment())
                configuration, port = launch_local.service_ready(service, data / (run + '-server.jsonl'))
                runtime = launch_local.write_configuration(data, configuration, port, True)
                if service.poll() is not None:
                    raise RuntimeError('Local service exited before game startup')
                if check_only:
                    print('PASS: full game inventory, adapter and native Linux service; no game started.', flush=True)
                    return 0
                if before_game:
                    before_game()
                with (data / (run + '-wine.txt')).open('xb') as wine_log:
                    game = subprocess.Popen(command, cwd=root / 'Base/Bin', env=environment,
                                            stdout=wine_log, stderr=wine_log)
                    code = launch_local.wait_for_graphics_game(root, game, enabled=False, monitor=monitor)
                (data / (run + '-exit.txt')).write_text(str(code) + '\n', encoding='ascii')
                if code:
                    print('Game exited with status ' + str(code) + '. Wine errors: ' +
                          str(data / (run + '-wine.txt')), flush=True)
                return 0 if code == 0 else 1
        finally:
            if runtime is not None:
                compatibility.plain(runtime).unlink(missing_ok=True)
            try:
                if job is not None: job.close()
            finally:
                portable_lock.locking(lock.fileno(), portable_lock.LK_UNLCK, 1)
