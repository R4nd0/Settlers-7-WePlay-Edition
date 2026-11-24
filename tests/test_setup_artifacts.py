"""Isolated build-fixture regressions for setup provenance and publication."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/installer'))
import compatibility

spec = importlib.util.spec_from_file_location('setup_artifact_audit', ROOT/'tools/desktop/build_rc2.py')
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class SetupArtifactTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='setup-artifacts-',dir=ROOT/'work')
        self.addCleanup(temporary.cleanup)
        self.root=Path(temporary.name)
        self.app=self.root/'App';self.output=self.root/'output';self.output.mkdir()
        for folder in ('Payload','Runtime','Tools/installer'):
            (self.app/'Package'/folder).mkdir(parents=True)
        self.tool=self.app/'Package/Tools/installer/portable_lock.py'
        self.tool.write_bytes(b'previous fixture tool')
        self.manifest={'format':'s7-patch-package-v1',
            'files':compatibility.inventory(self.app/'Package',('Payload','Runtime','Tools'))}
        self.release={'format':'s7-desktop-release-v1','source_commit':'1'*40,'source_tree_dirty':False,
                      'package_id':compatibility.fingerprint(self.manifest['files'])}
        self.metadata=self.app/'desktop-release.json'
        self.metadata.write_text(json.dumps(self.release),encoding='utf-8')
        self.patch=patch.object(build.install,'package_manifest',return_value=self.manifest)
        self.patch.start();self.addCleanup(self.patch.stop)

    def compile_fixture(self,arguments,**_options):
        output=Path(next(arg.removeprefix('/DOutput=') for arg in arguments if arg.startswith('/DOutput=')))
        (output/'Setup.exe').write_bytes(b'new authored fixture')
        return SimpleNamespace(returncode=0,stdout='',stderr='')

    def test_refreshed_stage_updates_package_identity_and_source_commit(self):
        publish=self.root/'publish';publish.mkdir()
        (publish/'S7Launcher.dll').write_bytes(b'updated launcher fixture')
        with (patch.object(build,'copy_documents'),
              patch.object(build.subprocess,'check_output',side_effect=['2'*40+'\n',' M changed\n'])):
            build.refresh_stage(self.app,publish)
        release=json.loads(self.metadata.read_bytes())
        self.assertNotEqual(release['package_id'],self.release['package_id'])
        self.assertEqual(release['package_id'],compatibility.fingerprint(self.manifest['files']))
        self.assertEqual(release['source_commit'],'2'*40)
        self.assertTrue(release['source_tree_dirty'])

    def test_compiler_success_without_fresh_output_never_republishes_old_setup(self):
        target=self.output/'Setup.exe';target.write_bytes(b'previous installer')
        with patch.object(build.subprocess,'run',return_value=SimpleNamespace(returncode=0,stdout='',stderr='')):
            with self.assertRaisesRegex(RuntimeError,'no installer'):
                build.compile_setup(self.app,self.output,'fixture-iscc')
        self.assertEqual(target.read_bytes(),b'previous installer')
        self.assertFalse((self.output/'Setup.exe.sha256').exists())

    def test_standalone_compile_replaces_old_release_receipt_with_exact_proof(self):
        previous=self.output/'Setup.release.json';previous.write_text('{}',encoding='utf-8')
        with patch.object(build.subprocess,'run',side_effect=self.compile_fixture):
            build.compile_setup(self.app,self.output,'fixture-iscc')
        proof=json.loads(previous.read_bytes())
        data=(self.output/'Setup.exe').read_bytes()
        self.assertEqual(proof['setup_sha256'],hashlib.sha256(data).hexdigest())
        self.assertEqual(proof['setup_bytes'],len(data))
        self.assertEqual(proof['package_id'],self.release['package_id'])

    def test_stale_package_metadata_blocks_compiler_before_producing_setup(self):
        self.manifest['files']={'tools/installer/portable_lock.py':'0'*64}
        compiler=Mock(side_effect=self.compile_fixture)
        with patch.object(build.subprocess,'run',compiler):
            with self.assertRaisesRegex(ValueError,'package identity'):
                build.compile_setup(self.app,self.output,'fixture-iscc')
        compiler.assert_not_called()
        self.assertFalse((self.output/'Setup.exe').exists())

    def test_installer_output_must_stay_outside_application_stage(self):
        output=self.app/'nested output';output.mkdir()
        compiler=Mock(side_effect=self.compile_fixture)
        with patch.object(build.subprocess,'run',compiler):
            with self.assertRaisesRegex(ValueError,'outside the application stage'):
                build.compile_setup(self.app,output,'fixture-iscc')
        compiler.assert_not_called()
        self.assertFalse((output/'Setup.exe').exists())


if __name__=='__main__':
    unittest.main()
