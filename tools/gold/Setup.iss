#ifndef Stage
  #error Stage must point to the verified Gold application package
#endif
#ifndef Output
  #error Output must point to a new Gold output directory
#endif

[Setup]
AppId={{CC6101DC-57C6-40A4-9894-A40DAF968624}
AppName=Settlers 7: WePlay Gold
AppVersion=0.1.0
AppVerName=Settlers 7: WePlay Gold – Einzelspieler
AppPublisher=WePlay Community
AppPublisherURL=https://github.com/R4nd0/Settlers-7-WePlay-Edition
DefaultDirName={localappdata}\Settlers7WePlayGold
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
MinVersion=10.0.19045
OutputDir={#Output}
OutputBaseFilename=WePlay-Gold-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableDirPage=no
AppendDefaultDirName=no
DisableProgramGroupPage=yes
DisableWelcomePage=yes
UsePreviousAppDir=no
CloseApplications=no
RestartApplications=no
AppMutex=WePlayGoldLauncher
SetupMutex=Local\WePlayGoldSetup
SetupLogging=yes
SetupIconFile=..\..\assets\branding\weplay.ico
UninstallDisplayIcon={app}\weplay.ico

[Languages]
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\assets\branding\weplay.ico"; DestDir: "{app}"; Flags: ignoreversion; AfterInstall: PrepareGoldGame

[Tasks]
Name: "desktopicon"; Description: "Desktop-Verknüpfung erstellen"; Flags: unchecked

[Icons]
Name: "{autoprograms}\WePlay Gold"; Filename: "{app}\Runtime\python\pythonw.exe"; Parameters: "-B ""{app}\Tools\gold\start.py"" --app ""{app}"""; WorkingDir: "{app}"; IconFilename: "{app}\weplay.ico"
Name: "{autodesktop}\WePlay Gold"; Filename: "{app}\Runtime\python\pythonw.exe"; Parameters: "-B ""{app}\Tools\gold\start.py"" --app ""{app}"""; WorkingDir: "{app}"; IconFilename: "{app}\weplay.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\Runtime\python\pythonw.exe"; Parameters: "-B ""{app}\Tools\gold\start.py"" --app ""{app}"""; Description: "WePlay Gold starten"; Flags: postinstall nowait skipifsilent

[Code]
var
  SourcePage: TInputDirWizardPage;
  PreparationFailed: Boolean;

function GoldRoot(const Folder: String): String;
begin
  Result := Folder;
  if not FileExists(Result + '\Data\Base\_Dbg\Bin\Release\Settlers7R.exe') then
    Result := Folder + '\Settlers 7';
end;

function SourceError(): String;
var Source, Target: String;
begin
  Result := '';
  Source := AddBackslash(ExpandFileName(GoldRoot(SourcePage.Values[0])));
  Target := AddBackslash(ExpandFileName(ExpandConstant('{app}')));
  if (Pos(Lowercase(Source), Lowercase(Target)) = 1) or
     (Pos(Lowercase(Target), Lowercase(Source)) = 1) then begin
    Result := 'Originalinstallation und WePlay-Ziel müssen getrennte Ordner sein.';
    Exit;
  end;
  if DirExists(ExpandConstant('{app}\Game')) then begin
    Result := 'Hier existiert bereits eine Gold-Spielkopie. Bitte einen neuen Zielordner wählen.';
    Exit;
  end;
  if not FileExists(Source + 'Data\Base\_Dbg\Bin\Release\Settlers7R.exe') then
    Result := 'Bitte die unveränderte Gold-Originalinstallation auswählen.'
  else if GetSHA256OfFile(Source + 'Data\Base\_Dbg\Bin\Release\Settlers7R.exe') <>
    'ec27c7fab2c83a1444f043eb4730f0065393ca2e91607f71e5fd78ee5a5c7c94' then
    Result := 'Diese Gold-EXE ist verändert oder wird nicht unterstützt. Benötigt wird Gold 1.12.1396.'
  else if not FileExists(Source + 'Data\Base\BBA\Interface.bba') then
    Result := 'Das Gold-Interface-Archiv fehlt.'
  else if GetSHA256OfFile(Source + 'Data\Base\BBA\Interface.bba') <>
    '5f335b7ebdf0707079009abda7334ecf5d83cfd3039776d2e775f39718255215' then
    Result := 'Das Gold-Interface-Archiv ist verändert oder wird nicht unterstützt.';
end;

procedure InitializeWizard();
begin
  SourcePage := CreateInputDirPage(wpSelectDir, 'Gold-Original auswählen',
    'Siedler 7 Gold Edition 1.12.1396',
    'Die Originalinstallation bleibt unverändert. Setup erstellt eine neue Kopie mit bereinigtem Interface und allen Kampagnenkarten.',
    False, '');
  SourcePage.Add('Originalinstallation (oder S7Gold-Ordner):');
  SourcePage.Values[0] := ExpandConstant('{param:SOURCE|}');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var Error: String;
begin
  Result := True;
  { Silent installs use the same validation in PrepareToInstall; no modal page dialogs. }
  if WizardSilent then Exit;
  if CurPageID = wpSelectDir then begin
    if DirExists(ExpandConstant('{app}\Game')) then begin
      MsgBox('Hier existiert bereits eine Gold-Spielkopie. Bitte einen neuen Zielordner wählen.', mbError, MB_OK);
      Result := False;
    end;
  end;
  if CurPageID = SourcePage.ID then begin
    Error := SourceError();
    Result := Error = '';
    if not Result then MsgBox(Error, mbError, MB_OK);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  { Also enforce these checks for silent installs, before any app file changes. }
  Result := SourceError();
end;

procedure PrepareGoldGame();
var Code: Integer;
begin
  PreparationFailed := True;
  WizardForm.StatusLabel.Caption := 'Gold wird kopiert, geprüft und bereinigt. Dies kann einige Minuten dauern ...';
  if not Exec(ExpandConstant('{app}\Runtime\python\pythonw.exe'),
    '-B "' + ExpandConstant('{app}\Tools\gold\installer.py') + '" --quiet --app "' + ExpandConstant('{app}') +
    '" --source "' + SourcePage.Values[0] + '"', ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, Code) then
    RaiseException('Die Gold-Vorbereitung konnte nicht gestartet werden.');
  if (Code <> 0) or not FileExists(ExpandConstant('{app}\Game\gold-install.json')) then begin
    RaiseException('Die Gold-Vorbereitung wurde abgebrochen. Die Originalinstallation wurde nicht verändert.');
  end;
  PreparationFailed := False;
end;

function GetCustomSetupExitCode(): Integer;
begin
  Result := 0;
  if PreparationFailed then Result := 2;
end;
