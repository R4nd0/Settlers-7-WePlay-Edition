"""Create a NEW D3D11 experiment from a prepared native-D3D9 S7 test bundle.

Never edits the source or an existing output. No downloads, system changes,
game executable patches, copied profiles or account data. On interruption the
new output is incomplete; launch only after this tool reports success.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'installer'))
import compatibility as content
sys.path.insert(0, str(Path(__file__).resolve().parent))
import menu_lock

EXE = '4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f'
ARCHIVE = '9afa493396feec7108f42a2d97661eef96bbaac5addf0dfb5cfbc3001cd76a0a'
DLL = '060cb161eb7a0c608a520185f0b334db325ad71d69016bc26060c9d5b9651f7d'
URL = 'https://github.com/dege-diosg/dgVoodoo2/releases/download/2.87.6/dgVoodoo2_87_6.zip'


def configuration(template, adapter, fps=60, watermark=False):
    if type(adapter) is not int or not 1 <= adapter <= 32:
        raise ValueError('Bitte eine gueltige einbasierte DXGI-Adapterzahl angeben.')
    if type(fps) is not int or not 30 <= fps <= 240:
        raise ValueError('Bildratenlimit muss zwischen 30 und 240 liegen.')
    # Keep application resolution, AA and filtering. No forced HDR or fake caps.
    values = {'OutputAPI': 'd3d11_fl11_0', 'Adapters': str(adapter),
              'FPSLimit': str(fps), 'VRAM': '512',
              'dgVoodooWatermark': str(watermark).lower()}
    for key, value in values.items():
        template, count = re.subn(r'(?m)^' + key + r'\s*=.*$',
                                  key + ' = ' + value, template)
        if count != 1:
            raise ValueError('Unerwartetes dgVoodoo-Konfigurationsformat.')
    return template.encode('utf-8')


def package(path):
    path = content.plain(path)
    if content.digest(path) != ARCHIVE:
        raise ValueError('Nicht das gepruefte offizielle dgVoodoo2-2.87.6-Paket.')
    with zipfile.ZipFile(path) as archive:
        # Read exactly two members; never extract archive paths.
        dll = archive.read('MS/x86/D3D9.dll')
        template = archive.read('dgVoodoo.conf').decode('utf-8-sig')
    if hashlib.sha256(dll).hexdigest() != DLL:
        raise ValueError('Unerwartete D3D9-Uebersetzungsbibliothek.')
    pe = struct.unpack_from('<I', dll, 60)[0]
    if dll[pe:pe+4] != b'PE\0\0' or struct.unpack_from('<H', dll, pe+4)[0] != 0x14c:
        raise ValueError('Die Bibliothek muss x86 sein.')
    return dll, template


def prepare(source, output, archive, adapter, fps=60):
    source, output = content.plain(source), content.plain(output)
    if output.exists() or output.is_relative_to(source) or source.is_relative_to(output):
        raise ValueError('Ausgabe muss ein neuer, getrennter Ordner sein.')
    dll, template = package(archive)
    conf = configuration(template, adapter, fps)
    expected = content.verify(source)
    bundle = json.loads(content.plain(source / 'local-bundle.json').read_text('utf-8'))
    core = {'Base/Bin/Settlers7R.exe', 'Base/Bin/uplay_r1_loader.dll', 'Base/BBA/Interface.bba'}
    if (bundle.get('format') != 's7-local-experimental-v1'
            or set(bundle.get('files', {})) != core
            or bundle['files']['Base/Bin/Settlers7R.exe'] != EXE):
        raise ValueError('Quelle ist keine unterstuetzte lokale History-Testkopie.')
    for name, digest in bundle['files'].items():
        if expected['files'].get(name.lower()) != digest:
            raise ValueError('Startmanifest und Dateiinventar stimmen nicht ueberein.')
    locked_menu = menu_lock.build(content.plain(source / 'Base/BBA/Interface.bba').read_bytes())
    if any((source / 'Base/Bin' / name).exists() for name in
           ('d3d9.dll', 'dgVoodoo.conf', 'dxgi.dll', 'd3d11.dll', 'dxvk.conf')):
        raise ValueError('Quelle muss den unveraenderten D3D9-Grafikpfad verwenden.')
    # Preflight the full copy set, excluding all LocalData/profiles/logs.
    paths = []
    for folder in ('Base', 'Launcher', 'Runtime', 'Server'):
        start = content.plain(source / folder)
        if not start.is_dir():
            raise ValueError('Ein benoetigter Testordner fehlt.')
        for path in start.rglob('*'):
            content.plain(path)
            if path.is_file():
                paths.append(path)
    output.mkdir(parents=True)
    for path in paths:
        target = output / path.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    (output / 'Base/Bin/d3d9.dll').write_bytes(dll)
    (output / 'Base/Bin/dgVoodoo.conf').write_bytes(conf)
    (output / 'Base/BBA/Interface.bba').write_bytes(locked_menu)
    bundle['files']['Base/BBA/Interface.bba'] = hashlib.sha256(locked_menu).hexdigest()
    bundle['runtime_verified'] = bundle['lan_verified'] = False
    (output / 'local-bundle.json').write_text(json.dumps(bundle, indent=2), 'utf-8')
    # Validate every copied content file and add only the two intended files.
    actual = content.create(output)
    additions = {'base/bin/d3d9.dll', 'base/bin/dgvoodoo.conf', 'base/bba/interface.bba'}
    if set(content.compare(expected, actual)) != additions:
        raise ValueError('Die Kopie weicht unerwartet von der Quelle ab; nicht starten.')
    (output / 'compatibility.json').write_text(json.dumps(actual, indent=2), 'utf-8')
    receipt = {'format': 's7-d3d11-experiment-v1', 'dgvoodoo_version': '2.87.6',
               'package_sha256': ARCHIVE, 'dll_sha256': DLL, 'source_url': URL,
               'backend': 'd3d11_fl11_0', 'adapter_ordinal': adapter, 'fps_limit': fps,
               'menu_locked': True,
               'runtime_verified': False, 'lan_verified': False}
    (output / 'd3d11-experiment.json').write_text(json.dumps(receipt, indent=2), 'utf-8')
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--archive', required=True)
    parser.add_argument('--adapter', type=int, required=True)
    parser.add_argument('--fps', type=int, default=60)
    args = parser.parse_args()
    prepare(args.source, args.output, args.archive, args.adapter, args.fps)
    print('Getrennte D3D11-Testkopie erstellt und Dateistand geprueft.')


if __name__ == '__main__':
    main()
