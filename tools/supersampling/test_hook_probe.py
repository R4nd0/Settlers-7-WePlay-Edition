import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.supersampling import hook_probe, probe


class HookBuildIdentityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        native = self.root / 'native'
        self.build = self.root / 'build'
        native.mkdir()
        self.build.mkdir()
        for name in hook_probe.SOURCES:
            (native / name).write_bytes(('fixture source: ' + name).encode())
        script = native / 'supersampling_hook_build.ps1'
        script.write_bytes(b'fixture build script')
        for name in hook_probe.BINARIES:
            (self.build / name).write_bytes(('fixture binary, never executed: ' + name).encode())
        self.manifest = {
            'format': 's7-supersampling-hook-build-v1',
            'build_sha256': probe.sha256(script),
            'files': {name: probe.sha256(native / name) for name in hook_probe.SOURCES},
        }
        self.manifest['files'].update({name: probe.sha256(self.build / name) for name in hook_probe.BINARIES})
        self.manifest_path = self.build / 'supersampling-hook-build.json'
        self.manifest_path.write_text(json.dumps(self.manifest), encoding='utf-8')
        self.patcher = patch.object(probe, 'REPOSITORY', self.root)
        self.patcher.start()
        self.addCleanup(self.patcher.stop)

    def test_accepts_exact_header_cpp_script_dll_and_fixture_binding(self):
        self.assertEqual(hook_probe.verify_build(self.build), self.manifest)

    def test_rejects_changed_header_even_if_dll_and_cpp_are_unchanged(self):
        (self.root / 'native/supersampling_hook.h').write_bytes(b'changed ABI')
        with self.assertRaisesRegex(ValueError, 'Stale'):
            hook_probe.verify_build(self.build)

    def test_rejects_changed_dll_without_executing_anything(self):
        (self.build / 'WePlaySupersamplingLab.dll').write_bytes(b'changed DLL')
        with self.assertRaisesRegex(ValueError, 'Stale'):
            hook_probe.verify_build(self.build)

    def test_rejects_extra_source_or_binary_in_manifest(self):
        self.manifest['files']['unreviewed.dll'] = '0' * 64
        self.manifest_path.write_text(json.dumps(self.manifest), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'Stale'):
            hook_probe.verify_build(self.build)


if __name__ == '__main__':
    unittest.main()
