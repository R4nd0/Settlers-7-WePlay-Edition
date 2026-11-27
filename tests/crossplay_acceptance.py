"""Drive the real Windows three-edition room runner through fresh games and cold loads."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid


UI_SCRIPT = Path(__file__).with_name('crossplay_acceptance_ui.ps1')
FULL = (0, 0, 1024, 768)
HOSTS = {1: 'Gold', 2: 'Ubisoft History', 3: 'Steam History'}
GAME_PATHS = {1: 'Data/Base/_Dbg/Bin/Release/Settlers7R.exe',
              2: 'Base/Bin/Settlers7R.exe', 3: 'Base/Bin/Settlers7R.exe'}
SLOTS = {1: (1, 2, 3), 2: (2, 1, 3), 3: (2, 3, 1)}


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def read_json(path):
    return json.loads(path.read_bytes())


def wait_for(label, predicate, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(0.25)
    raise RuntimeError(f'Timed out waiting for {label}.')


class Desktop:
    def __enter__(self):
        powershell = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
        self.process = subprocess.Popen(
            [str(powershell), '-NoProfile', '-NonInteractive', '-File', str(UI_SCRIPT)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding='utf-8',
            creationflags=subprocess.CREATE_NO_WINDOW,
        )
        return self

    def __exit__(self, *_):
        self.process.stdin.close()
        self.process.wait(timeout=15)

    def send(self, game, action, **fields):
        pid, image = game
        self.process.stdin.write(json.dumps(dict(game_pid=pid, image=str(image), action=action, **fields)) + '\n')
        self.process.stdin.flush()
        response = self.process.stdout.readline()
        if not response:
            raise RuntimeError('Windows game UI helper stopped.')
        result = json.loads(response)
        if not result['ok']:
            raise RuntimeError(result['error'])
        return result['result']

    def screen(self, game, box=FULL):
        try:
            return re.sub(r'\s+', ' ', self.send(game, 'read', box=box)['text']).strip().casefold()
        except RuntimeError as error:
            if str(error) not in ('The game window is not ready.', 'The test copy does not have foreground focus.'):
                raise
            return ''

    def expect(self, game, *labels, box=FULL, timeout=45):
        def visible():
            text = self.screen(game, box)
            return any(label.casefold() in text for label in labels)
        wait_for(' / '.join(labels), visible, timeout)

    def click(self, game, x, y):
        self.send(game, 'click', x=x, y=y)


class Run:
    def __init__(self, options, desktop, host):
        self.options = options
        self.desktop = desktop
        self.host = host
        self.games = {}
        self.latest = None
        self.trace = None

    def __enter__(self):
        marker = self.options.copies / 'launched.private.json'
        previous = marker.read_bytes() if marker.is_file() else None
        command = [str(self.options.python), '-B', str(self.options.runner), '--candidate', 'launch-three', '--host', str(self.host)]
        self.process = subprocess.Popen(command, cwd=self.options.copies.parent)

        def launched():
            if self.process.poll() is not None:
                raise RuntimeError(f'The room runner exited before startup ({self.process.returncode}).')
            try:
                value = marker.read_bytes()
                return json.loads(value) if value != previous else None
            except (FileNotFoundError, json.JSONDecodeError):
                return None

        try:
            self.info = wait_for('the new room startup receipt', launched, 180)
            pids = self.info['pids']
            failed = self.info['startup_errors']
            started = [edition for edition in HOSTS if str(edition) not in failed]
            if (self.info.get('host') != self.host or self.info.get('editions') != [1, 2, 3]
                    or self.info.get('setup_sha256') != self.options.setup_sha256
                    or self.info.get('package_id') != self.options.package_id
                    or len(pids) != len(started) or len(set(pids)) != len(started)
                    or any(type(pid) is not int or pid <= 0 for pid in pids)):
                raise RuntimeError('The room startup receipt does not match the exact requested test.')
            self.games = {edition: (pid, self.options.images[edition]) for edition, pid in zip(started, pids)}
            if failed:
                raise RuntimeError(f'Game startup failed for edition(s) {", ".join(failed)}.')
            name = self.info['trace']
            if not re.fullmatch(rf'mixed-room-{self.host}-[0-9]+\.private\.jsonl', name):
                raise RuntimeError('Invalid local trace filename.')
            path = self.options.copies / name
            self.trace = wait_for('room trace', lambda: path.open('rb') if path.is_file() else None, 15)
            return self
        except BaseException:
            self.__exit__(*sys.exc_info())
            raise

    def __exit__(self, error_type, *_):
        if self.trace is not None:
            self.trace.close()
        if self.process.poll() is None:
            for game in self.games.values():
                try:
                    self.desktop.send(game, 'close')
                except (OSError, RuntimeError) as error:
                    print(f'Test window did not accept close: {error}', flush=True)
            try:
                self.process.wait(timeout=45)
            except subprocess.TimeoutExpired:
                if error_type is None:
                    raise RuntimeError('A test window did not close normally.') from None
        if error_type is None:
            exits = read_json(self.options.copies / 'exit.private.json')['exit_codes']
            if self.process.returncode != 0 or exits != [0, 0, 0]:
                raise RuntimeError(f'Expected three normal game exits, got {exits}.')

    def sample(self):
        while True:
            start = self.trace.tell()
            line = self.trace.readline()
            if not line.endswith(b'\n'):
                self.trace.seek(start)
                break
            self.latest = json.loads(line)
        if self.process.poll() is not None or (self.latest and any(code is not None for code in self.latest['exits'])):
            raise RuntimeError('A game exited before the test was complete.')
        if self.latest and self.latest['first_departure'] is not None:
            raise RuntimeError('A game disconnected before the test was complete.')
        return self.latest or {}

    def wait_event(self, stage, count, timeout=90):
        return wait_for(stage, lambda: len([row for row in self.sample().get('deliveries', ()) if row[0] == stage]) >= count,
                        timeout)

    def world(self, seconds):
        last_turns, stalled, focus = 0, time.monotonic(), 0

        def progressed():
            nonlocal last_turns, stalled, focus
            sample = self.sample()
            world = sample.get('world', {})
            if world.get('crc_mismatches') or world.get('diagnostic_errors'):
                raise RuntimeError('The game turn evidence contains a mismatch or an invalid packet.')
            turns = world.get('common_finished_turns', 0)
            if turns > last_turns:
                last_turns, stalled = turns, time.monotonic()
            if time.monotonic() - stalled > 5:
                focus = focus % 3 + 1
                try:
                    self.desktop.send(self.games[focus], 'focus')
                except RuntimeError as error:
                    if str(error) != 'The test copy does not have foreground focus.':
                        raise
                stalled = time.monotonic()
            if (sample.get('rooms_ready') == 3 and not sample.get('rejections') and world.get('contiguous_from_zero')
                    and world['elapsed_since_first_measurement'] >= seconds):
                return world

        return wait_for('synchronized original game turns', progressed, seconds + 120)

    def open_lobby(self, save_name=None):
        gold = self.games[1]
        self.desktop.expect(gold, 'continue', box=(775, 605, 130, 50), timeout=75)
        self.desktop.click(gold, 834, 631)
        host = self.games[self.host]
        if save_name:
            self.desktop.expect(host, 'load game', 'spiel laden', box=(47, 610, 160, 55))
            self.desktop.click(host, 125, 635)
            self.desktop.expect(host, save_name, box=(309, 225, 393, 48))
            self.desktop.click(host, 440, 244)
            self.desktop.click(host, 420, 548)
            self.wait_event('saved-native-file-receipt', 2, timeout=500)
        else:
            self.desktop.expect(host, 'create session', 'sitzung erstellen', box=(708, 610, 162, 55))
            self.desktop.click(host, 786, 635)
            self.desktop.expect(host, 'monhaim valley', 'tal von monhaim', box=(155, 340, 333, 49))
            self.desktop.click(host, 230, 362)
            self.desktop.expect(host, 'ok', box=(744, 584, 140, 48))
            self.desktop.click(host, 813, 611)
            self.wait_event('native-roster-readback', 6)
            self.desktop.expect(host, 'against computer', 'gegen den computer', box=(155, 371, 207, 49))
            self.desktop.click(host, 310 if self.host == 1 else 224, 398)
            self.desktop.expect(host, 'ai opponent', 'ki-gegner', 'ki gegner', box=(388, 260, 243, 55))
            self.desktop.click(host, 610, 338)
            self.desktop.expect(host, 'maid marielle', 'magd marielle', box=(402, 460, 216, 44))
            self.desktop.click(host, 480, 478)
            self.desktop.expect(host, 'normal', box=(400, 395, 225, 36))
            self.desktop.click(host, 491, 477)
            wait_for('the original AI slot', lambda: (self.sample().get('configuration') or {}).get('computer_slots') == [4], 45)

        for edition, slot in zip(HOSTS, SLOTS[self.host]):
            if edition == self.host:
                continue
            y = {2: 250, 3: 322}[slot]
            game = self.games[edition]
            # Exact button text keeps an already-ready client from being toggled back.
            wait_for('the guest Ready button', lambda: self.desktop.screen(game, (866, y - 13, 103, 28)) in ('ready', 'bereit'), 45)
            self.desktop.click(game, 919, y)
        if save_name:
            self.wait_event('saved-ready-applied', 2)
        else:
            wait_for('both guests ready with their AI opponent', lambda: (self.sample().get('configuration') or {}).get('human_ready') == {
                '1': True, '2': True, '3': True}, 45)
        self.desktop.expect(host, 'start', box=(850, 610, 133, 50))
        self.desktop.click(host, 915, 630)
        if save_name:
            self.wait_event('saved-native-start-receipt', 2)

    def save(self, name):
        host = self.games[self.host]
        folder = self.options.copies / str(self.host) / 'LocalData/ProfileRoot'

        def saves():
            return {path: (path.stat().st_mtime_ns, path.stat().st_size) for path in folder.rglob('*.s7s') if path.is_file()}

        before = saves()
        self.desktop.send(host, 'focus')
        self.desktop.send(host, 'escape')
        self.desktop.expect(host, 'save game', 'speichern', box=(430, 251, 172, 47))
        self.desktop.click(host, 514, 274)
        self.desktop.expect(host, 'free slot', 'freies feld', box=(305, 214, 395, 58))
        self.desktop.click(host, 440, 234)
        self.desktop.click(host, 505, 527)
        self.desktop.send(host, 'type', text=name)
        self.desktop.expect(host, name, box=(359, 507, 327, 38))
        self.desktop.click(host, 421, 560)

        def written():
            changed = [path for path, stat in saves().items() if before.get(path) != stat and stat[1] > 0]
            return changed[0] if len(changed) == 1 else None

        file = wait_for('a new native save archive', written, 30)
        time.sleep(0.5)
        return file, digest(file)


def acceptance(options):
    outcomes = []
    with Desktop() as desktop:
        for host in options.hosts:
            print(f'{HOSTS[host]}: start, save, then continue in a new room.', flush=True)
            name = 'weplay' + uuid.uuid4().hex[:8]
            with Run(options, desktop, host) as fresh:
                fresh.open_lobby()
                before = fresh.world(options.sample_seconds)
                archive, archive_sha = fresh.save(name)
            with Run(options, desktop, host) as reload:
                if set(fresh.info['pids']) & set(reload.info['pids']) or fresh.info['session_sha256'] == reload.info['session_sha256']:
                    raise RuntimeError('Reload must use a fresh room and fresh processes.')
                reload.open_lobby(name)
                if digest(archive) != archive_sha:
                    raise RuntimeError('The original native save changed during reload.')
                after = reload.world(options.sample_seconds)
            outcomes.append(dict(host=HOSTS[host], before=before, after=after, save_sha256=archive_sha,
                                 initial_trace=fresh.info['trace'], reload_trace=reload.info['trace']))
            print(f'{HOSTS[host]}: {after["common_finished_turns"]} matching turns after reload.', flush=True)
    return outcomes


def finish_policy(copies):
    done = copies / 'policy-finished-three'
    with done.open('x', encoding='ascii') as marker:
        marker.write('finished')
    cleanup = copies / 'policy-cleanup-three.private.json'
    outcome = wait_for('temporary network rule cleanup', lambda: read_json(cleanup) if cleanup.is_file() else None, 15)
    if outcome != {'failures': 0, 'active': 0}:
        raise RuntimeError('Temporary network rules did not fully clean up.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('copies', 'setup', 'prepared', 'python', 'runner', 'receipt'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--hosts', type=int, choices=HOSTS, nargs='+', default=list(HOSTS))
    parser.add_argument('--sample-seconds', type=int, choices=range(1, 121), default=60, metavar='1..120')
    options = parser.parse_args()
    if os.name != 'nt' or options.setup.name != 'Setup.exe' or len(set(options.hosts)) != len(options.hosts):
        parser.error('Use Windows, an exact Setup.exe, and distinct host roles.')
    for name in ('copies', 'setup', 'prepared', 'python', 'runner'):
        setattr(options, name, getattr(options, name).resolve(strict=True))
    if options.receipt.exists():
        parser.error('The acceptance receipt must be a new file.')
    proof = read_json(options.setup.with_suffix('.release.json'))
    options.setup_sha256 = digest(options.setup)
    if proof.get('setup_sha256') != options.setup_sha256 or proof.get('crossplay_mixed') is not True:
        parser.error('The installer does not match its mixed Crossplay package receipt.')
    options.package_id = proof['package_id']
    prepared = read_json(options.prepared)
    if (options.prepared.parent != options.copies or prepared.get('package_id') != options.package_id
            or prepared.get('setup_sha256') != options.setup_sha256
            or [entry['edition'] for entry in prepared.get('editions', [])] != list(HOSTS)):
        parser.error('Use the three isolated copies verified against this installer.')
    options.images = {edition: (options.copies / str(edition) / path).resolve(strict=True)
                      for edition, path in GAME_PATHS.items()}
    if any(not image.is_relative_to(options.copies / str(edition)) or digest(image) != entry['exe']
           for (edition, image), entry in zip(options.images.items(), prepared['editions'])):
        parser.error('A game executable differs from its verified test copy.')
    ready = options.copies / 'policy-ready-three.private.json'
    if (not ready.is_file() or read_json(ready) != {'ready': True}
            or any((options.copies / name).exists() for name in ('policy-finished-three', 'policy-cleanup-three.private.json'))):
        parser.error('Start the temporary rules for these three test copies first.')
    with options.receipt.open('x', encoding='utf-8') as report:
        try:
            result = acceptance(options)
            finish_policy(options.copies)
        except Exception as error:
            json.dump(dict(setup_sha256=options.setup_sha256, pass_=False, error=str(error)), report, indent=2)
            raise
        json.dump(dict(setup_sha256=options.setup_sha256, package_id=options.package_id, pass_=True,
                       temporary_rules_removed=True, host_roles=result), report, indent=2)


if __name__ == '__main__':
    main()
