"""Protect sources/inputs while permitting relocated-workspace outputs."""
from pathlib import Path
import subprocess
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/installer'))
import build_package


class PackageLocationTests(unittest.TestCase):
    def test_builder_bootstraps_its_imports_with_isolated_python(self):
        script=Path(build_package.__file__).resolve()
        result=subprocess.run([sys.executable,'-I','-B',str(script),'--help'],capture_output=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stderr.decode(errors='replace'))
        self.assertIn(b'--dxvk-dll',result.stdout)

    def test_private_workspace_outputs_and_external_output_are_allowed(self):
        repo = Path('/workspace/project').absolute()
        inputs = (repo / 'work/service', repo / 'work/runtime', repo / 'work/downloads', repo / 'work/adapter')
        for path in (repo / 'outputs/package-v4', repo / 'work/new-package', repo.parent / 'package'):
            with self.subTest(path=path):
                build_package.check_output_location(repo, path, inputs)

    def test_sources_ancestors_and_both_input_overlap_directions_are_rejected(self):
        repo = Path('/workspace/project').absolute()
        inputs = (repo / 'work/service',)
        for path in (repo, repo.parent, repo / 'tools/package', repo / 'Source/package',
                     repo / 'work/service', repo / 'work/service/package', repo / 'work'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                build_package.check_output_location(repo, path, inputs)


if __name__ == '__main__':
    unittest.main()
