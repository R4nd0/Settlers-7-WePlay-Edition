"""Invented three-client receipt fixtures; no game, key access or publication."""
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
        self.proof=dict(format='weplay-final-mixed-reload-v4',version=gate.VERSION,sequence=gate.SEQUENCE,
            tag=gate.TAG,setup_sha256=sha,setup_bytes=self.setup.stat().st_size,source_commit='1'*40,
            package_id='2'*64,catalog_sha256='3'*64,cases=[])
        self.cases={}
        gold=gate.platform_standalone.GOLD;history=gate.platform_standalone.HISTORY_PINS[(True,True)]
        self.enterContext(patch.object(gate.crossplay_content,'APPROVED_DIGESTS',frozenset(('3'*64,))))
        self.enterContext(patch.dict(gate.crossplay_session.ADAPTER_PINS,{1:(gold,),2:(history,),3:(history,)}))
        exe=lambda edition:next(key for key,value in gate.crossplay_session.EXECUTABLE_EDITIONS.items() if value==edition)
        for host,name in gate.CASES:
            session=hashlib.sha256((name+'-fresh').encode()).hexdigest()
            reload=hashlib.sha256((name+'-reload').encode()).hexdigest()
            participants=[dict(edition=edition,role='host' if edition==host else 'guest',
                exe_sha256=exe(edition),adapter_sha256=gold if edition==1 else history,
                installed_from_setup_sha256=sha,installed_package_id='2'*64,
                session_sha256=session,reload_session_sha256=reload,loaded_host_save_sha256='6'*64,
                original_save_loader_used=True,continued_logistics_observed=True,ai_opponent_observed=True,
                neo_state_preserved=True,renderer='dxvk',reload_renderer='dxvk',exit_code=0) for edition in gate.EDITIONS]
            self.cases[name]=dict(format='weplay-final-mixed-reload-case-v4',host_edition=host,
                installed_from_setup_sha256=sha,installed_package_id='2'*64,source_commit='1'*40,catalog_sha256='3'*64,
                participants=participants,human_players=3,ai_opponents=1,all_clients_simultaneous=True,
                session_sha256=session,reload_session_sha256=reload,host_save_sha256='6'*64,normal_speed=True,
                normal_production_launch=True,fresh_host_save_written=True,fresh_reload_processes=True,
                mixed_reload_completed=True,continued_logistics_observed=True,original_crc_source='native-end-of-turn',
                crc_compared_editions=[1,2,3],shared_crc_blocks_before_save=1200,shared_crc_blocks_after_reload=3000,
                post_reload_duration_ms=300000,stability_review_passed=True,crc_mismatches=0,synthetic_turns=0,
                neo_features=19,neo_enabled=True,neo_state_preserved=True,forced_termination=False,
                restoration_verified=True,native_lab_features=False,reviewed_evidence_sha256='4'*64)
        self.write()

    def write(self):
        self.proof['cases']=[]
        for host,name in gate.CASES:
            filename='Setup.mixed-reload-'+name+'.json';raw=json.dumps(self.cases[name],sort_keys=True).encode()
            (self.root/filename).write_bytes(raw)
            self.proof['cases'].append(dict(host_edition=host,file=filename,sha256=hashlib.sha256(raw).hexdigest()))
        self.setup.with_suffix('.mixed-reload.json').write_text(json.dumps(self.proof))
        self.setup.with_suffix('.release.json').write_text(json.dumps(self.build))

    def check(self,**changes):
        gate.check(self.setup,self.build,**dict(dict(version=gate.VERSION,sequence=gate.SEQUENCE,tag=gate.TAG),**changes))

    def test_complete_reviewed_identity_binding_passes_and_changed_evidence_fails(self):
        self.check()
        path=self.root/'Setup.mixed-reload-gold-host.json';path.write_bytes(path.read_bytes()+b' ')
        with self.assertRaisesRegex(ValueError,'exact final installed'):self.check()

    def test_candidate7_exception_requires_opt_in_and_exact_build(self):
        self.setup.with_suffix('.mixed-reload.json').unlink()
        allowed = tuple(self.build[key] for key in
                        ('setup_sha256', 'setup_bytes', 'source_commit', 'package_id'))
        with patch.object(gate, 'APPROVED_CANDIDATE7', allowed):
            self.check(approved_candidate7=True)
            with self.assertRaises(ValueError):
                self.check()
            with self.assertRaises(ValueError):
                self.check(approved_candidate7=True, tag='v2.0.0-rc3.25')
            original = self.setup.read_bytes()
            self.setup.write_bytes(bytes([original[0] ^ 1]) + original[1:])
            with self.assertRaises(ValueError):
                self.check(approved_candidate7=True)

    def test_signer_rejects_missing_proof_before_any_key_or_signing_process(self):
        self.setup.with_suffix('.mixed-reload.json').unlink()
        with patch.object(signer.subprocess,'run') as process:
            with self.assertRaisesRegex(ValueError,'each edition as host'):
                signer.build(self.setup,self.root/'weplay-update.json',version=gate.VERSION,
                    sequence=gate.SEQUENCE,tag=gate.TAG,key_path=self.root/'nonexistent-fixture-key')
        process.assert_not_called();self.assertFalse((self.root/'weplay-update.json').exists())
        self.assertFalse((self.root/signer.update_client.ISOLATED_MANIFEST).exists())

    def test_missing_duplicate_or_swapped_host_cases_and_changed_setup_fail(self):
        original=copy.deepcopy(self.proof['cases'])
        for records in (original[:1],original[1:],original[::2],original[::-1],[original[0]]*3):
            self.proof['cases']=records;self.setup.with_suffix('.mixed-reload.json').write_text(json.dumps(self.proof))
            with self.assertRaises(ValueError):self.check()
        self.write();self.setup.write_bytes(b'MZ different final installer')
        with self.assertRaises(ValueError):self.check()

    def test_unapproved_catalog_or_nonproduction_adapter_never_passes(self):
        with patch.object(gate.crossplay_content,'APPROVED_DIGESTS',frozenset()):
            with self.assertRaises(ValueError):self.check()
        for edition in gate.EDITIONS:
            with patch.dict(gate.crossplay_session.ADAPTER_PINS,{edition:()}):
                with self.assertRaises(ValueError):self.check()
        self.cases['gold-host']['participants'][1]['adapter_sha256']='5'*64;self.write()
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

    def test_wrong_roster_or_separate_sessions_cannot_substitute_for_three_clients(self):
        original=copy.deepcopy(self.cases['gold-host'])
        for change in ({'human_players':2,'ai_opponents':2},{'human_players':4,'ai_opponents':0},
                       {'human_players':True},{'ai_opponents':True},{'all_clients_simultaneous':False},
                       {'session_sha256':'0'*64},{'reload_session_sha256':original['session_sha256']},
                       {'crc_compared_editions':[1,3]},{'crc_compared_editions':[True,2,3]}):
            self.cases['gold-host']=dict(original,**change);self.write()
            with self.subTest(change=change),self.assertRaises(ValueError):self.check()
        for participants in (original['participants'][:2],original['participants'][1:],
                             original['participants'][::-1],[original['participants'][0]]*3):
            self.cases['gold-host']=dict(original,participants=participants);self.write()
            with self.assertRaises(ValueError):self.check()

    def test_each_edition_must_load_same_host_save_in_same_package_and_session(self):
        original=copy.deepcopy(self.cases['gold-host'])
        for index in range(3):
            for change in ({'edition':True},{'exe_sha256':'a'*64},{'adapter_sha256':'b'*64},
                           {'role':'spectator'},{'installed_from_setup_sha256':'7'*64},
                           {'installed_package_id':'8'*64},{'session_sha256':'9'*64},
                           {'reload_session_sha256':'a'*64},{'loaded_host_save_sha256':'b'*64},
                           {'original_save_loader_used':False},{'continued_logistics_observed':False},
                           {'ai_opponent_observed':False},{'neo_state_preserved':False},
                           {'renderer':'legacy'},{'reload_renderer':'legacy'},
                           {'exit_code':True},{'exit_code':7}):
                self.cases['gold-host']=copy.deepcopy(original)
                self.cases['gold-host']['participants'][index].update(change);self.write()
                with self.subTest(edition=index+1,change=change),self.assertRaises(ValueError):self.check()

    def test_synthetic_counters_unfinished_reload_or_unreviewed_stability_fail(self):
        original=copy.deepcopy(self.cases['gold-host'])
        changes=({'normal_production_launch':False},{'fresh_host_save_written':False},
            {'mixed_reload_completed':False},{'fresh_reload_processes':False},{'continued_logistics_observed':False},
            {'shared_crc_blocks_before_save':0},{'shared_crc_blocks_after_reload':0},
            {'shared_crc_blocks_after_reload':True},{'post_reload_duration_ms':0},
            {'post_reload_duration_ms':True},{'stability_review_passed':False},{'normal_speed':False},
            {'crc_mismatches':1},{'synthetic_turns':1},{'original_crc_source':'synthetic'},{'neo_features':5},
            {'neo_enabled':1},{'neo_enabled':False},{'neo_state_preserved':False},{'forced_termination':True},
            {'restoration_verified':False},{'native_lab_features':True},{'reviewed_evidence_sha256':'0'*64})
        for change in changes:
            self.cases['gold-host']=dict(original,**change);self.write()
            with self.subTest(change=change),self.assertRaises(ValueError):self.check()

    def test_steam_host_case_requires_all_three_editions_and_actual_host_save(self):
        original=copy.deepcopy(self.cases['steam-host'])
        for change in ({'host_edition':1},{'host_edition':2},{'host_edition':True},
                       {'fresh_host_save_written':False},{'mixed_reload_completed':False}):
            self.cases['steam-host']=dict(original,**change);self.write()
            with self.subTest(change=change),self.assertRaises(ValueError):self.check()
        self.cases['steam-host']=copy.deepcopy(original);self.write()
        self.cases['steam-host']['participants'][0]['role']='host';self.write()
        with self.assertRaises(ValueError):self.check()
        self.cases['steam-host']=original;self.write()
        (self.root/'Setup.mixed-reload-steam-host.json').unlink()
        with self.assertRaises(ValueError):self.check()

    def test_older_schemas_are_rejected_even_when_hashes_are_current(self):
        for version in (1,2,3):
            self.proof['format']='weplay-final-mixed-reload-v'+str(version)
            self.setup.with_suffix('.mixed-reload.json').write_text(json.dumps(self.proof))
            with self.assertRaises(ValueError):self.check()
        self.proof['format']='weplay-final-mixed-reload-v4'
        for version in (1,2,3):
            self.cases['gold-host']['format']='weplay-final-mixed-reload-case-v'+str(version);self.write()
            with self.assertRaises(ValueError):self.check()

    def test_duplicate_json_and_oversized_receipts_fail_and_previous_releases_are_unchanged(self):
        path=self.setup.with_suffix('.mixed-reload.json')
        path.write_text('{"format":"first","format":"second"}')
        with self.assertRaises(ValueError):self.check()
        path.write_bytes(b' '*(1024*1024+1))
        with self.assertRaises(ValueError):self.check()
        self.check(version='2.0.0-rc3.25',sequence=25,tag='v2.0.0-rc3.25')

if __name__=='__main__':unittest.main()
