"""Updater transport and owned-cache fault tests; no executable is launched."""
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/installer'))
import update_client as client


class UpdateResilienceTests(unittest.TestCase):
    def setUp(self):
        base=Path(os.environ.get('S7_TEST_TEMP_ROOT',str(ROOT/'work/test-temp')));base.mkdir(parents=True,exist_ok=True)
        temp=tempfile.TemporaryDirectory(dir=base);self.addCleanup(temp.cleanup)
        self.app=Path(temp.name)/'App';self.app.mkdir()
        (self.app/'desktop-release.json').write_bytes(client.canonical(dict(version='2.0.0-rc3.22',update_sequence=22,update_channel='preview')))
        _,self.updates=client.paths(self.app)
        self.raw=(ROOT/'tests/fixtures/updates/rc323-signed-update.json').read_bytes()
        self.value=client.verify(self.raw)

    def listing(self,*tags):
        return client.canonical([dict(draft=False,tag_name=tag,assets=[dict(name=client.MANIFEST,
            browser_download_url='https://github.com/'+client.REPOSITORY+'/releases/download/'+tag+'/'+client.MANIFEST)]) for tag in tags])

    def test_valid_newest_offer_survives_older_transport_failure(self):
        # Checkout line endings can change the JSON envelope's whitespace.
        # They must not alter the signed payload, its verification or caching.
        for ending in (b'\n',b'\r\n'):
            raw=self.raw.replace(b'\r\n',b'\n').rstrip(b'\n')+ending
            with self.subTest(envelope_line_ending=repr(ending)):
                def fetch(url,limit):
                    if url==client.RELEASES:return self.listing(self.value['tag'],'older-release')
                    if self.value['tag'] in url:return raw
                    raise client.UpdateError('update_download_failed')
                result=client.check(self.app,fetch=fetch)
                self.assertTrue(result['available']);self.assertTrue(result['incomplete'])
                self.assertEqual(result['sequence'],23)
                cached=(self.updates/'verified-envelope.json').read_bytes()
                self.assertEqual(client.decode(cached),client.decode(raw))
                self.assertEqual(client.verify(cached),self.value)

    def test_transport_failure_cannot_claim_current_when_no_candidate_verified(self):
        def fetch(url,limit):
            if url==client.RELEASES:return self.listing(self.value['tag'])
            raise client.UpdateError('update_download_failed')
        with self.assertRaisesRegex(client.UpdateError,'download_failed'):client.check(self.app,fetch=fetch)
        self.assertFalse((self.updates/'verified-envelope.json').exists())

    def test_tampered_older_manifest_is_still_fatal_after_valid_candidate(self):
        changed=json.loads(self.raw);changed['signature']='A'+changed['signature'][1:]
        def fetch(url,limit):
            if url==client.RELEASES:return self.listing(self.value['tag'],'older-release')
            return self.raw if self.value['tag'] in url else client.canonical(changed)
        with self.assertRaisesRegex(client.UpdateError,'signature_invalid'):client.check(self.app,fetch=fetch)
        self.assertFalse((self.updates/'verified-envelope.json').exists())

    def test_incomplete_check_retains_highwater_and_equivocation_guards(self):
        client.atomic(self.updates/'highest.json',dict(sequence=24,payload_sha256='a'*64))
        def fetch(url,limit):
            if url==client.RELEASES:return self.listing(self.value['tag'],'older-release')
            if self.value['tag'] in url:return self.raw
            raise client.UpdateError('update_download_failed')
        with self.assertRaisesRegex(client.UpdateError,'downgrade'):client.check(self.app,fetch=fetch)
        (self.updates/'highest.json').unlink()
        modified=dict(self.value,tag='other-signed-tag',notes='different signed contents')
        with patch.object(client,'verify',side_effect=[self.value,modified]),self.assertRaisesRegex(client.UpdateError,'invalid'):
            client.check(self.app,fetch=lambda url,limit:self.listing(self.value['tag'],modified['tag']) if url==client.RELEASES else b'algorithm fixture')

    def test_conflicting_older_sequence_is_not_hidden_by_higher_sequence(self):
        old=dict(self.value,sequence=22,tag='older-a');changed=dict(old,tag='older-b',notes='conflicting signed contents')
        with patch.object(client,'verify',side_effect=[self.value,old,changed]),self.assertRaisesRegex(client.UpdateError,'invalid'):
            client.check(self.app,fetch=lambda url,limit:self.listing(self.value['tag'],old['tag'],changed['tag']) if url==client.RELEASES else b'algorithm fixture')

    def prepare_fault(self,*,unknown=False):
        raw=b'owned installer fixture';value=dict(sequence=23,version='2.0.0-rc3.23',setup=dict(size=len(raw),sha256=hashlib.sha256(raw).hexdigest(),url='fixture'))
        (self.updates/'verified-envelope.json').write_bytes(b'{}')
        client.atomic(self.updates/'highest.json',dict(sequence=23,payload_sha256=hashlib.sha256(client.canonical(value)).hexdigest()))
        def process(pid):return dict(pid=pid,path=str(self.app/('S7Launcher.exe' if pid==os.getppid() else 'Package/Runtime/python/python.exe')),started_ticks=1)
        def fetch(url,limit,**kwargs):
            kwargs['destination'].write_bytes(raw)
            if unknown:(kwargs['destination'].parent/'personal.txt').write_bytes(b'keep')
            return len(raw),hashlib.sha256(raw).hexdigest()
        with patch.object(client,'verify',return_value=value),patch.object(client,'handoff_source',side_effect=client.UpdateError('update_invalid')):
            with self.assertRaisesRegex(client.UpdateError,'invalid'):
                client.prepare(self.app,os.getppid(),1,fetch=fetch,process_reader=process)

    def test_failure_after_verified_rename_removes_fresh_owned_completed_installer(self):
        self.prepare_fault()
        self.assertEqual(list(self.updates.glob('*/Setup.exe')),[])
        self.assertEqual(list(self.updates.glob('*/Setup.partial')),[])

    def test_unexpected_file_preserves_entire_failed_job(self):
        self.prepare_fault(unknown=True)
        self.assertEqual(len(list(self.updates.glob('*/personal.txt'))),1)
        self.assertEqual(len(list(self.updates.glob('*/Setup.exe'))),1)

    def job(self,index,created,*,pid=100,ready=None):
        job=self.updates/f'{index:032x}';job.mkdir();(job/'Setup.exe').write_bytes(b'owned bytes')
        client.atomic(job/'job-owner.json',dict(format=client.JOB_FORMAT,app=hashlib.sha256(str(self.app).casefold().encode()).hexdigest(),
            job=job.name,created=created,creator_pid=pid,files={'Setup.exe':client.compatibility.digest(job/'Setup.exe')}))
        if ready is not None:client.atomic(job/'ready.json',dict(format='weplay-update-ready-v1',helper_pid=ready))
        return job

    def test_reaper_enforces_count_age_and_size_only_for_inactive_owned_jobs(self):
        now=int(time.time());old=self.job(1,now-100);new=self.job(2,now-50);newest=self.job(3,now-10)
        self.assertEqual(client.reclaim_jobs(self.app,now=now,alive=lambda pid:False,max_jobs=2),1)
        self.assertFalse(old.exists());self.assertTrue(new.exists());self.assertTrue(newest.exists())
        self.assertEqual(client.reclaim_jobs(self.app,now=now,alive=lambda pid:False,max_bytes=11),1)
        self.assertFalse(new.exists());self.assertTrue(newest.exists())
        self.assertEqual(client.reclaim_jobs(self.app,now=now,alive=lambda pid:False,max_age=5),1)

    def test_reaper_preserves_active_helper_creator_unknown_and_changed_files(self):
        now=int(time.time());creator=self.job(1,now-9999,pid=222);helper=self.job(2,now-9999,ready=333)
        changed=self.job(3,now-9999);(changed/'Setup.exe').write_bytes(b'modified user file')
        unknown=self.job(4,now-9999);(unknown/'personal.txt').write_bytes(b'keep')
        legacy=self.updates/('5'*32);legacy.mkdir();(legacy/'Setup.exe').write_bytes(b'unowned legacy')
        self.assertEqual(client.reclaim_jobs(self.app,now=now,alive=lambda pid:pid in (222,333),max_age=1,max_jobs=0),0)
        self.assertTrue(all(p.exists() for p in (creator,helper,changed,unknown,legacy)))

    def test_reaper_never_follows_linked_job_or_file(self):
        now=int(time.time());outside=self.app.parent/'outside';outside.mkdir();keep=outside/'Setup.exe';keep.write_bytes(b'outside')
        job=self.job(1,now-9999);(job/'Setup.exe').unlink()
        try:(job/'Setup.exe').symlink_to(keep)
        except OSError as error:self.skipTest('Symlink privilege unavailable: '+str(error))
        self.assertEqual(client.reclaim_jobs(self.app,now=now,alive=lambda pid:False,max_jobs=0),0)
        self.assertEqual(keep.read_bytes(),b'outside');self.assertTrue(job.exists())


if __name__=='__main__':unittest.main()
