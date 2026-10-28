"""Build an offline Windows x64 preparation package, without any game data.

Requires a tested adapter/service, a local .NET 10 runtime and the pinned official
CPython embeddable ZIP. All runtime licenses are included. No downloads here.
"""
import argparse
import json
from pathlib import Path
import shutil
import zipfile

import compatibility as content
from install import write_json, FORMAT, LAN_LOBBY_TOOLS

PYTHON_SHA256 = '90905e64a840557d8c1bc4f5b74db3d7ee85e3ffb217611cac1c7813acea4159'
ADAPTER_SHA256 = '6c76f17cafccae5885590b77be064f603f8db7400933e620c758c6f21c188b64'
SERVICE_SHA256 = '1409f3f784886923957a5861b4b1f048de49d60a57f759e9c14df3f4ba2f4ed5'
ARCHIVE_SHA256 = '2b1ad77ab1afda98e9d9aab8bbc91279607e1bbbac506d2b52de675550de08ad'


def check_output_location(repo, output, inputs):
    # The relocated workspace intentionally keeps private build products below
    # work/ and outputs/. Never allow source-tree or input-directory overlap.
    if output == repo or repo.is_relative_to(output):
        raise ValueError('Output overlaps source repository')
    if output.is_relative_to(repo) and not any(output.is_relative_to(repo / name) for name in ('work', 'outputs')):
        raise ValueError('Output must stay outside source folders')
    if any(output.is_relative_to(p) or p.is_relative_to(output) for p in inputs):
        raise ValueError('Output overlaps package inputs')


def build(adapter, service, dotnet, python_zip, output):
    adapter, service, dotnet, python_zip, output = map(content.plain, (adapter, service, dotnet, python_zip, output))
    repo = Path(__file__).resolve().parents[2]
    if output.exists() or not output.parent.is_dir():
        raise ValueError('Output must be a new directory with an existing parent')
    check_output_location(repo, output, (service, dotnet, python_zip.parent, adapter.parent))
    if content.digest(adapter) != ADAPTER_SHA256 or content.digest(python_zip) != PYTHON_SHA256:
        raise ValueError('Adapter or Python distribution fingerprint mismatch')
    if content.digest(service / 'S7Patcher.dll') != SERVICE_SHA256:
        raise ValueError('Service fingerprint does not match the tested LAN build')
    version = '10.0.13'
    for path in (dotnet / 'dotnet.exe', dotnet / 'host/fxr' / version / 'hostfxr.dll',
                 dotnet / 'shared/Microsoft.NETCore.App' / version / 'System.Private.CoreLib.dll'):
        if not path.is_file(): raise ValueError('Expected tested .NET runtime is absent')
    with zipfile.ZipFile(python_zip) as archive:
        for item in archive.infolist():
            # Official archive is flat. Refuse paths/links rather than trusting ZIP extraction.
            if '/' in item.filename or '\\' in item.filename or ':' in item.filename or item.filename in ('.', '..'):
                raise ValueError('Unexpected Python ZIP path')
        output.mkdir()
        runtime = output / 'Runtime/python'; runtime.mkdir(parents=True)
        archive.extractall(runtime)
    net = output / 'Runtime/dotnet'; net.mkdir()
    for name in ('dotnet.exe', 'LICENSE.txt', 'ThirdPartyNotices.txt'):
        shutil.copyfile(dotnet / name, net / name)
    for relative in ('host/fxr/' + version, 'shared/Microsoft.NETCore.App/' + version):
        shutil.copytree(dotnet / relative, net / relative)
    server = output / 'Payload/Server'; server.mkdir(parents=True)
    for name in ('S7Patcher.dll', 'S7Patcher.deps.json', 'S7Patcher.runtimeconfig.json'):
        shutil.copyfile(service / name, server / name)
    shutil.copyfile(adapter, output / 'Payload/uplay_r1_loader.dll')
    names = ['installer/install.py', 'installer/start.py', 'installer/launch_lan.py', 'installer/compatibility.py',
             'offline-ui/launch_local.py', 'offline-ui/patch-spec.json', 'lan/bba.py', 'lan/prepare.py',
             'installer/update_join_menu.py', 'offline-ui/join-menu-update.json']
    names += ['installer/graphics_profile.py', 'installer/graphics_choice.py',
              'installer/Choose-Graphics.ps1']
    names += [original for original, _ in LAN_LOBBY_TOOLS]
    for name in names:
        target = output / 'Tools' / name; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(repo / 'tools' / name, target)
    shutil.copyfile(repo / 'LICENSE', output / 'Tools/LICENSE.txt')
    write_json(output / 'package.json', {'format': FORMAT,
        'capabilities': {'local_test': True, 'lan_host': False, 'lan_join': False},
        'join_menu_update': True,
        'graphics_choice': True,
        'automatic_lan': True,
        'patched_archive_sha256': ARCHIVE_SHA256,
        'files': content.inventory(output, ('Payload', 'Runtime', 'Tools'))})
    (output / 'Installieren.cmd').write_text('@echo off\n"%~dp0Runtime\\python\\python.exe" -B "%~dp0Tools\\installer\\install.py" --package "%~dp0."\npause\n', encoding='ascii')
    (output / 'LIESMICH.txt').write_text(
        'S7 automatische LAN-Lobby: Testpaket v4\n\n'
        'Paket vollstaendig entpacken und Installieren.cmd starten.\n'
        'Den eigenen S7-History-Installationsordner und einen neuen Testordner eingeben.\n'
        'Den Ziel-Unterordner vorher nicht anlegen. Alle bisherigen Korrekturen sind enthalten.\n'
        'Eine separate Kopie benoetigt etwa 7 GB freien Speicher. Das Original bleibt erhalten.\n'
        'Python und .NET sind enthalten; keine Systeminstallation oder Administratorrechte erforderlich.\n\n'
        'Enthalten: bekannte Zusatzinhalte, volle Kampagnenauswahl und fuer diesen Test maximal zwei physische CPU-Kerne ohne SMT.\n'
        'Der Entwicklermodus ist im LAN verbindlich deaktiviert.\n'
        'Kampagnenauswahl und CPU-Zuordnung sind im Spiel bestaetigt; weitere Belohnungen noch nicht vollstaendig geprueft.\n'
        'Neue automatische LAN-Verbindung fuer weitere Spieltests, noch keine stabile Freigabe.\n'
        'Das gleiche Paket ist fuer beide PCs vorgesehen. Lokale Spielerkennungen entstehen auf jedem PC neu.\n'
        'Nur dieses Paket weitergeben, niemals die vorbereitete Spielkopie oder LocalData.\n'
        'Dateistand-pruefen.cmd prueft die vorbereitete Kopie; compatibility.json enthaelt nur Dateinamen und Pruefsummen.\n'
        'Start-S7-LAN.cmd sucht LAN-Lobbys automatisch und bietet einen eigenen Spielernamen.\n'
        'Mehrere Rechner koennen derselben LAN-Lobby beitreten; Karten bestimmen die Partieplaetze.\n'
        'Bei Bedarf LAN-Netzwerk-freigeben.cmd fuer die private Windows-Freigabe dieser Kopie starten.\n'
        'Test-LAN-Verbindung.cmd bleibt als bisheriger Zweirechner-Diagnosestart vorhanden.\n'
        'Ein Spieler waehlt Neue LAN-Lobby, die anderen oeffnen diese Lobby aus der Liste.\n'
        'Auf allen PCs dieselbe Paketversion verwenden. Es werden keine IP-Adressen abgefragt.\n'
        'Eine oeffentliche Lobby mit freiem Platz erstellen; der Mitspieler waehlt sie aus und tritt bei.\n'
        'Das Starterfenster waehrend des Spielens geoeffnet lassen.\n'
        'Mit dem vorherigen Zweirechner-Paket waren Beitritt und Partiestart in beiden Richtungen bestaetigt.\n'
        'Die neue automatische Verbindung ist ueber die lokale Netzwerkkarte getestet; drei echte Spielclients sind noch nicht bestaetigt.\n'
        'Der zuvor anwachsende Befehlsvorlauf des Mitspielers ist korrigiert; im LAN-Test bleibt er bei fuenf Runden.\n'
        'Zeitnahe Befehle auf dem Mitspieler-PC sind bestaetigt; laengere Partien werden noch geprueft.\n'
        'In einer laengeren Partie mit PC 2 als Host trat ein Desync auf; seine Ursache ist noch offen.\n'
        'Die automatische LAN-Lobby bleibt fuer angemeldete Mitspieler bestehen, hoechstens 24 Stunden.\n'
        'Ihr Rechner muss erreichbar bleiben. Ohne Teilnehmer endet sie nach einer Minute.\n'
        'Gleiche Dateien verhindern nicht alle Synchronisationsfehler der Spielsimulation.\n', encoding='utf-8')
    (output / 'RUNTIME-SOURCES.txt').write_text(
        'CPython 3.14.10 Windows embeddable x64\n'
        'https://www.python.org/downloads/release/python-31410/\n'
        'https://www.python.org/ftp/python/3.14.10/python-3.14.10-embed-amd64.zip\nSHA256 ' + PYTHON_SHA256 + '\n'
        'License: Runtime/python/LICENSE.txt\n\n'
        '.NET 10.0.13 Windows x64 runtime from the development SDK runtime.\n'
        'Licenses: Runtime/dotnet/LICENSE.txt and ThirdPartyNotices.txt\n', encoding='utf-8')
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('adapter', 'service', 'dotnet', 'python-zip', 'output'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    print(build(args.adapter, args.service, args.dotnet, args.python_zip, args.output))
