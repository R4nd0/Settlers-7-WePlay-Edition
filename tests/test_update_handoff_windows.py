"""Opt-in production Windows handoff with locally compiled, harmless executables.

Set S7_HANDOFF_DOTNET to a local .NET 10 SDK's dotnet.exe to run. No downloaded
program, real application, installer registry entry, or production key is used.
"""
import base64
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/installer'))
import update_client as client

SDK = os.environ.get('S7_HANDOFF_DOTNET', '')
CSHARP = r'''
using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading;
class Fixture {
    static int Main(string[] args) {
        if(args.Length == 2 && args[0] == "--hold") {
            File.WriteAllText(args[1] + ".started", "ready");
            while(!File.Exists(args[1])) Thread.Sleep(20);
            return 0;
        }
        string executable = Environment.ProcessPath!;
        if(Path.GetFileName(executable).Equals("Setup.exe", StringComparison.OrdinalIgnoreCase)) {
            string directory = args.Single(x => x.StartsWith("/DIR=", StringComparison.Ordinal)).Substring(5).Trim('"');
            using var record = JsonDocument.Parse(File.ReadAllText(Path.Combine(Path.GetDirectoryName(executable)!, "handoff.json")));
            bool Alive(string name) {
                try { using var p = Process.GetProcessById(record.RootElement.GetProperty(name).GetProperty("pid").GetInt32()); return !p.HasExited; }
                catch(ArgumentException) { return false; }
            }
            File.WriteAllText(Path.Combine(directory, "setup.marker"), JsonSerializer.Serialize(new { parent_alive=Alive("parent"), backend_alive=Alive("backend"), arguments=args }));
            return File.Exists(Path.Combine(directory, "fail-installer.fixture")) ? 23 : 0;
        }
        File.WriteAllText(Path.Combine(Path.GetDirectoryName(executable)!, "restarted.marker"), "restarted");
        return 0;
    }
}
'''


@unittest.skipUnless(os.name == 'nt' and SDK, 'opt-in local Windows .NET SDK handoff fixture')
class WindowsHandoffTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        base = ROOT / 'work/test-temp'
        base.mkdir(parents=True, exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix='real-handoff-', dir=base)
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.root = Path(cls.temporary.name)
        cls.environment = dict(os.environ, DOTNET_ROOT=str(Path(SDK).resolve().parent),
                               DOTNET_CLI_HOME=str(cls.root / 'dotnet-home'),
                               DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1', DOTNET_CLI_TELEMETRY_OPTOUT='1')
        project = cls.root / 'compiler'
        project.mkdir()
        (project / 'Fixture.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><Nullable>enable</Nullable><UseAppHost>true</UseAppHost></PropertyGroup></Project>', encoding='utf-8')
        (project / 'Program.cs').write_text(CSHARP, encoding='utf-8')
        (project / 'NuGet.Config').write_text('<configuration><packageSources><clear /></packageSources></configuration>', encoding='utf-8')
        cls.binary = cls.root / 'compiled'
        built = subprocess.run([SDK, 'build', str(project / 'Fixture.csproj'), '-c', 'Release', '-o', str(cls.binary), '--nologo'],
                               env=cls.environment, capture_output=True, text=True, timeout=60)
        if built.returncode:
            raise AssertionError('Benign fixture compilation failed: ' + built.stdout + built.stderr)
        cls.private = cls.root / 'ephemeral-fixture.dpapi'
        cls.public = cls.root / 'ephemeral-public.json'
        created = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-File', str(ROOT / 'tools/updates/sign_manifest.ps1'),
                                  '-Action', 'CreateKey', '-KeyPath', str(cls.private), '-PublicPath', str(cls.public)],
                                 capture_output=True, text=True, timeout=30)
        if created.returncode:
            raise AssertionError('Ephemeral fixture key generation failed.')
        cls.pin = json.loads(cls.public.read_bytes())

    def setUp(self):
        self.app = self.root / ('App with spaces ' + uuid.uuid4().hex)
        self.job = self.app / 'State/Updates' / uuid.uuid4().hex
        self.job.mkdir(parents=True)
        self.processes = []
        self.addCleanup(self.stop_owned)
        self.trusted = self.app / 'Package/Tools/installer'
        self.trusted.mkdir(parents=True)
        self.executables(self.app, 'S7Launcher.exe')
        self.executables(self.app / 'Package/Runtime/python', 'python.exe')
        self.executables(self.job, 'Setup.exe')
        for target in (self.trusted, self.job):
            shutil.copyfile(self.public, target / 'update-key.json')
            shutil.copyfile(ROOT / 'tools/installer/update_handoff.ps1', target / 'update_handoff.ps1')
        self.write(self.app / 'desktop-release.json', {'version': '2.0.0-rc3.4', 'update_sequence': 1, 'update_channel': 'preview'})
        (self.app / 'State/keep.fixture').write_text('preferences', encoding='utf-8')
        (self.app / 'Instances').mkdir()
        (self.app / 'Instances/keep.fixture').write_text('save', encoding='utf-8')
        self.parents = []
        for name, executable in (('parent', self.app / 'S7Launcher.exe'), ('backend', self.app / 'Package/Runtime/python/python.exe')):
            signal = self.app / (name + '.exit')
            process = self.spawn([str(executable), '--hold', str(signal)])
            self.until(lambda: signal.with_suffix('.exit.started').is_file(), process)
            self.parents.append((process, signal))
        self.record = {'format': 'weplay-update-handoff-v1', 'app': str(self.app),
                       'parent': client.process_info(self.parents[0][0].pid), 'backend': client.process_info(self.parents[1][0].pid),
                       'version': '2.0.0-rc3.5', 'sequence': 2}
        self.write(self.job / 'handoff.json', self.record)
        setup = (self.job / 'Setup.exe').read_bytes()
        value = {'format': 'weplay-update-v1', 'repository': client.REPOSITORY, 'channel': 'preview', 'sequence': 2,
                 'version': self.record['version'], 'tag': 'weplay-test-2', 'notes': 'Harmless compiled fixture.',
                 'setup': {'name': 'Setup.exe', 'url': 'https://github.com/' + client.REPOSITORY + '/releases/download/weplay-test-2/Setup.exe',
                           'size': len(setup), 'sha256': hashlib.sha256(setup).hexdigest()}}
        payload = client.canonical(value)
        (self.job / 'payload.json').write_bytes(payload)
        signed = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-File', str(ROOT / 'tools/updates/sign_manifest.ps1'),
                                 '-Action', 'Sign', '-KeyPath', str(self.private), '-PayloadPath', str(self.job / 'payload.json')],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(signed.returncode, 0, 'Ephemeral fixture signing must succeed.')
        self.write(self.job / 'weplay-update.json', {'format': 'weplay-signed-update-v1', 'key_id': self.pin['key_id'],
                   'payload': base64.urlsafe_b64encode(payload).decode().rstrip('='), 'signature': signed.stdout.strip()})
        self.write(self.job.parent / 'highest.json', {'sequence': 2, 'payload_sha256': hashlib.sha256(payload).hexdigest()})

    def executables(self, directory, name):
        directory.mkdir(parents=True, exist_ok=True)
        for path in self.binary.iterdir():
            if path.is_file():
                shutil.copyfile(path, directory / (name if path.name == 'Fixture.exe' else path.name))

    def spawn(self, args, diagnostics=False):
        process = subprocess.Popen(args, env=self.environment, stdout=subprocess.PIPE if diagnostics else subprocess.DEVNULL,
                                   stderr=subprocess.PIPE if diagnostics else subprocess.DEVNULL,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        self.processes.append(process)
        return process

    def stop_owned(self):
        # Kill only handles launched by this test, helper first to prevent a late installation.
        for process in reversed(self.processes):
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
            if process.stdout:
                process.stdout.close()
            if process.stderr:
                process.stderr.close()

    @staticmethod
    def write(path, value):
        path.write_bytes(client.canonical(value))

    def until(self, condition, process=None, timeout=25):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if condition():
                return
            if process is not None and process.poll() is not None:
                output = process.communicate() if process.stderr else (b'', b'')
                self.fail('Owned fixture process exited before its expected checkpoint: ' + str(process.returncode) + repr(output))
            time.sleep(.05)
        self.fail('Timed out waiting for an owned fixture checkpoint.')

    def helper(self):
        return self.spawn(['powershell.exe', '-NoProfile', '-NonInteractive', '-File', str(self.job / 'update_handoff.ps1'),
                           '-Job', str(self.job / 'handoff.json')], diagnostics=True)

    def finished(self, helper, expected):
        output = helper.communicate(timeout=30)
        self.assertEqual(helper.returncode, expected, repr(output))

    def ready(self, helper):
        self.until(lambda: (self.job / 'ready.json').is_file(), helper)
        self.assertEqual(json.loads((self.job / 'ready.json').read_bytes()),
                         {'format': 'weplay-update-ready-v1', 'helper_pid': helper.pid, 'version': self.record['version'], 'sequence': 2})
        self.assertEqual(self.status()['phase'], 'waiting')
        self.assertFalse((self.app / 'setup.marker').exists())

    def status(self):
        return json.loads((self.job.parent / 'status.json').read_bytes())

    def exit_parent(self, index):
        process, signal = self.parents[index]
        signal.write_text('exit', encoding='utf-8')
        self.assertEqual(process.wait(timeout=5), 0)

    def preserved(self):
        self.assertEqual((self.app / 'State/keep.fixture').read_text(encoding='utf-8'), 'preferences')
        self.assertEqual((self.app / 'Instances/keep.fixture').read_text(encoding='utf-8'), 'save')

    def test_success_waits_for_both_parents_holds_mutex_installs_and_restarts(self):
        helper = self.helper()
        self.ready(helper)
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.OpenMutexW.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR]
        kernel.OpenMutexW.restype = wintypes.HANDLE
        kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        kernel.WaitForSingleObject.restype = wintypes.DWORD
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        mutex = 'Local\\S7Launcher-Update-' + hashlib.sha256(str(self.app).lower().encode()).hexdigest().upper()[:24]
        handle = kernel.OpenMutexW(0x100000, False, mutex)
        self.assertTrue(handle, 'Production helper must hold the precise app mutex.')
        try:
            self.assertEqual(kernel.WaitForSingleObject(handle, 0), 258, 'Mutex must be owned by the helper.')
        finally:
            kernel.CloseHandle(handle)
        self.exit_parent(0)
        time.sleep(.3)
        self.assertIsNone(helper.poll())
        self.assertFalse((self.app / 'setup.marker').exists(), 'Setup must wait for the backend as well as the launcher.')
        self.exit_parent(1)
        self.finished(helper, 0)
        self.until(lambda: (self.app / 'restarted.marker').is_file())
        marker = json.loads((self.app / 'setup.marker').read_bytes())
        self.assertFalse(marker['parent_alive'])
        self.assertFalse(marker['backend_alive'])
        self.assertEqual(marker['arguments'], ['/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/DIR=' + str(self.app)])
        self.assertEqual(self.status()['phase'], 'installed')
        self.preserved()

    def test_failed_installer_records_failure_without_restarting(self):
        (self.app / 'fail-installer.fixture').write_text('fail', encoding='utf-8')
        helper = self.helper()
        self.ready(helper)
        self.exit_parent(0)
        self.exit_parent(1)
        self.finished(helper, 2)
        self.assertTrue((self.app / 'setup.marker').is_file())
        self.assertEqual(self.status()['phase'], 'failed')
        self.assertEqual(self.status()['code'], 'update_install_failed')
        self.assertFalse((self.app / 'restarted.marker').exists())
        self.preserved()

    def test_exact_start_token_mismatch_blocks_before_ready_or_setup(self):
        self.record['backend']['started_ticks'] += 1
        self.write(self.job / 'handoff.json', self.record)
        helper = self.helper()
        self.finished(helper, 2)
        self.assertEqual(self.status()['code'], 'update_parent_invalid')
        self.assertFalse((self.job / 'ready.json').exists())
        self.assertFalse((self.app / 'setup.marker').exists())
        self.assertTrue(all(process.poll() is None for process, _ in self.parents))
        self.preserved()

    def test_second_helper_is_refused_while_original_waits(self):
        helper = self.helper()
        self.ready(helper)
        second = self.helper()
        self.finished(second, 2)
        self.assertEqual(self.status()['code'], 'update_busy')
        self.assertIsNone(helper.poll())
        self.assertTrue(all(process.poll() is None for process, _ in self.parents))
        self.assertFalse((self.app / 'setup.marker').exists())
        self.preserved()

    def test_changed_setup_after_ready_is_refused_at_final_boundary(self):
        helper = self.helper()
        self.ready(helper)
        (self.job / 'Setup.exe').write_bytes(b'changed fixture; never execute')
        self.exit_parent(0)
        self.exit_parent(1)
        self.finished(helper, 2)
        self.assertEqual(self.status()['code'], 'update_hash_invalid')
        self.assertFalse((self.app / 'setup.marker').exists())
        self.assertFalse((self.app / 'restarted.marker').exists())
        self.preserved()


if __name__ == '__main__':
    unittest.main()
