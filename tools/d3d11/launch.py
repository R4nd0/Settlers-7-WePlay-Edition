"""Safely start the experiment using its native D3D9 fallback.

Run using the COPY's Runtime/python/python.exe, so process ownership remains
verifiable by the passive observer. Uses the copy's existing local launcher.
"""
import argparse
import importlib.util
from contextlib import contextmanager
import hashlib
import json
import msvcrt
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
from adapters import enumerate_adapters
import prepare
from prepare import content
import settings
import menu_lock
import memory_guard
import native_fallback

GAMEPLAY_QUALIFIED = False


class QualificationError(RuntimeError):
    pass


def require_qualified(preflight):
    if not preflight and not GAMEPLAY_QUALIFIED:
        raise QualificationError('Spielstart gesperrt: D3D11 verursacht Speicherfehler; '
                           'auch der D3D9-Rueckfall haengt im Menue. '
                           'Bis zur Klaerung ist nur --preflight verfuegbar.')


@contextmanager
def session_lock(root, name='session.lock'):
    data = content.plain(root / 'LocalData')
    data.mkdir(exist_ok=True)
    with content.plain(data / name).open('a+b') as lock:
        if lock.tell() == 0:
            lock.write(b'0'); lock.flush()
        lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        try:
            yield
        finally:
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)


def atomic_write(path, data, staging):
    with tempfile.NamedTemporaryFile(dir=staging, prefix='d3d11-', delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(data); stream.flush(); os.fsync(stream.fileno())
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def encoded(value):
    return (json.dumps(value, indent=2) + '\n').encode('utf-8')


def choose(cards, preferences, previous_adapter, recovery=False):
    result = subprocess.run(
        ['powershell.exe', '-NoProfile', '-NonInteractive', '-STA', '-WindowStyle', 'Hidden',
         '-File', str(Path(__file__).with_name('Choose-Adapter.ps1'))],
        input=json.dumps({'cards': cards, 'settings': preferences,
                          'adapter': previous_adapter, 'recovery': recovery,
                          'preflight': True}).encode('ascii'), capture_output=True,
        creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode == 10:
        return None
    if result.returncode or len(result.stdout) > 8192:
        raise ValueError('Grafikkartenauswahl fehlgeschlagen.')
    value = json.loads(result.stdout)
    if not isinstance(value, dict) or set(value) != {'adapter', 'settings'} or type(value['adapter']) is not int:
        raise ValueError('Ungueltige Grafikauswahl.')
    return value['adapter'], settings.validate(value['settings'])


class GuardedSubprocess:
    """Instrument only the exact game Popen call in the verified local launcher."""
    def __init__(self, root, report, sticky):
        self.root, self.report, self.sticky = root, report, sticky
        self.sentinel = None

    def __getattr__(self, key):
        return getattr(subprocess, key)

    def Popen(self, command, *args, **kwargs):
        executable = self.root / 'Base/Bin/Settlers7R.exe'
        is_game = isinstance(command, list) and command == [str(executable)]
        if is_game:
            require_qualified(False)
        if is_game and (args or Path(kwargs.get('cwd', '')).resolve() != executable.parent):
            raise ValueError('Unerwarteter Spielstart fuer die Speicherpruefung.')
        child = subprocess.Popen(command, *args, **kwargs)
        if is_game:
            try:
                self.sentinel = memory_guard.Sentinel(child,self.report,
                    memory_guard.Probe(child,executable),sticky=self.sticky)
                self.sentinel.start()
            except BaseException:
                # This is the newly created, owned Popen HANDLE, never a PID search.
                if child.poll() is None:
                    child.terminate(); child.wait(timeout=10)
                raise RuntimeError('Spielstart ohne funktionierende Speicherpruefung abgebrochen.') from None
        return child

    def close(self):
        if self.sentinel:
            self.sentinel.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', required=True)
    parser.add_argument('--archive', required=True)
    parser.add_argument('--adapter', type=int, help='Optional: Hardwarewahl fuer automatisierte lokale Tests')
    parser.add_argument('--preflight', action='store_true')
    args = parser.parse_args()
    require_qualified(args.preflight)
    root = content.plain(args.bundle)
    # This distinct lock spans dialog, configuration AND the original launcher's
    # own session lock. A second invocation cannot change choices in that gap.
    with session_lock(root,'graphics-launch.lock'):
        return run(args,root)


def run(args,root):
    require_qualified(args.preflight)
    if Path(sys.executable).resolve() != content.plain(root / 'Runtime/python/python.exe'):
        raise ValueError('Bitte den Python-Starthelfer dieser Testkopie verwenden.')
    prepare.package(args.archive)
    receipt_path = content.plain(root / 'd3d11-experiment.json')
    if receipt_path.stat().st_size > 4096:
        raise ValueError('Ungueltiger Testkopie-Beleg.')
    receipt = json.loads(receipt_path.read_text('utf-8'))
    options_path = content.plain(settings.options_path())
    original_options = options_path.read_bytes() if options_path.exists() else b''
    preferences_path = content.plain(root / 'LocalData/graphics-settings.json')
    if preferences_path.exists():
        if preferences_path.stat().st_size > 8192:
            raise ValueError('Unbekanntes Grafikprofil.')
        preferences = settings.validate(json.loads(preferences_path.read_text('utf-8')))
    else:
        preferences = settings.read_defaults(original_options)
    report = content.plain(root / 'LocalData/memory-summary.json')
    recovery = memory_guard.recovery_required(report)
    preferences = settings.conservative(preferences,recovery)
    cards = enumerate_adapters()
    ordinal = args.adapter
    if ordinal is None:
        chosen = choose(cards, preferences, receipt['adapter_ordinal'], recovery)
        if chosen is None:
            return 0
        ordinal, preferences = chosen
    preferences = settings.conservative(preferences,recovery)
    selected = next((card for card in cards if card['ordinal'] == ordinal), None)
    current = enumerate_adapters()
    if selected is None or selected not in current:
        raise ValueError('Die ausgewaehlte Grafikkarte ist nicht mehr vorhanden.')
    with session_lock(root):
        check = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command',
                                'if(Get-Process Settlers7R -ErrorAction SilentlyContinue){exit 7};exit 0'],
                               capture_output=True, timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)
        if check.returncode:
            raise ValueError('Vor einer Grafikaenderung bitte alle S7-Testinstanzen schliessen.')
        if content.digest(content.plain(root / 'Base/Bin/Settlers7R.exe')) != prepare.EXE:
            raise ValueError('Unbekannte History-EXE.')
        # D3D11 failed repeated map loads even at medium quality and lower VRAM.
        # The known wrapper is removed using a restartable, fixed-file journal.
        receipt = native_fallback.ensure_native(root)
        expected = content.load(content.plain(root / 'compatibility.json'))
        # The preparation performs the full multi-gigabyte content check.
        # Launch checks the actual executable, graphics/engine modules and
        # launch code, without re-reading all maps and videos every time.
        required = ['Base/Bin/Settlers7R.exe', 'Base/Bin/uplay_r1_loader.dll',
                    'Base/Bin/Vision90.dll', 'Base/Bin/vBase90.dll',
                    'Base/BBA/Interface.bba', 'Launcher/launch_local.py',
                    'Server/S7Patcher.dll']
        if expected['files'].get('base/bin/settlers7r.exe') != prepare.EXE:
            raise ValueError('Unbekannte History-EXE.')
        for name in required:
            if content.digest(content.plain(root / name)) != expected['files'].get(name.lower()):
                raise ValueError('Eine benoetigte Startdatei wurde extern veraendert.')
        if (receipt.get('format') != 's7-d3d11-experiment-v1'
                or receipt.get('package_sha256') != prepare.ARCHIVE
                or receipt.get('backend') != 'native_d3d9'):
            raise ValueError('Keine gepruefte D3D11-Testkopie.')
        if any(content.plain(root / 'Base/Bin' / name).exists() for name in
               ('d3d9.dll','dgVoodoo.conf','dxgi.dll','d3d11.dll','dxvk.conf')):
            raise ValueError('Der native Rueckfallweg enthaelt fremde Grafikdateien.')
        manifest_path = content.plain(root / 'compatibility.json')
        menu_path = content.plain(root / 'Base/BBA/Interface.bba')
        bundle_path = content.plain(root / 'local-bundle.json')
        originals = {path: path.read_bytes() for path in
                     (manifest_path, menu_path, bundle_path)}
        originals[preferences_path] = preferences_path.read_bytes() if preferences_path.exists() else None
        menu = originals[menu_path]
        if hashlib.sha256(menu).hexdigest() == menu_lock.ARCHIVE:
            menu = menu_lock.build(menu)
        if hashlib.sha256(menu).hexdigest() != menu_lock.PATCHED:
            raise ValueError('Unbekannte Grafikmenue-Sperre.')
        bundle = json.loads(originals[bundle_path])
        bundle['files']['Base/BBA/Interface.bba'] = menu_lock.PATCHED
        expected['files']['base/bba/interface.bba'] = menu_lock.PATCHED
        expected['content_id'] = content.fingerprint(expected['files'])
        # The installed game uses this Documents/Options.ini. Only the
        # allowlisted graphics keys change; audio and other sections survive.
        next_options = settings.replace_game_values(original_options, preferences)
        options_written = False
        try:
            for path, data in ((menu_path, menu),
                               (bundle_path, encoded(bundle)),
                               (manifest_path, encoded(expected)),
                               (preferences_path, encoded(preferences))):
                atomic_write(path, data, root / 'LocalData')
            if not args.preflight:
                current_options = options_path.read_bytes() if options_path.exists() else b''
                if current_options != original_options:
                    raise ValueError('S7-Einstellungen wurden waehrend der Auswahl veraendert.')
                backup = content.plain(root / 'LocalData/options-before-graphics.ini')
                if not backup.exists():
                    with backup.open('xb') as stream:
                        stream.write(original_options)
                options_path.parent.mkdir(parents=True, exist_ok=True)
                atomic_write(options_path, next_options, options_path.parent)
                options_written = True
        except BaseException:
            for path, data in originals.items():
                if data is None:
                    path.unlink(missing_ok=True)
                else:
                    atomic_write(path, data, root / 'LocalData')
            if options_written:
                atomic_write(options_path, original_options, options_path.parent)
            raise
    # Load the already fingerprinted launcher from the OWN copy, not the
    # concurrently developed installer launcher in this repository.
    spec = importlib.util.spec_from_file_location('d3d11_bundle_launcher', root / 'Launcher/launch_local.py')
    launcher = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(launcher)
    guarded = GuardedSubprocess(root,report,recovery)
    launcher.subprocess = guarded
    try:
        result = launcher.run(SimpleNamespace(
            bundle=str(root), dotnet=str(root / 'Runtime/dotnet/dotnet.exe'),
            server=str(root / 'Server/S7Patcher.dll'), local_connect_probe=True,
            local_session_display_probe=False, launch_game=not args.preflight))
        if result and guarded.sentinel:
            guarded.sentinel.summary['recovery_required'] = True
        return result
    finally:
        guarded.close()


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        # Fixed qualification errors are safe to show; unexpected errors may
        # contain a private runtime path, so keep their details out of the UI.
        if isinstance(error, QualificationError):
            print('Spielstart gesperrt: D3D11-Speicherfehler und D3D9-Menuehaenger '
                  'sind noch offen. Nur --preflight ist verfuegbar.',flush=True)
        else:
            print('Startpruefung fehlgeschlagen. Kein weiterer Spielstart.',flush=True)
        raise SystemExit(2)
