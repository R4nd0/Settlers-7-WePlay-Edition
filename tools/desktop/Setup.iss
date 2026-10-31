#ifndef Stage
  #error Stage must point to the prepared desktop application
#endif
#ifndef Output
  #error Output must point to an existing output directory
#endif

[Setup]
AppId={{B66B3FA3-165A-4B0E-ACAA-771C64367317}
AppName=Settlers 7: WePlay Edition
AppVersion=2.0.0-rc3.7.1
AppVerName=Settlers 7: WePlay Edition RC3.7.1 Maintenance
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
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
DisableFinishedPage=yes
ShowLanguageDialog=no
UsePreviousAppDir=yes
AppMutex=S7Launcher-Install
CloseApplications=no
RestartApplications=no
UninstallDisplayIcon={app}\S7Launcher.exe
SetupLogging=yes
SetupIconFile=..\..\assets\branding\weplay.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Run]
Filename: "{app}\S7Launcher.exe"; Parameters: "--setup"; Flags: nowait skipifsilent runasoriginaluser

[CustomMessages]
english.UninstallBusy=The managed game files could not be removed safely. Close the game and launcher, then try again. Saved games have been kept.
german.UninstallBusy=Die verwalteten Spieldateien konnten nicht sicher entfernt werden. Beende Spiel und Starter und versuche es erneut. Spielstände bleiben erhalten.

[Code]
function InitializeUninstall(): Boolean;
var ResultCode: Integer;
begin
  Result := Exec(ExpandConstant('{app}\S7Launcher.exe'), '--uninstall --quiet',
                 ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := Result and (ResultCode = 0);
  if not Result then
    SuppressibleMsgBox(CustomMessage('UninstallBusy'), mbError, MB_OK, IDOK);
end;
