"""Fault injection for update publication, allocation tracking and package swap checks."""
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).parent))
import test_installer as fixtures
import test_desktop_backend as desktops
import test_standalone as portables
import desktop_backend as backend
import compatibility as content
import install
import standalone
import windows_system

spec=importlib.util.spec_from_file_location('weplay_package_check',Path(__file__).resolve().parents[1]/'tools/desktop/check_package.py')
check=importlib.util.module_from_spec(spec);spec.loader.exec_module(check)


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.f=fixtures.InstallerTests();self.f.setUp();self.addCleanup(self.f.doCleanups)
        self.expected=self.f.root/'expected.json';shutil.copyfile(self.f.package/'package.json',self.expected)

    def test_complete_new_package_validates_without_old_overlay(self):
        old=self.f.root/'old';shutil.copytree(self.f.package,old)
        (old/'Tools/removed-helper.py').write_bytes(b'old helper')
        record=json.loads((old/'package.json').read_text());record['files']=content.inventory(old,('Payload','Runtime','Tools'))
        (old/'package.json').write_text(json.dumps(record));install.package_manifest(old)
        shutil.copytree(self.f.package,old,dirs_exist_ok=True)
        with self.assertRaises(ValueError):install.package_manifest(old)
        check.verify(self.f.package,self.expected)
        (self.f.package/'Tools/unknown.py').write_bytes(b'extra')
        with self.assertRaises(ValueError):check.verify(self.f.package,self.expected)

    def test_staged_manifest_cannot_redefine_expected_payload(self):
        target=self.f.package/'Payload/uplay_r1_loader.dll';target.write_bytes(b'changed')
        self.f.manifest()
        with self.assertRaises(ValueError):check.verify(self.f.package,self.expected)

    def test_staging_uses_new_bounded_slot_and_leaves_current_package_intact(self):
        self.f.write(self.f.package,'Runtime/notices/GPL-3.0.txt',b'license notice')
        self.f.manifest();shutil.copyfile(self.f.package/'package.json',self.expected)
        app=self.f.root/'App';app.mkdir();current=app/'Package';shutil.copytree(self.f.package,current)
        (current/'Tools/old-extra.txt').write_bytes(b'preserve old package')
        before=content.inventory(current,('Payload','Runtime','Tools'))
        target=app/('.weplay-package-'+'a'*32)
        check.stage(self.f.package,self.expected,app,target)
        check.verify(target,self.expected)
        self.assertIn('GPL-3.0.txt',[p.name for p in (target/'Runtime/notices').iterdir()])
        self.assertEqual(content.inventory(current,('Payload','Runtime','Tools')),before)
        with self.assertRaises(FileExistsError):check.stage(self.f.package,self.expected,app,target)
        with self.assertRaises(ValueError):check.stage(self.f.package,self.expected,app,app/'Package')

    def test_staging_space_check_does_not_allocate_a_partial_slot(self):
        app=self.f.root/'App';app.mkdir();target=app/('.weplay-package-'+'b'*32)
        with patch.object(check.shutil,'disk_usage',return_value=type('Disk',(),{'free':0})()):
            with self.assertRaises(ValueError):check.stage(self.f.package,self.expected,app,target)
        self.assertFalse(target.exists())

    def test_backup_retirement_keeps_modified_unknown_and_personal_files(self):
        app=self.f.root/'App';backup=app/'State/PackageBackups'/('.weplay-package-'+'a'*32+'.previous')
        shutil.copytree(self.f.package,backup)
        changed=backup/'Payload/uplay_r1_loader.dll';changed.write_bytes(b'user modification')
        unknown=backup/'Tools/unknown.txt';unknown.write_bytes(b'keep')
        result=check.retire(app,backup)
        self.assertGreater(result['removed'],0)
        self.assertEqual(changed.read_bytes(),b'user modification');self.assertEqual(unknown.read_bytes(),b'keep')
        self.assertTrue((backup/'package.json').exists())
        with self.assertRaises(ValueError):check.retire(app,self.f.package)

    def test_space_preflight_accounts_for_retained_archive_and_save_allowance_before_allocation(self):
        old_budget=sum(p.stat().st_size for p in self.f.source.rglob('*') if p.is_file())
        old_budget+=sum(p.stat().st_size for p in self.f.package.rglob('*') if p.is_file())+512*1024*1024
        with patch.object(install.shutil,'disk_usage',return_value=type('Disk',(),{'free':old_budget+1})()):
            with self.assertRaisesRegex(ValueError,'Speicher'):self.f.prepare()
        self.assertFalse(self.f.destination.exists())
        with patch.object(install.shutil,'disk_usage',return_value=type('Disk',(),{'free':old_budget+1024*1024})()):
            with self.assertRaisesRegex(ValueError,'Speicher'):
                install.prepare(self.f.package,self.f.source,self.f.destination,additional_bytes=2*1024*1024)
        self.assertFalse(self.f.destination.exists())


class PreparationTests(unittest.TestCase):
    def setUp(self):
        self.c=desktops.DesktopTests();self.c.setUp();self.addCleanup(self.c.doCleanups)
        self.d=self.c.desktop;self.c.prepare();self.old=self.d.game();self.before=self.d.installed()

    def test_failed_patch_reclaims_tracked_game_files_and_preserves_active_copy(self):
        record=self.c.app/'Package/package.json';value=json.loads(record.read_text())
        value['patched_archive_sha256']='0'*64;record.write_text(json.dumps(value))
        with self.assertRaises(ValueError):self.c.prepare()
        self.assertEqual(self.d.installed(),self.before)
        roots=[p for p in self.old.parent.iterdir() if p!=self.old]
        self.assertEqual(len(roots),1)
        self.assertFalse((roots[0]/'Base/BBA/map.bba').exists())
        self.assertFalse((roots[0]/'RepairSource/Base/BBA/Interface.bba').exists())
        self.assertFalse(list((self.d.state/'Preparations').glob('*.json')))
        self.assertTrue(list((self.d.state/'Preparations/Retained').glob('*.json')))

    def test_later_failure_is_tracked_and_unknown_files_survive_cleanup(self):
        def fail(old,root):
            (root/'unknown.txt').write_bytes(b'user file')
            raise OSError('injected preference failure')
        with patch.object(self.d,'transfer_preferences',side_effect=fail):
            with self.assertRaises(OSError):self.c.prepare()
        self.assertEqual(self.d.installed(),self.before)
        roots=[p for p in self.old.parent.iterdir() if p!=self.old];self.assertEqual(len(roots),1)
        self.assertEqual(len(list((self.d.state/'Preparations').glob('*.json'))),1)
        with patch.object(windows_system,'process_paths',return_value=[]):self.d.cleanup_preparations()
        self.assertEqual((roots[0]/'unknown.txt').read_bytes(),b'user file')
        self.assertFalse((roots[0]/'Base/BBA/map.bba').exists())
        self.assertEqual(self.d.installed(),self.before)

    def test_save_growth_capacity_recheck_stops_before_copy(self):
        saves=self.old/'LocalData/saves';saves.mkdir();(saves/'keep.s7s').write_bytes(b'checkpoint')
        target=self.c.fixture.root/'new';target.mkdir()
        with patch.object(backend.shutil,'disk_usage',return_value=type('Disk',(),{'free':0})()):
            with self.assertRaisesRegex(ValueError,'Speicher'):self.d.transfer_preferences(self.old,target)
        self.assertFalse((target/'LocalData').exists())
        self.assertEqual((saves/'keep.s7s').read_bytes(),b'checkpoint')

    def test_unknown_files_present_before_publication_are_not_adopted_for_deletion(self):
        unknowns=('keep-unknown.txt','RepairSource/keep-unknown.bin','ArchiveStage/keep-unknown.bin')
        def renderer(root,*args):
            for name in unknowns:(root/name).write_bytes(b'preserve unknown data')
            return 'native'
        with patch.object(self.d,'renderer',side_effect=renderer):self.c.prepare()
        current=self.d.game();files=self.d.read(current/'desktop-files.json')['files']
        self.assertTrue(all(name.lower() not in files for name in unknowns))
        self.c.prepare()
        for name in unknowns:self.assertEqual((current/name).read_bytes(),b'preserve unknown data')


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.f=portables.StandaloneTests();self.f.setUp();self.addCleanup(self.f.doCleanups)
        self.d=standalone.bind(self.f.root);self.before=self.d.installed()
        self.descriptor=standalone.descriptor(self.f.root)

    def move_export(self,name):
        moved=self.f.fixture.root/name
        assert self.f.root.resolve().is_relative_to(self.f.fixture.root.resolve())
        assert moved.resolve().is_relative_to(self.f.fixture.root.resolve())
        shutil.move(str(self.f.root),moved)
        return moved

    def test_descriptor_failure_rolls_back_active_selection(self):
        real=self.d.write
        def write(path,value):
            if path==self.f.root/'standalone.json':raise OSError('descriptor failure')
            real(path,value)
        with patch.object(self.d,'write',side_effect=write),patch.object(self.d,'clean_old') as cleanup:
            with self.assertRaises(OSError):self.d.install_game(self.f.game)
        self.assertEqual(self.d.installed(),self.before)
        self.assertEqual(standalone.descriptor(self.f.root),self.descriptor)
        self.assertFalse((self.d.state/'installation-transaction.json').exists());cleanup.assert_not_called()
        self.assertTrue((self.f.game/'Base/Bin/Settlers7R.exe').is_file())

    def interrupt(self,after_descriptor=False):
        real=self.d.write
        def write(path,value):
            real(path,value)
            target=self.f.root/'standalone.json' if after_descriptor else self.d.state/'installation.json'
            if path==target:raise OSError('simulated process interruption')
        with patch.object(self.d,'write',side_effect=write),patch.object(self.d,'recover_publication'),patch.object(self.d,'clean_old'):
            with self.assertRaises(OSError):self.d.install_game(self.f.game)
        self.assertTrue((self.d.state/'installation-transaction.json').exists())

    def test_startup_recovers_interruption_between_metadata_writes(self):
        self.interrupt()
        self.d.recover_publication()
        self.assertEqual(self.d.installed(),self.before)
        self.assertEqual(standalone.descriptor(self.f.root),self.descriptor)
        self.assertFalse((self.d.state/'installation-transaction.json').exists())

    def test_complete_pair_survives_interruption_before_journal_removal(self):
        self.interrupt(after_descriptor=True)
        new=self.d.installed();self.assertNotEqual(new['current'],self.before['current'])
        self.d.recover_publication()
        self.assertEqual(self.d.installed(),new)
        self.assertEqual(standalone.descriptor(self.f.root)['instance'],new['current'])

    def test_unexpected_metadata_is_preserved_and_recovery_stops(self):
        self.interrupt()
        marker=self.d.state/'installation.json';value=json.loads(marker.read_text());value['source']='unexpected change'
        marker.write_text(json.dumps(value));before=marker.read_bytes()
        with self.assertRaises(ValueError):self.d.recover_publication()
        self.assertEqual(marker.read_bytes(),before)
        self.assertTrue((self.d.state/'installation-transaction.json').exists())

    def test_relocation_preserves_pending_history_without_old_absolute_path_access(self):
        with patch.object(self.d,'clean_old',return_value={'cleaned':0}):self.d.install_game(self.f.game)
        current=self.d.game().name
        (self.f.game/'Base/BBA/map.bba').write_bytes(b'modified old map')
        moved=self.move_export('moved-with-history')
        self.f.game.mkdir(parents=True);sentinel=self.f.game/'foreign.txt';sentinel.write_bytes(b'foreign old location')
        rebound=standalone.bind(moved)
        self.assertEqual(rebound.game().name,current)
        self.assertEqual(rebound.installed()['previous'],[{'container':str(moved/'App/Instances'),'instance':self.f.instance}])
        with patch.object(windows_system,'process_paths',return_value=[]):rebound.clean_old()
        self.assertEqual((moved/'App/Instances'/self.f.instance/'Base/BBA/map.bba').read_bytes(),b'modified old map')
        self.assertEqual(sentinel.read_bytes(),b'foreign old location')

    def test_external_history_is_rejected_without_rebinding_it(self):
        with patch.object(self.d,'clean_old',return_value={'cleaned':0}):self.d.install_game(self.f.game)
        value=self.d.installed();value['previous'][0]['container']=str(self.f.fixture.root/'foreign')
        self.d.write(self.d.state/'installation.json',value)
        moved=self.move_export('moved-external-history')
        with self.assertRaises(ValueError):standalone.bind(moved)


if __name__=='__main__':unittest.main()
