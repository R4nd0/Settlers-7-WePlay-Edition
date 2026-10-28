"""Meaningful synthetic copy/guard tests; these never launch S7."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import prepare as target

TEMPLATE = '\n'.join(k + ' = original' for k in
                     ('OutputAPI', 'Adapters', 'FPSLimit', 'VRAM', 'dgVoodooWatermark'))


class PrepareTests(unittest.TestCase):
    def setUp(self):
        workspace = Path(__file__).resolve().parents[2]
        temporary_root = workspace / 'work/d3d11-tests'
        temporary_root.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=temporary_root)
        assert Path(self.temp.name).resolve().is_relative_to(temporary_root.resolve())
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'source'
        self.output = self.root / 'output'
        for name in ['Base/Bin/Settlers7R.exe', 'Base/Bin/uplay_r1_loader.dll',
                     'Base/BBA/Interface.bba', 'Launcher/launch_local.py',
                     'Runtime/python/python.exe', 'Server/S7Patcher.dll',
                     'LocalData/private-profile.txt']:
            path = self.source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())
        self.hash = target.content.digest(self.source / 'Base/Bin/Settlers7R.exe')
        core = ['Base/Bin/Settlers7R.exe', 'Base/Bin/uplay_r1_loader.dll', 'Base/BBA/Interface.bba']
        bundle = {'format': 's7-local-experimental-v1', 'files': {
            name: target.content.digest(self.source / name) for name in core}}
        (self.source / 'local-bundle.json').write_text(json.dumps(bundle))
        self.inventory()
        self.addCleanup(patch.stopall)
        patch.object(target, 'EXE', self.hash).start()
        patch.object(target, 'package', return_value=(b'wrapper', TEMPLATE)).start()

    def inventory(self):
        (self.source / 'compatibility.json').write_text(json.dumps(target.content.create(self.source)))

    def run_prepare(self):
        return target.prepare(self.source, self.output, self.root / 'package.zip', 2)

    def test_new_copy_with_no_profiles_and_only_two_content_additions(self):
        before = {p.relative_to(self.source): p.read_bytes()
                  for p in self.source.rglob('*') if p.is_file()}
        self.run_prepare()
        self.assertFalse((self.output / 'LocalData').exists())
        after = {p.relative_to(self.source): p.read_bytes()
                 for p in self.source.rglob('*') if p.is_file()}
        self.assertEqual(before, after)
        expected = target.content.load(self.source / 'compatibility.json')
        actual = target.content.verify(self.output)
        self.assertEqual(target.content.compare(expected, actual),
                         ['base/bin/d3d9.dll', 'base/bin/dgvoodoo.conf'])
        config = (self.output / 'Base/Bin/dgVoodoo.conf').read_text()
        self.assertIn('OutputAPI = d3d11_fl11_0', config)
        self.assertIn('Adapters = 2', config)
        with self.assertRaises(ValueError):
            self.run_prepare()  # Never overwrite a loaded/existing copy.

    def test_corruption_and_foreign_renderer_fail_before_output(self):
        exe = self.source / 'Base/Bin/Settlers7R.exe'
        exe.write_bytes(b'changed executable')
        with self.assertRaises(ValueError):
            self.run_prepare()
        self.assertFalse(self.output.exists())
        exe.write_bytes(b'Base/Bin/Settlers7R.exe')
        (self.source / 'Base/Bin/d3d9.dll').write_bytes(b'foreign renderer')
        self.inventory()
        with self.assertRaises(ValueError):
            self.run_prepare()
        self.assertFalse(self.output.exists())

    def test_escape_and_nested_output_are_rejected(self):
        self.output = self.source / 'nested'
        with self.assertRaises(ValueError):
            self.run_prepare()
        self.assertFalse(self.output.exists())
        self.output = self.root / 'output'
        link = self.source / 'Runtime/escape'
        try:
            link.symlink_to(self.root, target_is_directory=True)
        except OSError:
            self.skipTest('Symlink creation not permitted on this Windows host')
        with self.assertRaises(ValueError):
            self.run_prepare()
        self.assertFalse(self.output.exists())

    def test_unexpected_template_is_not_partially_accepted(self):
        for template in [TEMPLATE.replace('OutputAPI', 'UnknownKey'), TEMPLATE + '\nOutputAPI = duplicate']:
            with self.assertRaises(ValueError):
                target.configuration(template, 1)
        for adapter in [0, -1, True, 33, '1']:
            with self.assertRaises(ValueError):
                target.configuration(TEMPLATE, adapter)


if __name__ == '__main__':
    unittest.main()
