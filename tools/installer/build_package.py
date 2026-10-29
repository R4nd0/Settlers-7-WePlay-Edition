"""Build an offline Windows x64 preparation package, without any game data.

Requires a tested adapter/service, a local .NET 10 runtime and the pinned official
CPython embeddable ZIP. All runtime licenses are included. No downloads here.
"""
import argparse
import json
from pathlib import Path
import shutil
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compatibility as content
from install import write_json, FORMAT, LAN_LOBBY_TOOLS, DISPLAY_TOOLS, DESKTOP_TOOLS, DESKTOP_PACKAGE_TOOLS, INTERNET_TOOLS, DISCORD_PACKAGE_TOOLS, UPDATE_PACKAGE_TOOLS, ACHIEVEMENT_PACKAGE_TOOLS, WINDOWS_API_TOOLS

PYTHON_SHA256 = '90905e64a840557d8c1bc4f5b74db3d7ee85e3ffb217611cac1c7813acea4159'
ADAPTER_SHA256 = '14f9bc05638480a9f3cf32c6e668ce35781e47dc368a526d0c75223b5412bf16'
SERVICE_SHA256 = 'b228b88e6200bce03ea4dcd6befdb989e9b218b37e29a259b321662cbda61206'
ARCHIVE_SHA256 = '38edae6dd347e9bc4afb9f3474285e8458f3d712dd058492f56bacc066ce15e4'


def check_output_location(repo, output, inputs):
    # The relocated workspace intentionally keeps private build products below
    # work/ and outputs/. Never allow source-tree or input-directory overlap.
    if output == repo or repo.is_relative_to(output):
        raise ValueError('Output overlaps source repository')
    if output.is_relative_to(repo) and not any(output.is_relative_to(repo / name) for name in ('work', 'outputs')):
        raise ValueError('Output must stay outside source folders')
    if any(output.is_relative_to(p) or p.is_relative_to(output) for p in inputs):
        raise ValueError('Output overlaps package inputs')


def build(adapter, service, dotnet, python_zip, output, *, dxvk_dll=None, dxvk_license=None, graphics_probe=None, easytier_runtime=None,windows_helpers=None):
    adapter, service, dotnet, python_zip, output = map(content.plain, (adapter, service, dotnet, python_zip, output))
    repo = Path(__file__).resolve().parents[2]
    if output.exists() or not output.parent.is_dir():
        raise ValueError('Output must be a new directory with an existing parent')
    check_output_location(repo, output, (service, dotnet, python_zip.parent, adapter.parent))
    if content.digest(adapter) != ADAPTER_SHA256 or content.digest(python_zip) != PYTHON_SHA256:
        raise ValueError('Adapter or Python distribution fingerprint mismatch')
    if content.digest(service / 'S7Patcher.dll') != SERVICE_SHA256:
        raise ValueError('Service fingerprint does not match the tested LAN build')
    if (dxvk_dll is None) != (dxvk_license is None):
        raise ValueError('DXVK DLL and original license must be supplied together')
    if graphics_probe is not None:
        graphics_probe=content.plain(graphics_probe)
        if dxvk_dll is None or not graphics_probe.is_file():raise ValueError('Desktop graphics payload missing')
        check_output_location(repo,output,(graphics_probe.parent,))
    if dxvk_dll is not None:
        import graphics_profile
        from graphics_distribution import LICENSE_SHA256
        dxvk_dll, dxvk_license = map(content.plain, (dxvk_dll, dxvk_license))
        check_output_location(repo, output, (dxvk_dll.parent, dxvk_license.parent))
        if (content.digest(dxvk_dll) != graphics_profile.DXVK_SHA256
                or content.digest(dxvk_license) != LICENSE_SHA256):
            raise ValueError('DXVK distribution fingerprint mismatch')
    if easytier_runtime is not None:
        from easytier_runtime import verify_directory
        easytier_runtime=verify_directory(easytier_runtime)
        check_output_location(repo,output,(easytier_runtime,))
    if windows_helpers is None:raise ValueError('Compiled Windows helpers are required for new preparation packages')
    windows_helpers=content.plain(windows_helpers)
    check_output_location(repo,output,(windows_helpers,))
    from windows_system import SYSTEM_FILES
    for relative in [*('System/'+name for name in SYSTEM_FILES),'Update/WePlay.UpdateHelper.exe']:
        if not content.plain(windows_helpers/relative).is_file():raise ValueError('Compiled Windows helper missing')
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
    for name in SYSTEM_FILES:shutil.copyfile(windows_helpers/'System'/name,net/name)
    if easytier_runtime is not None:
        shutil.copytree(easytier_runtime,output/'Runtime/easytier')
        verify_directory(output/'Runtime/easytier')
    server = output / 'Payload/Server'; server.mkdir(parents=True)
    for name in ('S7Patcher.dll', 'S7Patcher.deps.json', 'S7Patcher.runtimeconfig.json'):
        shutil.copyfile(service / name, server / name)
    shutil.copyfile(adapter, output / 'Payload/uplay_r1_loader.dll')
    if graphics_probe is not None:
        helpers=output/'Payload/Helpers';helpers.mkdir()
        shutil.copyfile(windows_helpers/'Update/WePlay.UpdateHelper.exe',helpers/'WePlay.UpdateHelper.exe')
    if dxvk_dll is not None:
        profile = output / 'Payload/Graphics'; profile.mkdir()
        shutil.copyfile(dxvk_dll, profile / 'd3d9.dll')
        shutil.copyfile(dxvk_license, profile / 'LICENSE')
        if graphics_probe is not None:shutil.copyfile(graphics_probe,profile/'S7GraphicsProbe.exe')
    names = ['installer/install.py', 'installer/start.py', 'installer/launch_lan.py', 'installer/compatibility.py',
             'offline-ui/launch_local.py', 'offline-ui/patch-spec.json', 'lan/bba.py', 'lan/prepare.py',
             'installer/update_join_menu.py', 'offline-ui/join-menu-update.json']
    names += ['installer/graphics_profile.py', 'installer/graphics_choice.py',
              'installer/Choose-Graphics.ps1']
    names += [original for original, _ in DISPLAY_TOOLS]
    names += [original for original, _ in LAN_LOBBY_TOOLS]
    names += ['installer/lan_public_game.py','offline-ui/lan-public-creation.json']
    names += list(ACHIEVEMENT_PACKAGE_TOOLS)
    if easytier_runtime is not None:
        names += [original for original,_ in INTERNET_TOOLS]
    if dxvk_dll is not None:
        names += ['installer/graphics_distribution.py']
    if graphics_probe is not None:
        names += list(DESKTOP_PACKAGE_TOOLS)+[original for original,_ in DESKTOP_TOOLS]
        names += list(UPDATE_PACKAGE_TOOLS)
        if easytier_runtime is not None:names += list(DISCORD_PACKAGE_TOOLS)
    names += [original for original,_ in WINDOWS_API_TOOLS]
    names = [name for name in names if not name.endswith('.ps1')]
    for name in names:
        target = output / 'Tools' / name; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(repo / 'tools' / name, target)
    shutil.copyfile(repo / 'LICENSE', output / 'Tools/LICENSE.txt')
    write_json(output / 'package.json', {'format': FORMAT,
        'capabilities': {'local_test': True, 'lan_host': False, 'lan_join': False},
        'join_menu_update': True,
        'lan_public_creation': True,
        'graphics_choice': True,
        'display_selection': True,
        'automatic_lan': True,
        'internet_easytier': easytier_runtime is not None,
        'desktop_launcher': graphics_probe is not None,
        'discord_directory': graphics_probe is not None and easytier_runtime is not None,
        'update_patcher': graphics_probe is not None,
        'achievement_notifications_disabled': True,
        'windows_helpers': True,
        'patched_archive_sha256': ARCHIVE_SHA256,
        'files': content.inventory(output, ('Payload', 'Runtime', 'Tools'))})
    (output / 'Installieren.cmd').write_text('@echo off\n"%~dp0Runtime\\python\\python.exe" -B "%~dp0Tools\\installer\\install.py" --package "%~dp0."\npause\n', encoding='ascii')
    (output / 'LIESMICH.txt').write_text(
        'S7 automatische LAN-Lobby: Testpaket v4, Netzwerkrevision 3\n\n'
        'Paket vollstaendig entpacken und Installieren.cmd starten.\n'
        'Den eigenen S7-History-Installationsordner und einen neuen Testordner eingeben.\n'
        'Den Ziel-Unterordner vorher nicht anlegen. Alle bisherigen Korrekturen sind enthalten.\n'
        'Eine separate Kopie benoetigt etwa 7 GB freien Speicher. Das Original bleibt erhalten.\n'
        'Python und .NET sind enthalten; keine Systeminstallation oder Administratorrechte erforderlich.\n\n'
        'Enthalten: bekannte Zusatzinhalte, volle Kampagnenauswahl und fuer diesen Test maximal zwei physische CPU-Kerne ohne SMT.\n'
        'Der Entwicklermodus ist im LAN verbindlich deaktiviert.\n'
        'Revision 3 behebt verlorene Lobby-Anfragen bei verspaeteten Bestaetigungen und schuetzt verschachtelte Anmeldeaufrufe vor Fremdthreads.\n'
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
    if dxvk_dll is not None:
        shutil.copyfile(repo / 'docs/RC-GRAPHICS.md', output / 'START-HIER.txt')
        for name, action in (('Grafikprofil-aktivieren.cmd','install'),('Standardgrafik-wiederherstellen.cmd','restore')):
            (output / name).write_text('@echo off\n"%~dp0Runtime\\python\\python.exe" -B "%~dp0Tools\\installer\\graphics_distribution.py" '
                + action + ' --package "%~dp0."\npause\n', encoding='ascii')
        with (output / 'RUNTIME-SOURCES.txt').open('a', encoding='utf-8') as stream:
            stream.write('\nOriginal DXVK 3.1.1 x32/d3d9.dll (optional, D3D9 to Vulkan)\n'
                'https://github.com/doitsujin/dxvk/releases/tag/v3.1.1\n'
                'SHA256 ' + graphics_profile.DXVK_SHA256 + '\nLicense: Payload/Graphics/LICENSE\n')
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('adapter', 'service', 'dotnet', 'python-zip', 'output'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--dxvk-dll'); parser.add_argument('--dxvk-license')
    parser.add_argument('--graphics-probe')
    parser.add_argument('--easytier-runtime')
    parser.add_argument('--windows-helpers',required=True)
    args = parser.parse_args()
    print(build(args.adapter, args.service, args.dotnet, args.python_zip, args.output,
                dxvk_dll=args.dxvk_dll, dxvk_license=args.dxvk_license,graphics_probe=args.graphics_probe,
                easytier_runtime=args.easytier_runtime,windows_helpers=args.windows_helpers))
