#ifndef Stage
  #error Stage must point to the prepared desktop application
#endif
#ifndef Output
  #error Output must point to an existing output directory
#endif

[Setup]
#ifdef InstallerTest
AppId=WePlay-Standalone-Installer-Private-Test
Uninstallable=no
#else
AppId={{B66B3FA3-165A-4B0E-ACAA-771C64367317}
#endif
AppName=Settlers 7: WePlay Edition
AppVersion=2.0.0-rc3.20
#ifdef InstallerTest
AppVerName=WePlay Installer TEST - RC3.20
#else
AppVerName=Settlers 7: WePlay Edition RC3.20 Public Lobby Hotfix
#endif
AppPublisher=WePlay Community
AppPublisherURL=https://github.com/R4nd0/Settlers-7-WePlay-Edition
DefaultDirName={localappdata}\Settlers7WePlay
DefaultGroupName=Settlers 7 WePlay Edition
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
MinVersion=10.0.19045
OutputDir={#Output}
OutputBaseFilename=Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableWelcomePage=yes
DisableDirPage=no
AppendDefaultDirName=no
DisableProgramGroupPage=yes
DisableReadyPage=yes
DisableFinishedPage=yes
ShowLanguageDialog=no
UsePreviousAppDir=yes
AppMutex=S7Launcher-Install
SetupMutex=Local\S7Launcher-Setup
CloseApplications=no
RestartApplications=no
UninstallDisplayIcon={app}\S7Launcher.exe
SetupLogging=yes
SetupIconFile=..\..\assets\branding\weplay.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"

[Files]
Source: "check_package.py"; DestName: "WePlay-Package-Check.py"; Flags: dontcopy
Source: "{#Stage}\Package\package.json"; DestName: "WePlay-New-Package.json"; Flags: dontcopy
Source: "{#Stage}\Package\Tools\installer\standalone.py"; DestName: "WePlay-Standalone-Preflight.py"; Flags: dontcopy
; PrepareToInstall extracts and verifies this same entry before any installed
; file is written. Normal copying retains Inno rollback/uninstall registration.
Source: "{#Stage}\Package\*"; DestDir: "{app}\Package"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\*"; DestDir: "{app}"; Excludes: "Package\*"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\Package\Tools\installer\standalone.py"; DestDir: "{code:StandaloneRoot}"; DestName: "standalone.py"; Check: IsPortableTarget; Flags: ignoreversion

[Run]
Filename: "{app}\S7Launcher.exe"; Parameters: "--setup"; Flags: nowait skipifsilent runasoriginaluser

[CustomMessages]
english.UninstallBusy=The managed game files could not be removed safely. Close the game and launcher, then try again. Saved games have been kept.
german.UninstallBusy=Die verwalteten Spieldateien konnten nicht sicher entfernt werden. Beende Spiel und Starter und versuche es erneut. Spielstände bleiben erhalten.
english.PortableBrowse=Select existing standalone copy...
german.PortableBrowse=Vorhandene Standalone-Kopie auswählen...
english.PortableHint=To update a portable copy, choose its outer folder (with standalone.json) or its App folder. The existing game and saves are retained; no original installation is needed.
german.PortableHint=Zum Aktualisieren einer portablen Kopie ihren äußeren Ordner (mit standalone.json) oder den App-Ordner auswählen. Spielkopie und Spielstände bleiben erhalten; eine Originalinstallation ist nicht nötig.
english.PortableFound=Existing standalone copy detected. Its launcher will be updated here:
german.PortableFound=Vorhandene Standalone-Kopie erkannt. Ihr Launcher wird hier aktualisiert:
english.PortableChecking=Checking the existing standalone copy before updating its launcher...
german.PortableChecking=Vorhandene Standalone-Kopie wird vor dem Launcher-Update geprüft...
english.PortableFailed=The existing standalone copy could not be prepared. Close WePlay and S7, and make sure the complete export was extracted. No launcher files have been replaced.
german.PortableFailed=Die vorhandene Standalone-Kopie konnte nicht vorbereitet werden. WePlay und S7 schließen und prüfen, ob der vollständige Export entpackt wurde. Es wurden keine Launcher-Dateien ersetzt.
english.PortableBusy=This standalone copy is currently being updated or used by another WePlay operation. Wait for it to finish, then retry. No launcher files have been replaced.
german.PortableBusy=Diese Standalone-Kopie wird gerade aktualisiert oder von einem anderen WePlay-Vorgang verwendet. Den Vorgang beenden lassen und dann erneut versuchen. Es wurden keine Launcher-Dateien ersetzt.
english.PackageFailed=The new package could not be verified or published safely. The previous package and game data have been kept. Retry Setup; retained staging files will not be mixed into the active package.
german.PackageFailed=Das neue Paket konnte nicht sicher geprüft oder übernommen werden. Das bisherige Paket und die Spieldaten bleiben erhalten. Setup erneut ausführen; zurückbehaltene Dateien werden nicht mit dem aktiven Paket vermischt.
english.PackageBusy=The existing package is still in use. Close WePlay, then retry Setup. The new package has not replaced it.
german.PackageBusy=Das bisherige Paket wird noch verwendet. WePlay schließen und Setup erneut versuchen. Das neue Paket hat es nicht ersetzt.

[Code]
#include "PackageSetup.iss"
#include "StandaloneSetup.iss"

function InitializeUninstall(): Boolean;
var ResultCode: Integer;
begin
  Result := Exec(ExpandConstant('{app}\S7Launcher.exe'), '--uninstall --quiet',
                 ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := Result and (ResultCode = 0);
  if not Result then
    SuppressibleMsgBox(CustomMessage('UninstallBusy'), mbError, MB_OK, IDOK);
end;
