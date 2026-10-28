"""Launch a prepared D3D11 experiment with a fresh hardware choice each time.

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


@contextmanager
def session_lock(root):
    data = content.plain(root / 'LocalData')
    data.mkdir(exist_ok=True)
    with content.plain(data / 'session.lock').open('a+b') as lock:
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


def choose(cards):
    result = subprocess.run(
        ['powershell.exe', '-NoProfile', '-NonInteractive', '-STA', '-WindowStyle', 'Hidden',
         '-File', str(Path(__file__).with_name('Choose-Adapter.ps1'))],
        input=json.dumps(cards).encode('ascii'), capture_output=True,
        creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode == 10:
        return None
    if result.returncode or not result.stdout.strip().isdigit():
        raise ValueError('Grafikkartenauswahl fehlgeschlagen.')
    return int(result.stdout.strip())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', required=True)
    parser.add_argument('--archive', required=True)
    parser.add_argument('--adapter', type=int, help='Optional: Hardwarewahl fuer automatisierte lokale Tests')
    parser.add_argument('--preflight', action='store_true')
    args = parser.parse_args()
    root = content.plain(args.bundle)
    if Path(sys.executable).resolve() != content.plain(root / 'Runtime/python/python.exe'):
        raise ValueError('Bitte den Python-Starthelfer dieser Testkopie verwenden.')
    _, template = prepare.package(args.archive)
    cards = enumerate_adapters()
    ordinal = args.adapter if args.adapter is not None else choose(cards)
    if ordinal is None:
        return 0
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
        expected = content.load(content.plain(root / 'compatibility.json'))
        # The preparation performs the full multi-gigabyte content check.
        # Launch checks the actual executable, graphics/engine modules and
        # launch code, without re-reading all maps and videos every time.
        required = ['Base/Bin/Settlers7R.exe', 'Base/Bin/uplay_r1_loader.dll',
                    'Base/Bin/Vision90.dll', 'Base/Bin/vBase90.dll',
                    'Base/Bin/d3d9.dll', 'Base/Bin/dgVoodoo.conf',
                    'Base/BBA/Interface.bba', 'Launcher/launch_local.py',
                    'Server/S7Patcher.dll']
        if expected['files'].get('base/bin/settlers7r.exe') != prepare.EXE:
            raise ValueError('Unbekannte History-EXE.')
        for name in required:
            if content.digest(content.plain(root / name)) != expected['files'].get(name.lower()):
                raise ValueError('Eine benoetigte Startdatei wurde extern veraendert.')
        receipt_path = content.plain(root / 'd3d11-experiment.json')
        if receipt_path.stat().st_size > 4096:
            raise ValueError('Ungueltiger Testkopie-Beleg.')
        receipt = json.loads(receipt_path.read_text('utf-8'))
        if (receipt.get('format') != 's7-d3d11-experiment-v1'
                or receipt.get('package_sha256') != prepare.ARCHIVE
                or expected['files'].get('base/bin/d3d9.dll') != prepare.DLL):
            raise ValueError('Keine gepruefte D3D11-Testkopie.')
        conf = prepare.configuration(template, ordinal, receipt['fps_limit'])
        conf_path = content.plain(root / 'Base/Bin/dgVoodoo.conf')
        manifest_path = content.plain(root / 'compatibility.json')
        originals = {path: path.read_bytes() for path in (conf_path, manifest_path, receipt_path)}
        expected['files']['base/bin/dgvoodoo.conf'] = hashlib.sha256(conf).hexdigest()
        expected['content_id'] = content.fingerprint(expected['files'])
        receipt['adapter_ordinal'] = ordinal
        try:
            for path, data in ((conf_path, conf), (manifest_path, encoded(expected)),
                               (receipt_path, encoded(receipt))):
                atomic_write(path, data, root / 'LocalData')
        except BaseException:
            for path, data in originals.items():
                atomic_write(path, data, root / 'LocalData')
            raise
    # Load the already fingerprinted launcher from the OWN copy, not the
    # concurrently developed installer launcher in this repository.
    spec = importlib.util.spec_from_file_location('d3d11_bundle_launcher', root / 'Launcher/launch_local.py')
    launcher = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(launcher)
    return launcher.run(SimpleNamespace(
        bundle=str(root), dotnet=str(root / 'Runtime/dotnet/dotnet.exe'),
        server=str(root / 'Server/S7Patcher.dll'), local_connect_probe=True,
        local_session_display_probe=False, launch_game=not args.preflight))


if __name__ == '__main__':
    raise SystemExit(main())
