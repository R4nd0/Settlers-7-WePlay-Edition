{ Portable discovery is structural; the existing hash-checked package performs
  the full preflight before any launcher file is replaced. }
const
#ifdef InstallerTest
  PortableRegistry = 'Software\WePlay Community\StandaloneInstallerPrivateTest';
#else
  PortableRegistry = 'Software\WePlay Community\Settlers7WePlay';
#endif
var
  PortableNotice: TNewStaticText;
  PortableBrowseButton: TNewButton;
  PreparedPortable: String;

function PortableApp(const Folder: String): String;
var Candidate: String;
begin
  Result := '';
  if Folder = '' then exit;
  Candidate := RemoveBackslashUnlessRoot(ExpandFileName(Folder));
  if FileExists(AddBackslash(Candidate) + 'standalone.json') then
    Result := AddBackslash(Candidate) + 'App'
  else if (CompareText(ExtractFileName(Candidate), 'App') = 0) and
          FileExists(AddBackslash(ExtractFileDir(Candidate)) + 'standalone.json') then
    Result := Candidate;
end;

function ExplicitDirectory: Boolean;
var I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(Copy(ParamStr(I), 1, 5), '/DIR=') = 0 then begin
      Result := True;
      exit;
    end;
end;

function ShortcutPortable(const Filename: String): String;
var Shell, Link: Variant;
begin
  Result := '';
  if not FileExists(Filename) then exit;
  try
    Shell := CreateOleObject('WScript.Shell');
    Link := Shell.CreateShortcut(Filename);
    if CompareText(ExtractFileName(Link.TargetPath), 'S7Launcher.exe') = 0 then
      Result := PortableApp(ExtractFileDir(Link.TargetPath));
  except
    Log('Standalone shortcut could not be inspected.');
  end;
end;

function DiscoverPortable: String;
var Stored: String;
begin
  Result := PortableApp(ExpandConstant('{src}'));
  if Result <> '' then exit;
  Result := PortableApp(ExtractFileDir(ExpandConstant('{src}')));
  if Result <> '' then exit;
  if RegQueryStringValue(HKCU, PortableRegistry, 'LastStandaloneApp', Stored) then
    Result := PortableApp(Stored);
  if Result <> '' then exit;
  Result := ShortcutPortable(ExpandConstant('{userdesktop}\Settlers 7 WePlay Edition.lnk'));
  if Result <> '' then exit;
  Result := ShortcutPortable(ExpandConstant('{userprograms}\Settlers 7 WePlay Edition.lnk'));
end;

procedure UpdatePortableNotice(Sender: TObject);
var App: String;
begin
  App := PortableApp(WizardForm.DirEdit.Text);
  if App = '' then
    PortableNotice.Caption := CustomMessage('PortableHint')
  else
    PortableNotice.Caption := CustomMessage('PortableFound') + #13#10 + App;
end;

procedure BrowsePortable(Sender: TObject);
var Folder, App: String;
begin
  Folder := WizardForm.DirEdit.Text;
  if BrowseForFolder(CustomMessage('PortableHint'), Folder, False) then begin
    App := PortableApp(Folder);
    if App <> '' then begin
      WizardForm.DirEdit.Text := App;
      UpdatePortableNotice(nil);
    end
    else SuppressibleMsgBox(CustomMessage('PortableHint'), mbError, MB_OK, IDOK);
  end;
end;

procedure InitializeWizard;
var App: String;
begin
  PortableBrowseButton := TNewButton.Create(WizardForm);
  PortableBrowseButton.Parent := WizardForm.SelectDirPage;
  PortableBrowseButton.SetBounds(WizardForm.DirEdit.Left,
    WizardForm.DirEdit.Top + WizardForm.DirEdit.Height + ScaleY(14),
    WizardForm.DirEdit.Width + WizardForm.DirBrowseButton.Width + ScaleX(8), ScaleY(28));
  PortableBrowseButton.Caption := CustomMessage('PortableBrowse');
  PortableBrowseButton.OnClick := @BrowsePortable;
  PortableNotice := TNewStaticText.Create(WizardForm);
  PortableNotice.Parent := WizardForm.SelectDirPage;
  PortableNotice.SetBounds(PortableBrowseButton.Left,
    PortableBrowseButton.Top + PortableBrowseButton.Height + ScaleY(10),
    PortableBrowseButton.Width, ScaleY(80));
  PortableNotice.AutoSize := False;
  PortableNotice.WordWrap := True;
  { A signed updater's explicit /DIR always wins over discovery. }
  if not ExplicitDirectory then begin
    App := DiscoverPortable;
    if App <> '' then WizardForm.DirEdit.Text := App;
  end;
  UpdatePortableNotice(nil);
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpSelectDir then UpdatePortableNotice(nil);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var App: String;
begin
  Result := True;
  if CurPageID = wpSelectDir then begin
    App := PortableApp(WizardForm.DirEdit.Text);
    if App <> '' then WizardForm.DirEdit.Text := App;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var App, Root, Python, Helper: String;
    ResultCode, I: Integer;
    Output: TExecOutput;
    Started: Boolean;
begin
  Result := '';
  App := PortableApp(WizardDirValue);
  if App <> '' then WizardForm.DirEdit.Text := App;
  if not RecoverPackageUpgrade(WizardDirValue) then begin
    Result := CustomMessage('PackageFailed'); exit;
  end;
  if App = '' then begin
    if not PreparePackageUpgrade(WizardDirValue) then Result := CustomMessage('PackageFailed');
    if Result = '' then
      try
        if not ExtractVerifiedPackage then Result := CustomMessage('PackageFailed');
      except Result := CustomMessage('PackageFailed'); end;
    exit;
  end;
  if App = PreparedPortable then begin
    if not PreparePackageUpgrade(WizardDirValue) then Result := CustomMessage('PackageFailed');
    if Result = '' then
      try
        if not ExtractVerifiedPackage then Result := CustomMessage('PackageFailed');
      except Result := CustomMessage('PackageFailed'); end;
    exit;
  end;
  Root := ExtractFileDir(App);
  Python := AddBackslash(App) + 'Package\Runtime\python\python.exe';
  if not FileExists(Python) then begin
    Result := CustomMessage('PortableFailed');
    exit;
  end;
  WizardForm.PreparingLabel.Caption := CustomMessage('PortableChecking');
  ExtractTemporaryFile('WePlay-Standalone-Preflight.py');
  Helper := ExpandConstant('{tmp}\WePlay-Standalone-Preflight.py');
  Started := False;
  try
    Started := ExecAndCaptureOutput(Python,
      '-I -B "' + Helper + '" --root "' + AddBackslash(Root) + '." --check-only',
      Root, SW_SHOWNORMAL, ewWaitUntilTerminated, ResultCode, Output);
    for I := 0 to GetArrayLength(Output.StdOut) - 1 do Log(Output.StdOut[I]);
    for I := 0 to GetArrayLength(Output.StdErr) - 1 do Log(Output.StdErr[I]);
  except
    Log('Standalone preflight could not be started.');
  end;
  if not Started then Result := CustomMessage('PortableFailed')
  else if ResultCode = 3 then Result := CustomMessage('PortableBusy')
  else if (ResultCode <> 0) or Output.Error then Result := CustomMessage('PortableFailed')
  else begin
    PreparedPortable := App;
    Log('Existing standalone binding prepared before launcher replacement.');
  end;
  if (Result = '') and not PreparePackageUpgrade(WizardDirValue) then
    Result := CustomMessage('PackageFailed');
  if Result = '' then
    try
      if not ExtractVerifiedPackage then Result := CustomMessage('PackageFailed');
    except Result := CustomMessage('PackageFailed'); end;
end;

function IsPortableTarget: Boolean;
begin
  Result := PortableApp(ExpandConstant('{app}')) <> '';
end;

function StandaloneRoot(Param: String): String;
begin
  Result := ExtractFileDir(ExpandConstant('{app}'));
end;

procedure CurStepChanged(CurStep: TSetupStep);
var App: String;
begin
  if CurStep = ssInstall then
    try PublishPackageOnce;
    except
      Log('Package publication aborted before normal file installation: ' + GetExceptionMessage);
      Abort;
    end;
  if CurStep = ssPostInstall then begin
    CompletePackageUpgrade;
  end;
  if CurStep = ssDone then begin
    App := PortableApp(ExpandConstant('{app}'));
    if App <> '' then
      RegWriteStringValue(HKCU, PortableRegistry, 'LastStandaloneApp', App);
  end;
end;
