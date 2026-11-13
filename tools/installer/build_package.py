"""Build an offline Windows x64 preparation package, without any game data.

Requires a tested adapter/service, a local .NET 10 runtime and the pinned official
CPython embeddable ZIP. All runtime licenses are included. No downloads here.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compatibility as content
from install import write_json, FORMAT, LAN_LOBBY_TOOLS, DISPLAY_TOOLS, DESKTOP_TOOLS, DESKTOP_PACKAGE_TOOLS, STANDALONE_PACKAGE_TOOLS, INTERNET_TOOLS, DISCORD_PACKAGE_TOOLS, DIRECTORY_PACKAGE_TOOLS, PUBLIC_DIRECTORY_PACKAGE_TOOLS, UPDATE_PACKAGE_TOOLS, ACHIEVEMENT_PACKAGE_TOOLS, CARTOGRAPHER_PACKAGE_TOOLS, MULTIPLAYER_RESUME_TOOLS, WINDOWS_API_TOOLS
from install import DIRECTORY_ADAPTER_SHA256,adapter_fingerprint,NEO_LOGISTICS_TOOLS,MIDSUMMER_PACKAGE_TOOLS

PYTHON_SHA256 = '90905e64a840557d8c1bc4f5b74db3d7ee85e3ffb217611cac1c7813acea4159'
ADAPTER_SHA256 = '28a4d90a97e816b02f7f4d6fbba074fb1c5a6564bf2402712e400e7fb60acfef'
SERVICE_SHA256 = '1b06505d0356520bba3d8b99e363a72417f02f971b98b3cbce844fbce9944438'
ARCHIVE_SHA256 = '1e76e018d79aa39712f142d1f8996a58b1a0b73f438ee709428e5db82609ade3'
DIRECTORY_ARCHIVE_SHA256 = '6541f49185b0e37835fd1187378d09a001015046e5b667e7921bc7ae282a0223'
NEO_ARCHIVE_SHA256='7d2d0e91d014810ba0ce8eaa2f31ba2970abd0d8b7b61b4b98dd0c0fb9dde507'
NEO_DIRECTORY_ARCHIVE_SHA256='67cbd87bf33981384659b26d4c96d802d4a2a550e16381901e738541895a4e43'
MIDSUMMER_DIRECTORY_ARCHIVE_SHA256='2fbf4a98a89ac735b3930995a1ff92331eb4d02958e46bf580458d0f4c899f68'


def check_output_location(repo, output, inputs):
    # The relocated workspace intentionally keeps private build products below
    # work/ and outputs/. Never allow source-tree or input-directory overlap.
    if output == repo or repo.is_relative_to(output):
        raise ValueError('Output overlaps source repository')
    if output.is_relative_to(repo) and not any(output.is_relative_to(repo / name) for name in ('work', 'outputs')):
        raise ValueError('Output must stay outside source folders')
    if any(output.is_relative_to(p) or p.is_relative_to(output) for p in inputs):
        raise ValueError('Output overlaps package inputs')


def build(adapter, service, dotnet, python_zip, output, *, dxvk_dll=None, dxvk_license=None, graphics_probe=None, memory_scanner=None, easytier_runtime=None,windows_helpers=None,game_directory=False,neo_logistics=False,midsummer_profile=False,atmosphere_addon=None,platform_runtime=None):
    adapter, service, dotnet, python_zip, output = map(content.plain, (adapter, service, dotnet, python_zip, output))
    repo = Path(__file__).resolve().parents[2]
    if output.exists() or not output.parent.is_dir():
        raise ValueError('Output must be a new directory with an existing parent')
    if type(game_directory) is not bool:raise ValueError('Invalid directory capability')
    if type(neo_logistics) is not bool:raise ValueError('Invalid NEO-Logistics capability')
    if type(midsummer_profile) is not bool or (midsummer_profile and (not neo_logistics or not game_directory or dxvk_dll is None)):
        raise ValueError('Mittsommer requires the current NEO, directory and Modern graphics package')
    if midsummer_profile:
        import reshade_runtime
        if atmosphere_addon is None:raise ValueError('Mittsommer requires the tested read-only atmosphere clock')
        atmosphere_addon=content.plain(atmosphere_addon)
        if content.digest(atmosphere_addon)!=reshade_runtime.CLOCK_SHA256:raise ValueError('Atmosphere clock fingerprint mismatch')
        check_output_location(repo,output,(atmosphere_addon.parent,))
    elif atmosphere_addon is not None:
        raise ValueError('Atmosphere clock requires the Mittsommer graphics package')
    if game_directory:
        if graphics_probe is None or easytier_runtime is None or windows_helpers is None:raise ValueError('Directory requires desktop, EasyTier and current Windows helpers')
        helper=content.plain(Path(windows_helpers)/'System/WePlay.WindowsHelper.dll')
        probe=subprocess.run([str(dotnet/'dotnet.exe'),str(helper),'capabilities'],
            capture_output=True,timeout=30)
        if probe.returncode or json.loads(probe.stdout.decode('utf-8-sig'))!={'format':'weplay-windows-capabilities-v1','directory_crypto':1,'internet_firewall':2}:
            raise ValueError('Directory cryptography or restricted Internet firewall helper is missing')
    check_output_location(repo, output, (service, dotnet, python_zip.parent, adapter.parent))
    if platform_runtime is not None:
        import platform_standalone
        platform_runtime=platform_standalone.payload(platform_runtime)
        check_output_location(repo,output,(platform_runtime,))
    if content.digest(adapter) != adapter_fingerprint(game_directory,neo_logistics,platform_runtime is not None) or content.digest(python_zip) != PYTHON_SHA256:
        raise ValueError('Adapter or Python distribution fingerprint mismatch')
    if content.digest(service / 'S7Patcher.dll') != SERVICE_SHA256:
        raise ValueError('Service fingerprint does not match the tested LAN build')
    if (dxvk_dll is None) != (dxvk_license is None):
        raise ValueError('DXVK DLL and original license must be supplied together')
    if graphics_probe is not None:
        graphics_probe=content.plain(graphics_probe)
        if dxvk_dll is None or not graphics_probe.is_file():raise ValueError('Desktop graphics payload missing')
        check_output_location(repo,output,(graphics_probe.parent,))
        if memory_scanner is None:raise ValueError('Pinned native memory scanner is required for desktop packages')
        sys.path.insert(0,str(repo/'tools/d3d11'))
        import memory_guard
        memory_scanner=content.plain(memory_scanner)
        memory_guard.verify_scanner(memory_scanner)
        check_output_location(repo,output,(memory_scanner.parent,))
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
    version = '10.0.15'
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
    if midsummer_profile:
        clock=output/'Runtime/Graphics';clock.mkdir()
        shutil.copyfile(atmosphere_addon,clock/'WePlayAtmosphere.addon32')
        shutil.copyfile(repo/'tools/midsummer/effects/Atmosphere-LICENSE.txt',clock/'LICENSE.txt')
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
    if platform_runtime is not None:
        folder=output/'Payload/Platform';folder.mkdir()
        for name in platform_standalone.PAYLOAD:shutil.copyfile(platform_runtime/name,folder/name)
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
    names += list(CARTOGRAPHER_PACKAGE_TOOLS)
    names += list(MULTIPLAYER_RESUME_TOOLS)
    if neo_logistics:names+=list(NEO_LOGISTICS_TOOLS)
    if midsummer_profile:names+=list(MIDSUMMER_PACKAGE_TOOLS)
    if easytier_runtime is not None:
        names += [original for original,_ in INTERNET_TOOLS]
    if dxvk_dll is not None:
        names += ['installer/graphics_distribution.py']
    if graphics_probe is not None:
        names += list(DESKTOP_PACKAGE_TOOLS)+[original for original,_ in DESKTOP_TOOLS]
        names += list(STANDALONE_PACKAGE_TOOLS)
        names += list(UPDATE_PACKAGE_TOOLS)
        if easytier_runtime is not None:names += list(DISCORD_PACKAGE_TOOLS)
        if game_directory:names += list(DIRECTORY_PACKAGE_TOOLS)+list(PUBLIC_DIRECTORY_PACKAGE_TOOLS)
    names += [original for original,_ in WINDOWS_API_TOOLS]
    names += ['installer/portable_lock.py', 'installer/host_runtime.py']
    if platform_runtime is not None:names+=['installer/'+name for name in platform_standalone.TOOLS]
    names = [name for name in names if not name.endswith('.ps1')]
    for name in names:
        target = output / 'Tools' / name; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(repo / 'tools' / name, target)
    if graphics_probe is not None:
        shutil.copyfile(memory_scanner,output/'Tools/d3d11/memory_scan.dll')
    shutil.copyfile(repo / 'LICENSE', output / 'Tools/LICENSE.txt')
    if neo_logistics:
        licenses=output/'Tools/Licenses';licenses.mkdir(exist_ok=True)
        shutil.copyfile(repo/'native/third_party/puff/puff.h',licenses/'NEO-Logistics-puff.txt')
        shutil.copyfile(repo/'tools/lan/LICENSE.S7Packer.txt',licenses/'NEO-Logistics-S7Packer.txt')
    write_json(output / 'package.json', {'format': FORMAT,
        'platform_independent':platform_runtime is not None,
        'steam_history': True,
        'multiplayer_castles': True,
        'capabilities': {'local_test': True, 'lan_host': False, 'lan_join': False},
        'join_menu_update': True,
        'lan_public_creation': True,
        'graphics_choice': True,
        'display_selection': True,
        'automatic_lan': True,
        'internet_easytier': easytier_runtime is not None,
        'desktop_launcher': graphics_probe is not None,
        'standalone_export': graphics_probe is not None,
        'discord_directory': graphics_probe is not None and easytier_runtime is not None,
        'game_directory':game_directory,
        'public_game_directory':game_directory,
        'neo_logistics':neo_logistics,
        'midsummer_profile':midsummer_profile,
        'update_patcher': graphics_probe is not None,
        'achievement_notifications_disabled': True,
        'cartographer_context': True,
        'multiplayer_resume': True,
        'windows_helpers': True,
        'patched_archive_sha256': MIDSUMMER_DIRECTORY_ARCHIVE_SHA256 if midsummer_profile else ((NEO_DIRECTORY_ARCHIVE_SHA256 if game_directory else NEO_ARCHIVE_SHA256) if neo_logistics else (DIRECTORY_ARCHIVE_SHA256 if game_directory else ARCHIVE_SHA256)),
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
        '.NET 10.0.15 Windows x64 runtime from the development SDK runtime.\n'
        'Licenses: Runtime/dotnet/LICENSE.txt and ThirdPartyNotices.txt\n', encoding='utf-8')
    if neo_logistics:
        with (output/'LIESMICH.txt').open('a',encoding='utf-8') as stream:
            stream.write('\nNEO-Logistics (experimentell): In der Kartenkonfiguration einschalten. '
                'FIFO richtet sich nach dem Auftragsalter. Gruppenprioritaeten sind deaktiviert; '
                'Einzelprioritaeten bleiben aktiv. Im Mehrspieler benoetigen alle denselben Kandidaten. '
                'Einzelspieler-Speichern/Laden ist geprueft. Mehrspieler-Prioritaeten werden wiederhergestellt; der nachgewiesene KI-Desync nach dem Laden ist korrigiert. Langzeit- und separate-PC-Tests bleiben begrenzt.\n')
    if platform_runtime is not None:
        with (output/'LIESMICH.txt').open('a',encoding='utf-8') as stream:
            stream.write('\nPlattformunabhängig: Steam/Ubisoft Connect werden nicht benötigt. Die reguläre Steam-EXE bleibt auf Platte unverändert. '
                'Die Windows-Netzisolation beim ersten Start dieser Kopie bestätigen. Spielstände und Einstellungen bleiben '
                'in der Kopie unter LocalData/ProfileRoot. Für WePlay-LAN werden nur aktuelle private Mitspieler/Spielports '
                'zeitlich begrenzt freigegeben. Nach Verschieben der Kopie die Netzisolation am neuen Pfad bestätigen.\n')
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
    parser.add_argument('--memory-scanner')
    parser.add_argument('--easytier-runtime')
    parser.add_argument('--windows-helpers',required=True)
    parser.add_argument('--game-directory',action='store_true')
    parser.add_argument('--neo-logistics',action='store_true',help='Include the experimental NEO-Logistics map mode (matching adapter required)')
    parser.add_argument('--midsummer-profile',action='store_true',help='Include optional bright twilight/day lighting and ReShade tone support')
    parser.add_argument('--atmosphere-addon',help='Tested x86 read-only sun-clock bridge (required for Mittsommer)')
    parser.add_argument('--platform-runtime',help='Pinned platform-independent SDK, entry gates and Windows filtering helper')
    args = parser.parse_args()
    print(build(args.adapter, args.service, args.dotnet, args.python_zip, args.output,
                dxvk_dll=args.dxvk_dll, dxvk_license=args.dxvk_license,graphics_probe=args.graphics_probe,memory_scanner=args.memory_scanner,
                easytier_runtime=args.easytier_runtime,windows_helpers=args.windows_helpers,game_directory=args.game_directory,neo_logistics=args.neo_logistics,midsummer_profile=args.midsummer_profile,atmosphere_addon=args.atmosphere_addon,platform_runtime=args.platform_runtime))
