"""Run compiled public x86 ABI fixtures in a new, private Wine prefix.

No game, account state or Internet endpoints are used. Supply an existing fast
Linux directory below repository work/; timed leases need a local filesystem.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
NAMES = ('public_key_tests', 'cpu_affinity_tests', 'local_platform_tests', 'local_connect_tests',
         'bootstrap_patch_tests', 'steam_profile_tests', 'network_guard_tests',
         'lan_membership_tests', 'castle_profile_tests', 'castle_wire_tests',
         'lobby_team_tests', 'logistics_priority_tests', 'neo_fifo_policy_tests',
         'neo_logistics_state_tests', 'neo_logistics_tests', 'resume_pacing_tests',
         'terrain_angle_tests', 'workshop_angles_tests', 'map_template_tests',
         'game_directory_bridge_tests')


def run(wine, tests, work, *, keep_prefix=False):
    wine, tests, work = map(lambda p: Path(p).absolute(), (wine, tests, work))
    if (not work.is_relative_to(ROOT / 'work') or work == ROOT / 'work' or
            not work.is_dir() or any(p.is_symlink() for p in (work, *work.parents))):
        raise ValueError('Use an existing real directory strictly below repository work/')
    version = subprocess.run([str(wine), '--version'], capture_output=True, timeout=15, check=True)
    match = re.match(rb'wine-(\d+)\.', version.stdout)
    if not match or int(match[1]) < 11:
        raise ValueError('Wine 11 or newer is required')
    for name in NAMES:
        if not (tests / (name + '.exe')).is_file(): raise ValueError('Missing compiled fixture: ' + name)
    filesystem = subprocess.run(['/usr/bin/stat','-f','--format=%T','--',str(work)],
        capture_output=True, text=True, timeout=10, check=True).stdout.strip()
    if filesystem in ('9p', 'drvfs'):
        raise ValueError('Wine PE fixtures need a Linux filesystem, not a WSL Windows mount')
    result = Path(tempfile.mkdtemp(prefix='wine-abi-', dir=work))
    fixtures = result / 'fixtures'; fixtures.mkdir()
    for name in [*(name + '.exe' for name in NAMES), 'network_guard_test.dll']:
        original, target = tests/name, fixtures/name
        shutil.copyfile(original, target)
        if hashlib.sha256(original.read_bytes()).digest() != hashlib.sha256(target.read_bytes()).digest():
            raise ValueError('Compiled fixture changed during Linux staging')
    home, prefix = result / 'home', result / 'prefix'
    home.mkdir()
    environment = {k: v for k, v in os.environ.items() if not k.upper().startswith(('WINE', 'PROTON', 'STEAM_COMPAT'))}
    environment.update(HOME=str(home), WINEPREFIX=str(prefix), WINEARCH='win32', WINEDEBUG='-all,err+all',
                       WINEDLLOVERRIDES='winedbg.exe=d;mscoree,mshtml=')
    rows = []; succeeded = False

    def execute(arguments, log, timeout):
        with log.open('xb') as output:
            child = subprocess.Popen([str(wine), *map(str, arguments)], cwd=fixtures, env=environment,
                                     stdout=output, stderr=output, start_new_session=True)
            try: return child.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                # Only the session created above, never another Wine prefix.
                if child.poll() is None and os.getpgid(child.pid) == child.pid:
                    os.killpg(child.pid, signal.SIGTERM)
                child.wait(timeout=15)
                raise
    try:
        if execute(['wineboot', '--init'], result / 'prefix.log', 240):
            raise RuntimeError('Synthetic Wine prefix initialization failed')
        for name in NAMES:
            executable = fixtures / (name + '.exe')
            arguments = [executable]
            if name in ('local_platform_tests', 'lan_membership_tests'):
                parent = result / name; parent.mkdir()
                # The real root validator requires this exact suffix. The
                # fixture itself creates LocalData and refuses existing roots.
                arguments.append('Z:' + str(parent / 'LocalData').replace('/', '\\'))
            log = result / (name + '.log')
            code = execute(arguments, log, 180)
            output = log.read_text(encoding='utf-8', errors='replace')
            rows.append(dict(test=name, exit=code, skipped='SKIP' in output,
                             sha256=hashlib.sha256(executable.read_bytes()).hexdigest()))
            (result / 'result.json').write_text(json.dumps(rows, indent=2) + '\n', encoding='utf-8')
            print(name, 'exit=' + str(code), 'SKIP' if rows[-1]['skipped'] else '', flush=True)
            if code or 'FAIL ' in output or not output.strip():
                raise RuntimeError('Fixture failed; inspect ' + str(log))
        succeeded = True
        return result
    finally:
        subprocess.run([str(wine.parent / 'wineserver'), '-k'], env=environment, timeout=20, check=True)
        if succeeded and not keep_prefix:
            for path in (prefix, home):
                if path.is_symlink() or path.resolve().parent != result.resolve():
                    raise ValueError('Synthetic prefix moved during cleanup')
                shutil.rmtree(path)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wine', type=Path, required=True)
    parser.add_argument('--tests', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--keep-prefix', action='store_true', help='Retain the generated prefix after success for debugging')
    args = parser.parse_args()
    print(run(args.wine, args.tests, args.work, keep_prefix=args.keep_prefix))
