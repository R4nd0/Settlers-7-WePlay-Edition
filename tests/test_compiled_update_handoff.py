"""Same real process/installer fixtures against the independent native RC3.5 helper."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

import test_update_handoff_windows as legacy

ROOT=Path(__file__).resolve().parents[1]

@unittest.skipUnless(os.name=='nt' and legacy.SDK,'opt-in Windows .NET SDK native helper tests')
class CompiledHandoffTests(legacy.WindowsHandoffTests):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.native=cls.root/'native'
        intermediate=str(cls.root/'native-obj')+os.sep
        build=subprocess.run([legacy.SDK,'publish',str(ROOT/'tools/windows/UpdateHelper/WePlay.UpdateHelper.csproj'),
            '-c','Release','-r','win-x64','-o',str(cls.native),'-p:OS=Windows_NT',
            '-p:BaseIntermediateOutputPath='+intermediate,'-p:MSBuildProjectExtensionsPath='+intermediate,
            '-p:RestorePackagesPath='+str(ROOT/'work/nuget-packages'),
            '-p:UpdatePublicKey='+str(cls.public),'--source','https://api.nuget.org/v3/index.json','--nologo'],
            env=cls.environment,capture_output=True,text=True,timeout=120)
        if build.returncode:raise AssertionError('Native fixture helper build failed: '+build.stdout+build.stderr)
        # This inherited setting disables script files in any child Windows PowerShell.
        # No Windows registry or machine/user policy is changed.
        cls.environment['PSExecutionPolicyPreference']='Restricted'

    def setUp(self):
        super().setUp()
        trusted=self.app/'Package/Payload/Helpers';trusted.mkdir(parents=True)
        for target in (trusted,self.job):
            shutil.copyfile(self.native/'WePlay.UpdateHelper.exe',target/'WePlay.UpdateHelper.exe')

    def helper(self):
        return self.spawn([str(self.job/'WePlay.UpdateHelper.exe'),'--job',str(self.job/'handoff.json')],diagnostics=True)

    def test_corrupt_signature_is_rejected_before_any_process_exit_or_installer(self):
        path=self.job/'weplay-update.json';envelope=json.loads(path.read_bytes())
        raw=bytearray(legacy.client.b64(envelope['signature']));raw[0]^=1
        import base64
        envelope['signature']=base64.urlsafe_b64encode(raw).decode().rstrip('=');self.write(path,envelope)
        helper=self.helper();self.finished(helper,2)
        self.assertEqual(self.status()['code'],'update_signature_invalid')
        self.assertFalse((self.job/'ready.json').exists());self.assertFalse((self.app/'setup.marker').exists())
        self.assertTrue(all(p.poll() is None for p,_ in self.parents));self.preserved()

    def test_cached_payload_fingerprint_mismatch_is_refused(self):
        self.write(self.job.parent/'highest.json',{'sequence':2,'payload_sha256':'0'*64})
        helper=self.helper();self.finished(helper,2)
        self.assertEqual(self.status()['code'],'update_downgrade')
        self.assertFalse((self.app/'setup.marker').exists());self.preserved()

    def test_validation_only_never_runs_installer_or_touches_status(self):
        helper=self.spawn([str(self.job/'WePlay.UpdateHelper.exe'),'--validate',str(self.job/'handoff.json')],diagnostics=True)
        self.finished(helper,0)
        self.assertFalse((self.app/'setup.marker').exists());self.assertFalse((self.job.parent/'status.json').exists())
        self.assertTrue(all(p.poll() is None for p,_ in self.parents));self.preserved()

    def test_invalid_job_location_cannot_write_status_before_app_scope_is_validated(self):
        malformed=self.job/'untrusted.json';malformed.write_text('{}',encoding='utf-8')
        helper=self.spawn([str(self.job/'WePlay.UpdateHelper.exe'),'--job',str(malformed)],diagnostics=True)
        self.finished(helper,2)
        self.assertFalse((self.job.parent/'status.json').exists())
        self.assertFalse((self.app/'setup.marker').exists());self.preserved()

    def test_production_key_is_not_replaceable_by_a_job_public_key(self):
        production=Path(os.environ.get('S7_PRODUCTION_UPDATE_HELPER',ROOT/'work/windows-helpers-rc35/Update/WePlay.UpdateHelper.exe'))
        for target in (self.app/'Package/Payload/Helpers',self.job):
            shutil.copyfile(production,target/'WePlay.UpdateHelper.exe')
        helper=self.helper();self.finished(helper,2)
        self.assertEqual(self.status()['code'],'update_signature_invalid')
        self.assertFalse((self.app/'setup.marker').exists());self.preserved()

    def test_real_inno_fixture_installs_while_setup_is_read_locked_and_registers_no_uninstaller(self):
        import base64,uuid,winreg
        compiler=Path(os.environ.get('S7_HANDOFF_INNO',ROOT/'work/build-tools/InnoSetup7/ISCC.exe'))
        if not compiler.is_file():self.skipTest('opt-in local Inno Setup compiler')
        marker=self.root/('payload-'+uuid.uuid4().hex+'.txt');marker.write_bytes(b'owned Inno update fixture')
        identity='WePlay-Update-Test-'+uuid.uuid4().hex
        script=self.root/(identity+'.iss')
        script.write_text(f'''[Setup]
AppId={identity}
AppName=WePlay update fixture
AppVersion=1.0
DefaultDirName={self.app}
PrivilegesRequired=lowest
OutputDir={self.job}
OutputBaseFilename=Setup
Uninstallable=no
UsePreviousAppDir=no
CreateUninstallRegKey=no
CloseApplications=no
RestartApplications=no
DisableWelcomePage=yes
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
DisableFinishedPage=yes
[Files]
Source: "{marker}"; DestDir: "{{app}}"; DestName: "installed.fixture"; Flags: ignoreversion
''',encoding='utf-8')
        compiled=subprocess.run([str(compiler),'/Q',str(script)],capture_output=True,timeout=45)
        self.assertEqual(compiled.returncode,0,'Compile only the harmless isolated Inno fixture.')
        envelope=json.loads((self.job/'weplay-update.json').read_bytes())
        value=json.loads(legacy.client.b64(envelope['payload']))
        data=(self.job/'Setup.exe').read_bytes();value['setup'].update(size=len(data),sha256=hashlib.sha256(data).hexdigest())
        payload=legacy.client.canonical(value);(self.job/'payload.json').write_bytes(payload)
        signed=subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-File',str(ROOT/'tools/updates/sign_manifest.ps1'),
            '-Action','Sign','-KeyPath',str(self.private),'-PayloadPath',str(self.job/'payload.json')],capture_output=True,text=True,timeout=30)
        self.assertEqual(signed.returncode,0,'Sign with disposable fixture key only.')
        envelope.update(payload=base64.urlsafe_b64encode(payload).decode().rstrip('='),signature=signed.stdout.strip())
        self.write(self.job/'weplay-update.json',envelope)
        self.write(self.job.parent/'highest.json',{'sequence':2,'payload_sha256':hashlib.sha256(payload).hexdigest()})
        helper=self.helper();self.ready(helper);self.exit_parent(0);self.exit_parent(1);self.finished(helper,0)
        self.until(lambda:(self.app/'restarted.marker').exists())
        self.assertEqual((self.app/'installed.fixture').read_bytes(),marker.read_bytes())
        self.assertEqual(self.status()['phase'],'installed');self.assertFalse((self.app/'unins000.exe').exists());self.preserved()
        with self.assertRaises(FileNotFoundError):
            winreg.OpenKey(winreg.HKEY_CURRENT_USER,r'Software\Microsoft\Windows\CurrentVersion\Uninstall'+'\\'+identity+'_is1')

if __name__=='__main__':unittest.main()
