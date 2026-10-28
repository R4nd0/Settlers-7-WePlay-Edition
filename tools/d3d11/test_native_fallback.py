import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import native_fallback as target
import prepare


class FallbackTests(unittest.TestCase):
    def setUp(self):
        root = Path(__file__).resolve().parents[2]/'work/d3d11-tests'
        root.mkdir(exist_ok=True,parents=True)
        self.temp = tempfile.TemporaryDirectory(dir=root)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for folder in ('Base','Server','Launcher','Runtime'):
            (self.root/folder).mkdir(exist_ok=True)
        for name,data in [('Base/Bin/Settlers7R.exe',b'exe'),
                          ('Base/Bin/d3d9.dll',b'known-wrapper'),
                          ('Base/Bin/dgVoodoo.conf',b'known-config'),
                          ('Base/maps.untouched',b'map')]:
            path = self.root/name; path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(data)
        self.dll = prepare.content.digest(self.root/'Base/Bin/d3d9.dll')
        self.exe = prepare.content.digest(self.root/'Base/Bin/Settlers7R.exe')
        manifest = prepare.content.create(self.root)
        (self.root/'compatibility.json').write_bytes(target.encoded(manifest))
        (self.root/'d3d11-experiment.json').write_bytes(target.encoded({
            'format':'s7-d3d11-experiment-v1','package_sha256':prepare.ARCHIVE,
            'backend':'d3d11_fl11_0'}))
        self.original = {name:(self.root/name).read_bytes() for name in target.FILES}
        self.addCleanup(patch.stopall)
        patch.object(prepare,'DLL',self.dll).start()
        patch.object(prepare,'EXE',self.exe).start()

    def test_only_known_wrapper_removed_and_manifest_matches_native_inventory(self):
        target.ensure_native(self.root)
        expected = prepare.content.verify(self.root)
        self.assertNotIn('base/bin/d3d9.dll',expected['files'])
        self.assertEqual((self.root/'Base/maps.untouched').read_bytes(),b'map')
        self.assertEqual(json.loads((self.root/'d3d11-experiment.json').read_bytes())['backend'],
                         'native_d3d9')
        target.ensure_native(self.root)  # A repeated start remains valid.

    def test_interrupted_transition_resumes_but_unknown_bytes_are_never_overwritten(self):
        target.ensure_native(self.root)
        # Simulate interruption after only the first deletion.
        for name in target.FILES[1:]:
            (self.root/name).write_bytes(self.original[name])
        target.ensure_native(self.root)
        prepare.content.verify(self.root)
        foreign = self.root/'Base/Bin/dgVoodoo.conf'
        foreign.write_bytes(b'foreign edit')
        before = (self.root/'compatibility.json').read_bytes()
        with self.assertRaises(ValueError):
            target.ensure_native(self.root)
        self.assertEqual(foreign.read_bytes(),b'foreign edit')
        self.assertEqual((self.root/'compatibility.json').read_bytes(),before)

    def test_corrupted_wrapper_rejected_before_mutating_game(self):
        (self.root/'Base/Bin/d3d9.dll').write_bytes(b'foreign wrapper')
        with self.assertRaises(ValueError):
            target.ensure_native(self.root)
        self.assertEqual((self.root/'Base/Bin/dgVoodoo.conf').read_bytes(),self.original[target.FILES[1]])
        self.assertFalse((self.root/target.STATE/'ready.json').exists())


if __name__ == '__main__':
    unittest.main()
