"""Authored files and failure injection only; no Windows/GPU/profile access."""
import hashlib
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch
import zipfile

from . import on12_runtime as r,on12_runtime_build as b,recover,game,water_on12_probe,on12_water_patch as water

class Tests(unittest.TestCase):
    def setUp(self):
        temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup);self.root=Path(temp.name).resolve()
        self.bundle=self.root/'work/game';self.case=self.root/'work/case';self.directory=self.root/'work/build'
        self.exe=self.bundle/'Base/Bin/Settlers7R.exe';self.exe.parent.mkdir(parents=True);self.exe.write_bytes(b'authored original, never executed')
        self.directory.mkdir(parents=True);(self.directory/'archives').mkdir()
        source_pins={};packages={}
        for name in b.REVISIONS:
            archive=self.directory/'archives'/(name+'.zip')
            with zipfile.ZipFile(archive,'w') as z:z.writestr('upstream/test.h',b'authored source '+name.encode())
            source_pins[name]=b.digest(archive);b.unpack(archive,self.directory/'sources'/name,strip_root=True)
        for name in b.PACKAGES:
            archive=self.directory/'archives'/(name+'.zip')
            with zipfile.ZipFile(archive,'w') as z:z.writestr('test.h',b'authored dependency '+name.encode())
            packages[name]=('https://example.invalid/never-fetched',b.digest(archive));b.unpack(archive,self.directory/'dependencies'/name)
        self.signer=b'synthetic signer'
        for obj,name,value in ((recover,'ROOT',self.root),(r,'ROOT',self.root),(b,'ROOT',self.root),
                (b,'SOURCE_PINS',source_pins),(b,'PACKAGES',packages),(r,'SIGNER',hashlib.sha256(self.signer).hexdigest()),
                (water_on12_probe.loader,'UBI',b.digest(self.exe))):
            mocked=patch.object(obj,name,value);mocked.start();self.addCleanup(mocked.stop)
        for name in b.PROJECT_INPUTS:
            path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'authored fixture input')
        (self.directory/'project').mkdir();(self.directory/'project/CMakeLists.txt').write_bytes(b.project_cmake(self.directory).encode())
        for name,raw in [('d3d9on12.dll',b'authored runtime'),('dxbcSigner.dll',self.signer),('on12_runtime_gpu.exe',b'authored non-executable bytes')]:
            (self.directory/name).write_bytes(raw)
        self.manifest=dict(format='s7-on12-runtime-build-v1',variant='stock',configuration=b.CONFIG,revisions=b.REVISIONS,
            source_archives=b.SOURCE_PINS,packages={name:p for name,(_,p) in b.PACKAGES.items()},builder_sha256=b.digest(Path(b.__file__)),
            project_inputs={name:b.digest(self.root/name) for name in b.PROJECT_INPUTS},inputs=b.inputs(self.directory),
            files={name:b.digest(self.directory/name) for name in b.FILES})
        self.write_manifest()
        for target,value in [('tools.graphics_lab.game.platform.load',({'edition':'history'},self.exe,self.exe.with_name('adapter.dll'))),
                             ('tools.graphics_lab.on12_runtime.validate_runtime',self.root/'Windows/SysWOW64/d3d9.dll')]:
            mocked=patch(target,return_value=value);mocked.start();self.addCleanup(mocked.stop)
        mocked=patch.object(recover,'_no_games');self.no_games=mocked.start();self.addCleanup(mocked.stop)

    def write_manifest(self):
        (self.directory/b.MANIFEST).write_text(json.dumps(self.manifest))

    def session(self):
        self.case.mkdir();return r.Session(self.bundle,self.case,self.directory)

    def test_verification_rejects_binary_project_source_and_dependency_drift(self):
        r.verify_build(self.directory)
        for path in [self.directory/'d3d9on12.dll',self.root/b.PROJECT_INPUTS[0],
                     self.directory/'sources/D3D9On12/test.h',self.directory/'dependencies/wdk/test.h']:
            original=path.read_bytes();path.write_bytes(original+b' changed')
            with self.subTest(path=path),self.assertRaises(ValueError):r.verify_build(self.directory)
            path.write_bytes(original)

    def test_self_consistent_manifest_cannot_disguise_changed_upstream_source(self):
        name='sources/D3D9On12/test.h';path=self.directory/name;path.write_bytes(b'modified upstream code')
        self.manifest['inputs'][name]=b.digest(path);self.write_manifest()
        with self.assertRaisesRegex(ValueError,'sources or dependencies'):r.verify_build(self.directory)

    def test_water_variant_remains_bound_to_exact_patch_and_rejects_self_consistent_source_drift(self):
        raw=b'authored source D3D9On12'
        edits={'test.h':(hashlib.sha256(raw).hexdigest(),[(0,'authored','patched')])}
        with patch.object(water,'EDITS',edits),patch.object(water,'ROOT',self.root):
            for name in water.INPUTS:
                path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'authored variant input')
            for name in water.ADDED:(self.directory/'sources/D3D9On12'/name).parent.mkdir(parents=True,exist_ok=True)
            water.apply(self.directory)
            self.manifest['variant']='water-worker'
            self.manifest['project_inputs']={name:b.digest(self.root/name)for name in b.project_inputs('water-worker')}
            (self.directory/'project/CMakeLists.txt').write_bytes(b.project_cmake(self.directory,'water-worker').encode())
            self.manifest['inputs']=b.inputs(self.directory);self.write_manifest();r.verify_build(self.directory)
            path=self.directory/'sources/D3D9On12/test.h';path.write_bytes(b'other shader behavior')
            self.manifest['inputs']['sources/D3D9On12/test.h']=b.digest(path);self.write_manifest()
            with self.assertRaisesRegex(ValueError,'sources or dependencies'):r.verify_build(self.directory)
        with self.assertRaisesRegex(ValueError,'exact pinned'):water.patched('src/9on12Shader.cpp',b'unknown upstream revision')

    def test_backend_observation_requires_complete_read_only_detachment(self):
        session=self.session();session.apply();session.manifest['variant']='water-worker'
        owned=SimpleNamespace(pid=17,owned=Mock());report=self.case/'water-probe-live.private.json'
        good=dict(attached=True,on12_query_hr=0,failure=0,snapshot_done=True,snapshot_once=True,
                  hooks_detached=True,pow_cap_requested=False,caps_applied=0,caps_restored=0)
        for change in (dict(hooks_detached=False),dict(pow_cap_requested=True),dict(caps_applied=1,caps_restored=1)):
            report.write_text(json.dumps({**good,**change}))
            with self.subTest(change=change),self.assertRaisesRegex(RuntimeError,'read-only observer'):
                session.observe(owned,[self.exe.with_name('d3d9on12.dll')])
            self.assertFalse(session.observed)
        report.write_text(json.dumps(good));session.observe(owned,[self.exe.with_name('d3d9on12.dll')])
        self.assertEqual(json.loads((self.case/'on12-runtime-observed.private.json').read_text())['source_variant'],'water-worker')

    def test_unknown_variant_target_and_traversal_are_rejected(self):
        for key,value in [('variant','patched'),('files',{**self.manifest['files'],'../save.s7s':'a'*64}),
                          ('inputs',{'sources/../../save.s7s':'a'*64})]:
            before=self.manifest[key];self.manifest[key]=value;self.write_manifest()
            with self.subTest(key=key),self.assertRaises(ValueError):r.verify_build(self.directory)
            self.manifest[key]=before

    def test_partial_staging_recovers_without_touching_newer_save(self):
        session=self.session();save=self.bundle/'new-save.s7s';save.write_bytes(b'newer save')
        original=recover._atomic
        def fail(path,raw,**kwargs):
            if Path(path)==self.exe.with_name('dxbcSigner.dll'):raise OSError('authored interrupted second file')
            return original(path,raw,**kwargs)
        with patch.object(recover,'_atomic',side_effect=fail),self.assertRaises(OSError):session.apply()
        self.assertEqual(json.loads((self.case/r.JOURNAL).read_text())['state'],'prepared')
        session.restore();session.restore()
        self.assertFalse(self.exe.with_name('d3d9on12.dll').exists());self.assertEqual(save.read_bytes(),b'newer save')

    def test_existing_runtime_and_changed_runtime_are_preserved(self):
        session=self.session();target=self.exe.with_name('d3d9on12.dll');target.write_bytes(b'user runtime')
        with self.assertRaisesRegex(ValueError,'existing local runtime'):session.apply()
        self.assertFalse((self.case/r.JOURNAL).exists());target.unlink();session.apply()
        signer=self.exe.with_name('dxbcSigner.dll');signer.write_bytes(b'newer user signer')
        with self.assertRaisesRegex(RuntimeError,'newer data'):session.restore()
        self.assertTrue(target.exists());self.assertEqual(signer.read_bytes(),b'newer user signer')

    def test_recovery_survives_builder_changes_but_preserves_reappeared_file(self):
        session=self.session();session.apply()
        (self.root/b.PROJECT_INPUTS[0]).write_bytes(b'later fixture source')
        session.restore()
        self.exe.with_name('d3d9on12.dll').write_bytes(b'authored runtime')
        with self.assertRaisesRegex(RuntimeError,'newer data'):session.restore()

    def test_live_games_block_staging_and_restoration(self):
        session=self.session();self.no_games.side_effect=RuntimeError('owned elsewhere')
        with self.assertRaises(RuntimeError):session.apply()
        self.assertFalse((self.case/r.JOURNAL).exists());self.no_games.side_effect=None;session.apply()
        self.no_games.side_effect=RuntimeError('still live')
        with self.assertRaises(RuntimeError):session.restore()
        self.assertTrue(self.exe.with_name('d3d9on12.dll').exists())

    def test_observation_requires_selected_module_and_real_on12_identity(self):
        session=self.session();session.apply();owned=SimpleNamespace(pid=17,owned=Mock())
        with self.assertRaisesRegex(RuntimeError,'not selected'):session.observe(owned,[self.root/'Windows/d3d9on12.dll'])
        report=self.case/'water-probe-live.private.json'
        report.write_text(json.dumps(dict(attached=True,on12_query_hr=-1,failure=0)))
        with self.assertRaisesRegex(RuntimeError,'On12 device'):session.observe(owned,[self.exe.with_name('d3d9on12.dll')])
        report.write_text(json.dumps(dict(attached=True,on12_query_hr=0,failure=0)))
        session.observe(owned,[self.exe.with_name('d3d9on12.dll')]);self.assertTrue(session.observed)
        self.assertTrue((self.case/'on12-runtime-observed.private.json').exists());owned.owned.assert_called()

    def test_recovery_snapshot_changes_are_refused_before_mutation(self):
        session=self.session();session.apply();snapshot=self.case/r.SNAPSHOT;snapshot.write_bytes(snapshot.read_bytes()+b' ')
        with self.assertRaisesRegex(RuntimeError,'snapshot changed'):session.restore()
        self.assertTrue(self.exe.with_name('d3d9on12.dll').exists())

    def test_archive_paths_cannot_escape_the_destination(self):
        archive=self.root/'bad.zip'
        with zipfile.ZipFile(archive,'w') as z:z.writestr('../keep.txt',b'bad')
        with self.assertRaises(ValueError):b.unpack(archive,self.root/'extract')
        self.assertFalse((self.root/'keep.txt').exists())

if __name__=='__main__':unittest.main()
