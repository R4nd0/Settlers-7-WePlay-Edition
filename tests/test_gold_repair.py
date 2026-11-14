"""Gold repair uses retained originals, never damaged patched inputs or shared saves."""
import hashlib,importlib.util,json
from pathlib import Path
import sys,tempfile,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/installer'),str(ROOT/'tools/gold')]
spec=importlib.util.spec_from_file_location('gold_repair_fixture',ROOT/'tools/gold/installer.py')
installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)
import platform_standalone as platform
gold=installer.gold
sha=lambda data:hashlib.sha256(data).hexdigest()


class GoldRepairTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(dir=ROOT/'work');self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)/'Old';self.root.mkdir()
        self.originals={gold.EXE.as_posix():b'invented original exe',gold.INTERFACE.as_posix():b'invented original interface',
                        gold.SHARED.as_posix():b'invented original shared archive',
                        gold.EXE.with_name('mss32.dll').as_posix():b'invented original audio'}
        self.files={name:sha(data) for name,data in self.originals.items()}
        self.asset='Data/Base/BBA/game.fixture';self.files[self.asset]=sha(b'invented unchanged game asset')
        self.write('Backup/Settlers7R.original.exe',self.originals[gold.EXE.as_posix()])
        self.write('Backup/Interface.original.bba',self.originals[gold.INTERFACE.as_posix()])
        self.write('Backup/Shr.original.bba',self.originals[gold.SHARED.as_posix()])
        self.write(gold.EXE.with_name('weplay_miles_original.dll'),self.originals[gold.EXE.with_name('mss32.dll').as_posix()])
        self.write(self.asset,b'invented unchanged game asset')
        self.write('LocalData/ProfileRoot/Profiles/Savegames/keep.s7s',b'invented user save')
        self.report()
        for obj,name,value in ((gold,'GOLD_SHA256',self.files[gold.EXE.as_posix()]),
                               (gold,'INTERFACE_SHA256',self.files[gold.INTERFACE.as_posix()]),
                               (gold,'SHARED_SOURCE_SHA256',self.files[gold.SHARED.as_posix()]),
                               (platform,'MILES_ORIGINAL',self.files[gold.EXE.with_name('mss32.dll').as_posix()])):
            mock=patch.object(obj,name,value);mock.start();self.addCleanup(mock.stop)

    def write(self,name,data):
        path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)

    def report(self):
        self.write('gold-development.json',json.dumps(dict(format='s7-gold-development-copy-v1',source_files=self.files)).encode())

    def test_missing_patched_files_do_not_require_original_installation(self):
        originals,files=installer.repair_inputs(self.root)
        self.assertEqual(originals,self.originals);self.assertEqual(files,self.files)
        self.assertFalse((self.root/gold.EXE).exists())
        self.assertEqual((self.root/'LocalData/ProfileRoot/Profiles/Savegames/keep.s7s').read_bytes(),b'invented user save')

    def test_changed_retained_original_and_escaping_inventory_are_refused(self):
        self.write('Backup/Settlers7R.original.exe',b'changed')
        with self.assertRaisesRegex(ValueError,'retained original'):installer.repair_inputs(self.root)
        self.files['Data/../../outside']='a'*64;self.report()
        with self.assertRaisesRegex(ValueError,'repair inventory'):installer.repair_inputs(self.root)

    def test_shared_backup_is_required_once_graphics_archive_is_modified(self):
        original=self.originals[gold.SHARED.as_posix()]
        self.write(gold.SHARED,b'modified generated environment')
        self.assertEqual(installer.repair_inputs(self.root)[0][gold.SHARED.as_posix()],original)
        (self.root/'Backup/Shr.original.bba').unlink()
        with self.assertRaisesRegex(ValueError,'retained shared'):installer.repair_inputs(self.root)
        self.write(gold.SHARED,original)
        self.assertEqual(installer.repair_inputs(self.root)[0][gold.SHARED.as_posix()],original)

    def test_legacy_original_audio_is_accepted_only_with_exact_original_hash(self):
        (self.root/gold.EXE.with_name('weplay_miles_original.dll')).unlink()
        name=gold.EXE.with_name('mss32.dll').as_posix()
        self.write(name,self.originals[name])
        self.assertEqual(installer.repair_inputs(self.root)[0],self.originals)
        self.write(name,b'changed legacy audio')
        with self.assertRaisesRegex(ValueError,'retained original'):installer.repair_inputs(self.root)

    def test_changed_retained_audio_cannot_fall_back_to_another_original(self):
        name=gold.EXE.with_name('mss32.dll').as_posix()
        self.write(name,self.originals[name])
        self.write(gold.EXE.with_name('weplay_miles_original.dll'),b'changed retained audio')
        with self.assertRaisesRegex(ValueError,'retained original'):installer.repair_inputs(self.root)

    def test_repair_rebuilds_only_original_inventory_and_rechecks_unchanged_assets(self):
        originals,files=installer.repair_inputs(self.root)
        self.write('Data/unowned.fixture',b'not adopted')
        output=self.root.parent/'New'
        patches=(patch.object(gold,'inspect',return_value=dict(unlocked_exe_sha256=sha(b'prepared exe'))),
                 patch.object(gold,'unlock_bytes',return_value=b'prepared exe'),
                 patch.object(gold,'menu_bytes',return_value=b'clean interface'),
                 patch.object(gold,'shared_bytes',return_value=self.originals[gold.SHARED.as_posix()]),
                 patch.object(gold,'MENU_SHA256',sha(b'clean interface')))
        from contextlib import ExitStack
        with ExitStack() as stack:
            for item in patches:stack.enter_context(item)
            result=gold.prepare(self.root,output,originals=originals,source_files=files)
            self.assertEqual(result['source_files'],self.files)
            self.assertEqual((output/'Backup/Settlers7R.original.exe').read_bytes(),self.originals[gold.EXE.as_posix()])
            self.assertFalse((output/'Data/unowned.fixture').exists())
            self.assertFalse((output/'LocalData').exists())
            self.write(self.asset,b'changed retained asset')
            with self.assertRaisesRegex(ValueError,'copy verification'):
                gold.prepare(self.root,self.root.parent/'Refused',originals=originals,source_files=files)
        self.assertEqual((self.root/'LocalData/ProfileRoot/Profiles/Savegames/keep.s7s').read_bytes(),b'invented user save')


if __name__=='__main__':unittest.main()
