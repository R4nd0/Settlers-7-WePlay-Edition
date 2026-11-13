"""Isolation cannot be lost by an otherwise valid package/update/repair operation."""
import json
from pathlib import Path
import sys,tempfile,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/installer'))
import release_policy as policy
import install


class IsolationPolicyTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(dir=ROOT/'work');self.addCleanup(self.temp.cleanup)
        self.app=Path(self.temp.name)/'App';self.app.mkdir()

    def write(self,name,value):
        path=self.app/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(value))

    def test_every_main_package_requires_all_three_editions(self):
        value=dict(main_package=True,platform_independent=True,gold_standalone=True,platform_isolation=policy.CONTRACT)
        policy.check_package(self.app,value)
        for change in ({'platform_independent':False},{'gold_standalone':False},{'platform_isolation':None},
                       {'platform_isolation':dict(revision=True,editions=policy.CONTRACT['editions'])},
                       {'platform_isolation':dict(revision=1,editions=['history-steam','history-ubisoft'])}):
            with self.subTest(change=change),self.assertRaisesRegex(ValueError,'platform_isolation_required'):
                policy.check_package(self.app,dict(value,**change))

    def test_package_floor_survives_removed_release_or_package_flag(self):
        self.write('State/'+policy.FLOOR,policy.CONTRACT)
        self.write('desktop-release.json',{'platform_independent':False})
        self.write('Package/package.json',{'platform_independent':False})
        self.assertTrue(policy.required(self.app))
        with self.assertRaises(ValueError):policy.check_package(self.app,{'platform_independent':False})
        with self.assertRaises(ValueError):policy.check_update(self.app,{})
        policy.check_update(self.app,{'platform_isolation':policy.CONTRACT})

    def test_initial_standalone_release_or_package_already_requires_isolation(self):
        for name in ('desktop-release.json','Package/package.json'):
            with self.subTest(name=name):
                self.write(name,{'platform_independent':True})
                with self.assertRaises(ValueError):policy.check_update(self.app,{})
                (self.app/name).unlink()

    def test_malformed_floor_does_not_disable_enforcement(self):
        self.write('State/'+policy.FLOOR,{'revision':0})
        with self.assertRaises(ValueError):policy.check_package(self.app,{'platform_independent':False})

    def test_damaged_gold_and_history_manifests_still_block_repair_downgrade(self):
        for name in ('Launcher/platform-standalone.json','Base/Bin/.weplay-isolation',
                     'Data/Base/_Dbg/Bin/Release/.weplay-isolation','Launcher/weplay_network_policy.exe',
                     'Base/Bin/weplay_miles_original.dll','Data/Base/_Dbg/Bin/Release/weplay_miles_original.dll'):
            with self.subTest(name=name):
                path=self.app/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'damaged')
                self.assertTrue(policy.protected_game(self.app))
                with self.assertRaises(ValueError):policy.preserve_game(self.app,{'platform_independent':False})
                path.unlink()

    def test_history_prepare_refuses_downgrade_before_destination_creation(self):
        self.write('local-bundle.json',{'platform_independent':True})
        target=self.app.parent/'NewCopy'
        with patch.object(install,'package_manifest',return_value={'platform_independent':False}):
            with self.assertRaisesRegex(ValueError,'platform_isolation_required'):
                install.prepare(self.app.parent/'Package',self.app,target)
        self.assertFalse(target.exists())
        self.assertTrue((self.app/'local-bundle.json').is_file())


if __name__=='__main__':unittest.main()
