import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.supersampling import probe


class ProbeIdentityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'native').mkdir()
        self.source = self.root / 'native/supersampling_probe.cpp'
        self.build_script = self.root / 'native/supersampling_build.ps1'
        self.source.write_bytes(b'fixture C++ source')
        self.build_script.write_bytes(b'fixture build script')
        self.executable = self.root / 'supersampling_probe.exe'
        self.executable.write_bytes(b'fixture binary, never executed')
        self.manifest = {
            'format': 's7-supersampling-build-v1',
            'probe_sha256': probe.sha256(self.executable),
            'source_sha256': probe.sha256(self.source),
            'build_sha256': probe.sha256(self.build_script),
        }
        self.manifest_path = self.root / 'supersampling-build.json'
        self.manifest_path.write_text(json.dumps(self.manifest), encoding='utf-8')
        self.patcher = patch.object(probe, 'REPOSITORY', self.root)
        self.patcher.start()
        self.addCleanup(self.patcher.stop)

    def test_accepts_exact_build_source_and_script_binding(self):
        self.assertEqual(probe.verify_probe(self.executable), self.manifest)

    def test_rejects_binary_stale_source_or_changed_build_script(self):
        for path in (self.executable, self.source, self.build_script):
            with self.subTest(path=path.name):
                before = path.read_bytes()
                path.write_bytes(before + b'changed')
                with self.assertRaisesRegex(ValueError, 'Stale'):
                    probe.verify_probe(self.executable)
                path.write_bytes(before)

    def test_rejects_arbitrary_executable_basename(self):
        other = self.root / 'unrelated.exe'
        other.write_bytes(self.executable.read_bytes())
        with self.assertRaisesRegex(ValueError, 'named standalone'):
            probe.verify_probe(other)

    def test_rejects_extra_manifest_fields(self):
        self.manifest['unknown_override'] = True
        self.manifest_path.write_text(json.dumps(self.manifest), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'Stale'):
            probe.verify_probe(self.executable)


if __name__ == '__main__':
    unittest.main()
