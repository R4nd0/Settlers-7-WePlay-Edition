"""Run a tiny Inno fixture against the real package-swap code."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import uuid


ROOT = Path(__file__).resolve().parents[1]
COMPILER = ROOT/'work/build-tools/InnoSetup7/ISCC.exe'


@unittest.skipUnless(os.name == 'nt' and COMPILER.is_file(), 'local Inno Setup compiler')
class PackageSetupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        scratch = ROOT/'work'
        temporary = tempfile.TemporaryDirectory(prefix='inno-package-audit-', dir=scratch)
        cls.addClassCleanup(temporary.cleanup)
        cls.root = Path(temporary.name)
        fixture_file = cls.root/'fixture.txt'
        fixture_file.write_text('small authored fixture', encoding='ascii')
        include = ROOT/'tools/desktop/PackageSetup.iss'
        script = cls.root/'Fixture.iss'
        script.write_text(f'''#define InstallerTest
[Setup]
AppId=WePlay-Package-Audit-{uuid.uuid4().hex}
AppName=WePlay package audit fixture
AppVersion=1.0
DefaultDirName={cls.root/'App'}
OutputDir={cls.root}
OutputBaseFilename=Fixture
PrivilegesRequired=lowest
Uninstallable=no
CreateUninstallRegKey=no
UsePreviousAppDir=no
CloseApplications=no
RestartApplications=no
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
DisableFinishedPage=yes
[Files]
Source: "{fixture_file}"; DestDir: "{{app}}"; DestName: "published-launcher.fixture"
Source: "{fixture_file}"; DestName: "WePlay-Package-Check.py"; Flags: dontcopy
Source: "{fixture_file}"; DestName: "WePlay-New-Package.json"; Flags: dontcopy
[CustomMessages]
PackageFailed=Package failed.
PackageBusy=Package busy.
[Code]
#include "{include}"
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := 'Package fixture preparation failed';
  if not PreparePackageUpgrade(WizardDirValue) then exit;
  if not ForceDirectories(PackageStage('')) then exit;
  if not SaveStringToFile(AddBackslash(PackageStage('')) + 'payload.fixture', 'incoming payload', False) then exit;
  Result := '';
end;
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    try PublishPackageOnce;
    except Abort; end;
  if CurStep = ssPostInstall then CompletePackageUpgrade;
end;
''', encoding='utf-8')
        compiled = subprocess.run([str(COMPILER),'/Q',str(script)], capture_output=True, timeout=45)
        if compiled.returncode:
            raise AssertionError(compiled.stdout.decode(errors='replace')+compiled.stderr.decode(errors='replace'))

    def run_fixture(self, app, fault=None):
        args = [str(self.root/'Fixture.exe'),'/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',
                '/DIR='+str(app),'/LOG='+str(self.root/(app.name+'.log'))]
        if fault:
            args.append('/WePlayPackageTest='+fault)
        return subprocess.run(args, capture_output=True, timeout=30)

    def test_failed_fresh_publication_never_leaves_a_package_active(self):
        app = self.root/'fresh'
        (app/'State').mkdir(parents=True)
        (app/'State/keep.txt').write_text('preferences', encoding='ascii')
        result = self.run_fixture(app,'after-new-moved')
        self.assertNotEqual(result.returncode,0)
        self.assertFalse((app/'published-launcher.fixture').exists())
        self.assertFalse((app/'Package').exists())
        self.assertEqual((app/'State/keep.txt').read_bytes(),b'preferences')
        self.assertFalse((app/'State/package-upgrade.ini').exists())

    def test_failed_upgrade_preserves_previous_package(self):
        app = self.root/'upgrade'
        (app/'Package').mkdir(parents=True)
        (app/'Package/payload.fixture').write_bytes(b'previous payload')
        result = self.run_fixture(app,'after-new-moved')
        self.assertNotEqual(result.returncode,0)
        self.assertEqual((app/'Package/payload.fixture').read_bytes(),b'previous payload')
        self.assertFalse((app/'published-launcher.fixture').exists())
        self.assertFalse((app/'State/package-upgrade.ini').exists())

    def test_fresh_success_publishes_both_launcher_and_package(self):
        app = self.root/'success'
        result = self.run_fixture(app)
        self.assertEqual(result.returncode,0)
        self.assertEqual((app/'Package/payload.fixture').read_bytes(),b'incoming payload')
        self.assertEqual((app/'published-launcher.fixture').read_bytes(),b'small authored fixture')
        self.assertFalse((app/'State/package-upgrade.ini').exists())


if __name__ == '__main__':
    unittest.main()
