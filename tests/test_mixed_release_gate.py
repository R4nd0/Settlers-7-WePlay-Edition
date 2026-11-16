"""Invented receipt fixtures; no game, key access, signature or publication."""
import copy
import hashlib
import json
from pathlib import Path
import sys,tempfile,unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/updates'),str(ROOT/'tools/installer')]
import mixed_release_gate as gate
import build_release as signer
import release_policy


class MixedReleaseGateTests(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory(dir=ROOT/'work');self.addCleanup(temporary.cleanup)
        self.root=Path(temporary.name);self.setup=self.root/'Setup.exe'
        self.setup.write_bytes(b'MZ invented installer fixture, never execute')
        sha=hashlib.sha256(self.setup.read_bytes()).hexdigest()
        self.build=dict(format='weplay-main-package-release-v1',platform_isolation=release_policy.CONTRACT,
            setup_sha256=sha,setup_bytes=self.setup.stat().st_size,source_commit='1'*40,
            package_id='2'*64,source_tree_dirty=False,crossplay_mixed=True)
        self.proof=dict(format='weplay-final-mixed-reload-v2',version=gate.VERSION,sequence=gate.SEQUENCE,
            tag=gate.TAG,setup_sha256=sha,setup_bytes=self.setup.stat().st_size,source_commit='1'*40,
            package_id='2'*64,catalog_sha256='3'*64,cases=[])
        self.cases={}
        gold=gate.platform_standalone.GOLD;history=gate.platform_standalone.HISTORY_PINS[(True,True)]
        self.enterContext(patch.object(gate.crossplay_content,'APPROVED_DIGESTS',frozenset(('3'*64,))))
        self.enterContext(patch.dict(gate.crossplay_session.ADAPTER_PINS,{1:(gold,),2:(history,),3:(history,)}))
        exe=lambda edition:next(key for key,value in gate.crossplay_session.EXECUTABLE_EDITIONS.items() if value==edition)
        for host,guest,name in gate.CASES:
            edition=guest if host==1 else host
            self.cases[name]=dict(format='weplay-final-mixed-reload-case-v2',host_edition=host,guest_edition=guest,
                installed_from_setup_sha256=sha,installed_package_id='2'*64,source_commit='1'*40,catalog_sha256='3'*64,
                gold_exe_sha256=exe(1),history_exe_sha256=exe(edition),gold_adapter_sha256=gold,
                history_adapter_sha256=history,normal_production_launch=True,fresh_host_save_written=True,
                mixed_reload_completed=True,continued_logistics_observed=True,original_crc_source='native-end-of-turn',
                shared_crc_blocks=1200,crc_mismatches=0,synthetic_turns=0,neo_features=19,neo_enabled=True,
                neo_state_preserved=True,gold_exit_code=0,history_exit_code=0,forced_termination=False,
                restoration_verified=True,native_lab_features=False,reviewed_evidence_sha256='4'*64)
        self.write()

    def write(self):
        self.proof['cases']=[]
        for host,guest,name in gate.CASES:
            filename='Setup.mixed-reload-'+name+'.json';raw=json.dumps(self.cases[name],sort_keys=True).encode()
            (self.root/filename).write_bytes(raw)
            self.proof['cases'].append(dict(host_edition=host,guest_edition=guest,file=filename,sha256=hashlib.sha256(raw).hexdigest()))
        self.setup.with_suffix('.mixed-reload.json').write_text(json.dumps(self.proof))
        self.setup.with_suffix('.release.json').write_text(json.dumps(self.build))

    def check(self,**changes):
        gate.check(self.setup,self.build,**dict(dict(version=gate.VERSION,sequence=gate.SEQUENCE,tag=gate.TAG),**changes))

    def test_complete_reviewed_identity_binding_passes_and_changed_evidence_fails(self):
        self.check()
        path=self.root/'Setup.mixed-reload-steam.json';path.write_bytes(path.read_bytes()+b' ')
        with self.assertRaisesRegex(ValueError,'exact final installed'):self.check()

    def test_signer_rejects_missing_proof_before_any_key_or_signing_process(self):
        self.setup.with_suffix('.mixed-reload.json').unlink()
        with patch.object(signer.subprocess,'run') as process:
            with self.assertRaisesRegex(ValueError,'both original History'):
                signer.build(self.setup,self.root/'weplay-update.json',version=gate.VERSION,
                    sequence=gate.SEQUENCE,tag=gate.TAG,key_path=self.root/'nonexistent-fixture-key')
        process.assert_not_called();self.assertFalse((self.root/'weplay-update.json').exists())
        self.assertFalse((self.root/signer.update_client.ISOLATED_MANIFEST).exists())

    def test_missing_either_edition_duplicate_or_swapped_cases_and_changed_setup_fail(self):
        original=copy.deepcopy(self.proof['cases'])
        for records in (original[:2],original[1:],original[::2],original[::-1],[original[0]]*3):
            self.proof['cases']=records;self.setup.with_suffix('.mixed-reload.json').write_text(json.dumps(self.proof))
            with self.assertRaises(ValueError):self.check()
        self.write();self.setup.write_bytes(b'MZ different final installer')
        with self.assertRaises(ValueError):self.check()

    def test_unapproved_catalog_or_nonproduction_adapter_never_passes(self):
        with patch.object(gate.crossplay_content,'APPROVED_DIGESTS',frozenset()):
            with self.assertRaises(ValueError):self.check()
        with patch.dict(gate.crossplay_session.ADAPTER_PINS,{3:()}):
            with self.assertRaises(ValueError):self.check()
        self.cases['steam']['history_adapter_sha256']='5'*64;self.write()
        with self.assertRaises(ValueError):self.check()

    def test_incomplete_dirty_wrong_package_and_noncanonical_release_identifiers_fail(self):
        original=copy.deepcopy(self.build)
        for changes in ({'crossplay_mixed':False},{'source_tree_dirty':True},{'source_tree_dirty':None},
                        {'package_id':'9'*64},{'source_commit':'8'*40}):
            self.build=dict(original,**changes)
            with self.assertRaises(ValueError):self.check()
        self.build=original
        for changes in ({'version':'RC4.00'},{'tag':'v2.0.0-rc4.0'},{'sequence':30},
                        {'version':'2.0.0-rc3.25','sequence':30,'tag':'RC4.00'}):
            with self.assertRaises(ValueError):self.check(**changes)

    def test_synthetic_counters_bool_numbers_unfinished_reload_or_abnormal_exit_fail(self):
        original=copy.deepcopy(self.cases['steam'])
        changes=({'normal_production_launch':False},{'fresh_host_save_written':False},
            {'mixed_reload_completed':False},{'continued_logistics_observed':False},
            {'shared_crc_blocks':0},{'shared_crc_blocks':True},{'crc_mismatches':1},
            {'synthetic_turns':1},{'original_crc_source':'synthetic'},{'neo_features':5},
            {'neo_enabled':1},{'neo_state_preserved':False},{'gold_exit_code':True},
            {'history_exit_code':7},{'forced_termination':True},{'restoration_verified':False},
            {'native_lab_features':True},{'reviewed_evidence_sha256':'0'*64})
        for change in changes:
            self.cases['steam']=dict(original,**change);self.write()
            with self.subTest(change=change),self.assertRaises(ValueError):self.check()

    def test_reverse_case_requires_steam_host_gold_guest_and_its_actual_host_save_reload(self):
        original=copy.deepcopy(self.cases['steam-host'])
        for change in ({'host_edition':1,'guest_edition':3},{'host_edition':2},
                       {'host_edition':True},{'guest_edition':True},{'fresh_host_save_written':False},
                       {'mixed_reload_completed':False},{'history_exit_code':9},{'crc_mismatches':1}):
            self.cases['steam-host']=dict(original,**change);self.write()
            with self.subTest(change=change),self.assertRaises(ValueError):self.check()
        self.cases['steam-host']=original;self.write()
        self.check()
        (self.root/'Setup.mixed-reload-steam-host.json').unlink()
        with self.assertRaises(ValueError):self.check()

    def test_old_forward_only_schema_cannot_qualify_the_expanded_rc4_scope(self):
        self.proof['format']='weplay-final-mixed-reload-v1'
        self.setup.with_suffix('.mixed-reload.json').write_text(json.dumps(self.proof))
        with self.assertRaises(ValueError):self.check()

    def test_duplicate_json_and_oversized_receipts_fail_and_previous_releases_are_unchanged(self):
        path=self.setup.with_suffix('.mixed-reload.json')
        path.write_text('{"format":"first","format":"second"}')
        with self.assertRaises(ValueError):self.check()
        path.write_bytes(b' '*(1024*1024+1))
        with self.assertRaises(ValueError):self.check()
        self.check(version='2.0.0-rc3.25',sequence=25,tag='v2.0.0-rc3.25')


if __name__=='__main__':unittest.main()
