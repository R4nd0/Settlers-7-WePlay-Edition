"""Build the offline Gold installer from pinned local runtimes; no downloads/game data."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import installer
gold = installer.gold

REPO = Path(__file__).resolve().parents[2]
TOOLS = ('prepare.py', 'menu_interface.py', 'profile_interface.py', 'installer.py',
         'start.py', 'bootstrap.py', 'entry_bridge.py', 'release_interface.py', 'graphics_assets.py')


def build(stage, output, *, adapter, python_zip, dxvk, dxvk_license, compiler,platform_runtime=None):
    if platform_runtime is None:raise ValueError('Public Gold packages require the verified standalone runtime.')
    stage, output, adapter, python_zip, dxvk, dxvk_license, compiler = map(
        gold.plain, (stage, output, adapter, python_zip, dxvk, dxvk_license, compiler))
    if (stage.exists() or not stage.parent.is_dir() or not stage.is_relative_to(REPO / 'work')
            or not output.is_relative_to(REPO / 'outputs') or output.exists()):
        raise ValueError('Use new stage/output directories below work/ and outputs/.')
    for path in (adapter, python_zip, dxvk, dxvk_license, compiler):
        if stage.is_relative_to(path.parent) or output.is_relative_to(path.parent):
            raise ValueError('Gold installer output overlaps an input directory.')
    expected_adapter=installer.ADAPTER_SHA256
    if platform_runtime is not None:
        sys.path.insert(0,str(REPO/'tools/installer'))
        import platform_standalone
        platform_runtime=platform_standalone.payload(platform_runtime)
        expected_adapter=platform_standalone.GOLD
    for path, expected in ((adapter, expected_adapter), (python_zip, installer.PYTHON_SHA256),
                           (dxvk, gold.DXVK_SHA256), (REPO / 'Definitions/Definitions.bin', gold.DEFINITIONS_SHA256)):
        if gold.digest(path) != expected:
            raise ValueError('Gold installer input fingerprint differs: ' + path.name)
    if not compiler.is_file() or not dxvk_license.is_file():
        raise ValueError('Installer compiler or DXVK license missing.')
    stage.mkdir(); app = stage / 'App'; app.mkdir()
    runtime = app / 'Runtime/python'; runtime.mkdir(parents=True)
    with zipfile.ZipFile(python_zip) as archive:
        for entry in archive.infolist():
            if (not entry.filename or '/' in entry.filename or '\\' in entry.filename or ':' in entry.filename
                    or entry.filename in ('.', '..') or (entry.external_attr >> 16) & 0xf000 == 0xa000):
                raise ValueError('Unexpected embedded Python ZIP member.')
        archive.extractall(runtime)
    for name in TOOLS:
        target = app / 'Tools/gold' / name; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(REPO / 'tools/gold' / name, target)
    midsummer=app/'Tools/midsummer';midsummer.mkdir()
    for name in ('profile.py','archive_append.py','camera_preferences.lua','runtime.lua'):
        shutil.copy2(REPO/'tools/midsummer'/name,midsummer/name)
    if platform_runtime is not None:
        for name in (*platform_standalone.TOOLS,'compatibility.py'):
            shutil.copy2(REPO/'tools/installer'/name,app/'Tools/gold'/name)
    lan = app / 'Tools/lan'; lan.mkdir()
    shutil.copy2(REPO / 'tools/lan/bba.py', lan / 'bba.py')
    definitions = app / 'Definitions'; definitions.mkdir()
    shutil.copy2(REPO / 'Definitions/Definitions.bin', definitions / 'Definitions.bin')
    payload = app / 'Payload'; payload.mkdir()
    shutil.copy2(adapter, payload / 'weplay_gold_startup.dll')
    if platform_runtime is not None:
        folder=payload/'Platform';folder.mkdir()
        for name in platform_standalone.PAYLOAD:shutil.copy2(platform_runtime/name,folder/name)
    shutil.copy2(dxvk, payload / 'd3d9.dll')
    licenses = app / 'Licenses'; licenses.mkdir()
    for source, name in ((REPO / 'LICENSE', 'WePlay-MIT.txt'),
                         (REPO / 'tools/lan/LICENSE.S7Packer.txt', 'S7Packer-MIT.txt'),
                         (dxvk_license, 'DXVK.txt')):
        shutil.copy2(source, licenses / name)
    (app / 'package.json').write_text(json.dumps({
        'format': installer.FORMAT, 'menu_sha256': gold.MENU_SHA256, 'multiplayer': False,'platform_independent':platform_runtime is not None,
        'files': installer.inventory(app, ('Tools', 'Runtime', 'Payload', 'Definitions', 'Licenses'))}, indent=2) + '\n', encoding='utf8')
    installer.verify_package(app)
    # Exercise the shipped interpreter, dependencies and menu compressor before
    # compiling. The archive itself is always built on the recipient's machine.
    check = subprocess.run([str(runtime / 'python.exe'), '-B', '-c',
        'import sys;sys.path.insert(0,sys.argv[1]);import installer;installer.verify_package(sys.argv[2]);'
        'import menu_interface;assert menu_interface.profile.SOURCE==installer.gold.INTERFACE_SHA256',
        str(app / 'Tools/gold'), str(app)], capture_output=True, text=True)
    if check.returncode:
        raise RuntimeError('Embedded Gold runtime check failed: ' + check.stdout + check.stderr)
    output.mkdir(parents=True)
    result = subprocess.run([str(compiler), '/Qp', '/DStage=' + str(app), '/DOutput=' + str(output),
                             str(REPO / 'tools/gold/Setup.iss')], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    setup = output / 'WePlay-Gold-Setup.exe'
    if not setup.is_file():
        raise RuntimeError('Gold installer was not produced.')
    checksum = gold.digest(setup)
    (output / 'WePlay-Gold-Setup.exe.sha256').write_text(checksum + '  WePlay-Gold-Setup.exe\n', encoding='ascii')
    (output / 'LIESMICH.txt').write_text(
        'WePlay Gold – Einzelspieler\n\n'
        'WePlay-Gold-Setup.exe starten und die eigene unveränderte Gold-Installation 1.12.1396 auswählen.\n'
        'Setup erstellt eine separate Kopie mit bereinigtem Interface und vollständiger Kampagnenauswahl.\n'
        'Start über die Verknüpfung „WePlay Gold“. Python und DXVK sind enthalten.\n'
        + ('Das Original bleibt unverändert. Spielstände/Einstellungen liegen innerhalb der Kopie unter LocalData/ProfileRoot/Profiles.\n'
           'Steam und Ubisoft Connect werden nicht benötigt; Windows-Netzisolation beim ersten Start bestätigen.\n'
           if platform_runtime is not None else 'Das Original bleibt unverändert. Spielstände liegen im isolierten Documents/WePlay/Gold-Profil.\n') +
        'Mindestens 4 GiB freier RAM und eine DXVK/Vulkan-kompatible Grafikkarte erforderlich.\n'
        'Dieses Gold-Paket enthält keine freigegebene Mehrspieler-/History-Crossplay-Integration.\n'
        'Vorhandene Gold-Testkopien werden nicht überschrieben; für diese Version einen neuen Zielordner wählen.\n', encoding='utf8')
    print(json.dumps({'setup': str(setup), 'sha256': checksum, 'game_data_included': False}))
    return app


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'output', 'adapter', 'python-zip', 'dxvk', 'dxvk-license', 'compiler'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--platform-runtime',type=Path,required=True)
    args = parser.parse_args()
    build(args.stage, args.output, adapter=args.adapter, python_zip=args.python_zip,
          dxvk=args.dxvk, dxvk_license=args.dxvk_license, compiler=args.compiler,platform_runtime=args.platform_runtime)
