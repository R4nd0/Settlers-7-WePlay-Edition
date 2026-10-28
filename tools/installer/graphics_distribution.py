"""Activate/restore the bundled optional graphics profile on a fresh RC copy."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compatibility as content
import graphics_profile as graphics
import install

LICENSE_SHA256 = 'a5cb1a6ded7d2d7e92d550ba28edd21be2d1d4044662b399887351023e30ce64'


def apply(package, bundle, action):
    package, bundle = content.plain(package), content.plain(bundle)
    install.package_manifest(package)
    if action not in ('install', 'restore'):
        raise ValueError('Unbekannte Grafikaktion.')
    source = package / 'Payload/Graphics/d3d9.dll'
    license_path = package / 'Payload/Graphics/LICENSE'
    if (content.digest(source) != graphics.DXVK_SHA256
            or content.digest(license_path) != LICENSE_SHA256):
        raise ValueError('Das mitgelieferte Grafikprofil ist unvollstaendig oder veraendert.')
    # Do not enable a new renderer on an old copy whose launcher lacks today's
    # shader compatibility and display handling. Never update files in place.
    helpers = [*install.GRAPHICS_TOOLS, *install.DISPLAY_TOOLS,
               ('offline-ui/launch_local.py','launch_local.py'),
               ('installer/launch_lan.py','launch_lan.py'),
               ('installer/launch_auto_lan.py','launch_auto_lan.py')]
    for original, target in helpers:
        if content.digest(content.plain(bundle / 'Launcher' / target)) != content.digest(package / 'Tools' / original):
            raise ValueError('Bitte zuerst mit diesem RC eine neue getrennte Testkopie anlegen.')
    return graphics.install(bundle, source) if action == 'install' else graphics.restore(bundle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('install','restore'))
    parser.add_argument('--package', default=str(Path(__file__).resolve().parents[2]))
    parser.add_argument('--bundle')
    args = parser.parse_args()
    try:
        print('Optionales Grafikprofil fuer die getrennte RC-Testkopie. Alle S7-Spiele vorher schliessen.')
        bundle = args.bundle or input('Ordner der neu vorbereiteten RC-Spielkopie: ').strip().strip('"')
        if not bundle:
            raise ValueError('Der Ordner der Testkopie fehlt.')
        result = apply(Path(args.package), Path(bundle), args.action)
        print('Grafikprofil: ' + result + '. Danach den Starter in der Testkopie verwenden.')
        return 0
    except (OSError, ValueError, KeyError, TypeError, EOFError) as error:
        print('Grafikprofil abgebrochen: ' + str(error))
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
