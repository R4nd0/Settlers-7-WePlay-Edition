{ Package files are extracted separately. No overlay into the active Package. }
var
  PackageStageName: String;
  PackageCompleted: Boolean;
  PackagePublished: Boolean;
  PackagePrepared: Boolean;
  PackageVerifierReady: Boolean;
  PackageRetained: TArrayOfString;

function PackageFileAttributes(Path: String): LongWord;
  external 'GetFileAttributesW@kernel32.dll stdcall';

function PlainPackagePath(Path: String): Boolean;
var Parent: String; Attributes: LongWord;
begin
  Result := False;
  while Path <> '' do begin
    Attributes := PackageFileAttributes(Path);
    if (Attributes <> $FFFFFFFF) and ((Attributes and $400) <> 0) then exit;
    Parent := ExtractFileDir(Path);
    if Parent = Path then break;
    Path := Parent;
  end;
  Result := True;
end;

function PackageStage(Param: String): String;
begin
  if PackageStageName = '' then
    PackageStageName := '.weplay-package-' + GetMD5OfString(ExpandConstant('{tmp}'));
  Result := AddBackslash(ExpandConstant('{app}')) + PackageStageName;
end;

procedure PackageTestFault(Point: String);
begin
#ifdef InstallerTest
  if ExpandConstant('{param:WePlayPackageTest|}') = Point then
    RaiseException('Injected private installer test failure: ' + Point);
#endif
end;

function PackageJournal(App: String): String;
begin
  Result := AddBackslash(App) + 'State\package-upgrade.ini';
end;

function ValidPackageSlot(Name: String): Boolean;
var I: Integer;
begin
  Result := False;
  if (Length(Name) <> 48) or (Copy(Name, 1, 16) <> '.weplay-package-') then exit;
  for I := 17 to 48 do
    if Pos(Copy(Name, I, 1), '0123456789abcdef') = 0 then exit;
  Result := True;
end;

function PreservePackageSlot(App, Slot: String): Boolean;
var Folder, Target: String; N: Integer;
begin
  Result := True;
  if not DirExists(Slot) then exit;
  Folder := AddBackslash(App) + 'State\PackageBackups';
  Target := AddBackslash(Folder) + ExtractFileName(Slot);
  Result := PlainPackagePath(Slot) and PlainPackagePath(Folder) and
    (not DirExists(Target)) and ForceDirectories(Folder);
  if Result then begin
    Result := RenameFile(Slot, Target);
    if Result then begin
      N := GetArrayLength(PackageRetained);SetArrayLength(PackageRetained, N + 1);
      PackageRetained[N] := Target;
    end;
  end;
end;

function RecoverPackageUpgrade(App: String): Boolean;
var Journal, Name, Stage, Backup, Current: String;
begin
  Result := False;
  Journal := PackageJournal(App);
  if not PlainPackagePath(App) or not PlainPackagePath(Journal) then exit;
  if not FileExists(Journal) then begin Result := True; exit; end;
  Name := GetIniString('Transaction', 'stage', '', Journal);
  if (GetIniString('Transaction', 'format', '', Journal) <> 'weplay-package-swap-v1') or
      not ValidPackageSlot(Name) then exit;
  Stage := AddBackslash(App) + Name;
  Backup := Stage + '.previous';
  Current := AddBackslash(App) + 'Package';
  if not PlainPackagePath(Stage) or not PlainPackagePath(Backup) or not PlainPackagePath(Current) then exit;
  if DirExists(Backup) and (GetIniString('Transaction', 'phase', '', Journal) <> 'committed') then begin
    if DirExists(Current) then begin
      if DirExists(Stage) then exit;
      if not RenameFile(Current, Stage) then exit;
    end;
    if not RenameFile(Backup, Current) then exit;
  end;
  if not PreservePackageSlot(App, Backup) then exit;
  { A partial/unpublished stage is preserved, never recursively deleted. }
  if not PreservePackageSlot(App, Stage) then exit;
  Result := DeleteFile(Journal);
end;

function PreparePackageUpgrade(App: String): Boolean;
var Journal, Stage: String;
begin
  Result := False;
  if not RecoverPackageUpgrade(App) then exit;
  Stage := PackageStage('');
  if DirExists(Stage) or FileExists(Stage) or not PlainPackagePath(Stage) then exit;
  if not ForceDirectories(AddBackslash(App) + 'State') then exit;
  Journal := PackageJournal(App);
  if not SetIniString('Transaction', 'stage', PackageStageName, Journal) then exit;
  Result := SetIniString('Transaction', 'format', 'weplay-package-swap-v1', Journal);
  PackagePrepared := Result;
end;

function ExtractVerifiedPackage: Boolean;
var App, Python, Helper, Expected: String;
    TemporaryPackage: String;
    ResultCode, I: Integer; Output: TExecOutput; Started: Boolean;
begin
  Result := False;
  PackageVerifierReady := False;
  App := ExpandConstant('{app}');
  ExtractTemporaryFile('WePlay-Package-Check.py');
  ExtractTemporaryFile('WePlay-New-Package.json');
  Helper := ExpandConstant('{tmp}\WePlay-Package-Check.py');
  Expected := ExpandConstant('{tmp}\WePlay-New-Package.json');
  ExtractTemporaryFiles('{app}\Package\*');
  TemporaryPackage := AddBackslash(ExpandConstant('{tmp}')) + '{app}\Package';
#ifdef InstallerTest
  if ExpandConstant('{param:WePlayPackageTest|}') = 'tamper-stage' then
    SaveStringToFile(AddBackslash(TemporaryPackage) + 'Tools\LICENSE.txt', 'injected mismatch', False);
#endif
  Python := AddBackslash(TemporaryPackage) + 'Runtime\python\python.exe';
  Started := ExecAndCaptureOutput(Python, '-I -B "' + Helper +
    '" --package "' + TemporaryPackage + '" --expected "' + Expected +
    '" --app "' + App + '" --stage-to "' + PackageStage('') + '"',
    App, SW_HIDE, ewWaitUntilTerminated, ResultCode, Output);
  for I := 0 to GetArrayLength(Output.StdOut) - 1 do Log(Output.StdOut[I]);
  for I := 0 to GetArrayLength(Output.StdErr) - 1 do Log(Output.StdErr[I]);
  Result := Started and (ResultCode = 0) and not Output.Error;
  PackageVerifierReady := Result;
end;

procedure RetirePreservedPackages;
var Python, Helper, App: String; I, Code: Integer;
begin
  if not PackageVerifierReady then exit;
  App := ExpandConstant('{app}');
  Python := AddBackslash(ExpandConstant('{tmp}')) + '{app}\Package\Runtime\python\python.exe';
  Helper := ExpandConstant('{tmp}\WePlay-Package-Check.py');
  if not FileExists(Python) then exit;
  for I := 0 to GetArrayLength(PackageRetained) - 1 do
    if DirExists(PackageRetained[I]) then
      if not Exec(Python, '-I -B "' + Helper + '" --app "' + App +
          '" --retire "' + PackageRetained[I] + '"', App, SW_HIDE, ewWaitUntilTerminated, Code) then
        Log('Package backup retained for later inspection.');
end;

procedure PublishPackageOnce;
var App, Stage, Current, Backup: String;
begin
  if PackagePublished then exit;
  App := ExpandConstant('{app}'); Stage := PackageStage('');
  Current := AddBackslash(App) + 'Package'; Backup := Stage + '.previous';
  PackageTestFault('before-swap');
  { The staged interpreter has exited before either directory is renamed. }
  if not PlainPackagePath(Stage) or not PlainPackagePath(Current) or
      not PlainPackagePath(Backup) or DirExists(Backup) then
    RaiseException(CustomMessage('PackageFailed'));
  if DirExists(Current) then
    if not RenameFile(Current, Backup) then RaiseException(CustomMessage('PackageBusy'));
  PackageTestFault('after-old-moved');
  if not RenameFile(Stage, Current) then begin
    if DirExists(Backup) then RenameFile(Backup, Current);
    RaiseException(CustomMessage('PackageFailed'));
  end;
  PackageTestFault('after-new-moved');
  PackagePublished := True;
  PackageTestFault('after-final-check');
end;

procedure CompletePackageUpgrade;
var App, Backup: String;
begin
  App := ExpandConstant('{app}'); Backup := PackageStage('') + '.previous';
  PackageCompleted := True;
  if not SetIniString('Transaction', 'phase', 'committed', PackageJournal(App)) then begin
    Log('Verified package installed; completion journal could not be updated. Old backup retained.'); exit;
  end;
  if not PreservePackageSlot(App, Backup) then begin
    Log('Verified package published; old backup retained with recovery journal.'); exit;
  end;
  if not DeleteFile(PackageJournal(App)) then
    Log('Verified package published; committed journal retained.');
  RetirePreservedPackages;
end;

procedure DeinitializeSetup;
begin
  if PackagePrepared and not PackageCompleted and (WizardDirValue <> '') then
    if not RecoverPackageUpgrade(WizardDirValue) then
      Log('Package recovery retained its journal and files for the next setup.');
  RetirePreservedPackages;
end;
