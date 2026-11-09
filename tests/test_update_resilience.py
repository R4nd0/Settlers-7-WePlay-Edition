"""Updater transport and owned-cache fault tests; no executable is launched."""
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
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
                def fetch(url,limit,**kwargs):
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
        def fetch(url,limit,**kwargs):
            if url==client.RELEASES:return self.listing(self.value['tag'])
            raise client.UpdateError('update_download_failed')
        with self.assertRaisesRegex(client.UpdateError,'download_failed'):client.check(self.app,fetch=fetch)
        self.assertFalse((self.updates/'verified-envelope.json').exists())

    def test_tampered_older_manifest_is_still_fatal_after_valid_candidate(self):
        changed=json.loads(self.raw);changed['signature']='A'+changed['signature'][1:]
        def fetch(url,limit,**kwargs):
            if url==client.RELEASES:return self.listing(self.value['tag'],'older-release')
            return self.raw if self.value['tag'] in url else client.canonical(changed)
        with self.assertRaisesRegex(client.UpdateError,'signature_invalid'):client.check(self.app,fetch=fetch)
        self.assertFalse((self.updates/'verified-envelope.json').exists())

    def test_incomplete_check_retains_highwater_and_equivocation_guards(self):
        client.atomic(self.updates/'highest.json',dict(sequence=24,payload_sha256='a'*64))
        def fetch(url,limit,**kwargs):
            if url==client.RELEASES:return self.listing(self.value['tag'],'older-release')
            if self.value['tag'] in url:return self.raw
            raise client.UpdateError('update_download_failed')
        with self.assertRaisesRegex(client.UpdateError,'downgrade'):client.check(self.app,fetch=fetch)
        (self.updates/'highest.json').unlink()
        modified=dict(self.value,tag='other-signed-tag',notes='different signed contents')
        with patch.object(client,'verify',side_effect=[self.value,modified]),self.assertRaisesRegex(client.UpdateError,'invalid'):
            client.check(self.app,fetch=lambda url,limit,**kw:self.listing(self.value['tag'],modified['tag']) if url==client.RELEASES else b'algorithm fixture')

    def test_conflicting_older_sequence_is_not_hidden_by_higher_sequence(self):
        old=dict(self.value,sequence=22,tag='older-a');changed=dict(old,tag='older-b',notes='conflicting signed contents')
        with patch.object(client,'verify',side_effect=[self.value,old,changed]),self.assertRaisesRegex(client.UpdateError,'invalid'):
            client.check(self.app,fetch=lambda url,limit,**kw:self.listing(self.value['tag'],old['tag'],changed['tag']) if url==client.RELEASES else b'algorithm fixture')

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

    def interrupted_cleanup(self):
        job=self.job(40,1)
        (job/'handoff.json').write_bytes(b'owned handoff')
        marker=client.decode((job/'job-owner.json').read_bytes())
        marker['files']=client.job_inventory(job);client.atomic(job/'job-owner.json',marker)
        original=Path.unlink;removed=[]
        def fail_second(path,*args,**kwargs):
            if path.parent==job and path.name in marker['files']:
                if removed:raise PermissionError('synthetic sharing violation')
                removed.append(path.name)
            return original(path,*args,**kwargs)
        with patch.object(Path,'unlink',fail_second):
            self.assertEqual(client.reclaim_jobs(self.app,now=9999999,alive=lambda _:False),0)
        self.assertEqual(len(removed),1)
        self.assertEqual(client.decode((job/'job-owner.json').read_bytes())['format'],client.DELETING_FORMAT)
        return job,removed[0]

    def test_partial_cleanup_resumes_after_restart_even_inside_retention_limits(self):
        job,_=self.interrupted_cleanup()
        self.assertEqual(client.reclaim_jobs(self.app,now=10,alive=lambda _:False),1)
        self.assertFalse(job.exists())

    def test_resumed_cleanup_preserves_added_modified_and_replaced_files(self):
        for fault in ('added','modified','replaced'):
            with self.subTest(fault=fault):
                job,deleted=self.interrupted_cleanup()
                remaining=next(p for p in job.iterdir() if p.name!='job-owner.json')
                target=job/'personal.txt' if fault=='added' else remaining if fault=='modified' else job/deleted
                target.write_bytes(b'foreign bytes')
                before={p.name:p.read_bytes() for p in job.iterdir()}
                self.assertEqual(client.reclaim_jobs(self.app,now=9999999,alive=lambda _:False),0)
                self.assertEqual({p.name:p.read_bytes() for p in job.iterdir()},before)
                # Fixture teardown, not product cleanup; use a new isolated job.
                for p in job.iterdir():p.unlink()
                job.rmdir()

    def test_update_check_never_enters_destructive_maintenance(self):
        job=self.job(50,1)
        with patch.object(client,'reclaim_jobs',side_effect=AssertionError('read path called cleanup')):
            result=client.check(self.app,fetch=lambda *a,**kw:b'[]')
        self.assertFalse(result['available']);self.assertTrue((job/'Setup.exe').exists())

    def test_incomplete_legacy_receipt_is_not_adopted_as_a_deletion(self):
        job=self.job(51,1);(job/'Setup.exe').unlink()
        before=(job/'job-owner.json').read_bytes()
        self.assertEqual(client.reclaim_jobs(self.app,now=9999999,alive=lambda _:False),0)
        self.assertEqual((job/'job-owner.json').read_bytes(),before)

    def test_resumed_cleanup_preserves_link_substitution_and_active_creator(self):
        job,_=self.interrupted_cleanup()
        self.assertEqual(client.reclaim_jobs(self.app,now=9999999,alive=lambda _:True),0)
        remaining=next(p for p in job.iterdir() if p.name!='job-owner.json')
        outside=self.app/'outside';outside.write_bytes(b'foreign bytes');remaining.unlink()
        try:remaining.symlink_to(outside)
        except OSError as error:self.skipTest('Symlink privilege unavailable: '+str(error))
        self.assertEqual(client.reclaim_jobs(self.app,now=9999999,alive=lambda _:False),0)
        self.assertEqual(outside.read_bytes(),b'foreign bytes');self.assertTrue(job.exists())

    def test_total_budget_returns_verified_partial_and_next_check_finishes(self):
        now=[0.0];calls=[];progress=[]
        def fetch(url,limit,**kwargs):
            calls.append(url);self.assertEqual(kwargs['deadline'],120)
            if url==client.RELEASES:return self.listing(self.value['tag'],'slow','never')
            if self.value['tag'] in url:return self.raw
            now[0]=120;raise client.UpdateError('update_download_failed')
        result=client.check(self.app,fetch=fetch,clock=lambda:now[0],progress=lambda *a:progress.append(a))
        self.assertTrue(result['available']);self.assertTrue(result['incomplete']);self.assertEqual(len(calls),3)
        self.assertEqual(client.verify((self.updates/'verified-envelope.json').read_bytes()),self.value)
        self.assertGreaterEqual(len(progress),3)
        result=client.check(self.app,fetch=lambda url,limit,**kw:self.listing(self.value['tag']) if url==client.RELEASES else self.raw)
        self.assertTrue(result['available']);self.assertNotIn('incomplete',result)

    def test_total_budget_without_candidate_fails_and_cancellation_does_not_cache(self):
        now=[0.0]
        def listing(url,limit,**kwargs):
            now[0]=120;return self.listing(self.value['tag'])
        with self.assertRaisesRegex(client.UpdateError,'download_failed'):
            client.check(self.app,fetch=listing,clock=lambda:now[0])
        stop=[False]
        def fetch(url,limit,**kwargs):
            if url==client.RELEASES:return self.listing(self.value['tag'],'later')
            stop[0]=True;return self.raw
        with self.assertRaisesRegex(client.UpdateError,'cancelled'):
            client.check(self.app,fetch=fetch,cancelled=lambda:stop[0])
        self.assertFalse((self.updates/'verified-envelope.json').exists())

    def test_streaming_transfer_obeys_total_deadline_and_cancellation(self):
        for cancel in (False,True):
            now=[0.0];closed=[];stop=[False];timeouts=[]
            class Socket:
                def settimeout(self,value):timeouts.append(value)
            class Raw:
                _sock=Socket()
            class Buffered:
                raw=Raw()
            class Response:
                status=200
                fp=Buffered()
                def getheader(self,name):return None
                def read1(self,size):
                    now[0]+=1;stop[0]=cancel
                    return b'x'
            class Connection:
                sock=None
                def __init__(self,*a,**kw):pass
                def request(self,*a,**kw):pass
                def getresponse(self):return Response()
                def close(self):closed.append(True)
            with self.subTest(cancel=cancel),self.assertRaisesRegex(client.UpdateError,'cancelled' if cancel else 'download_failed'):
                client.transfer(client.RELEASES,100,connection_factory=Connection,clock=lambda:now[0],deadline=3,cancelled=lambda:stop[0])
            self.assertTrue(closed);self.assertLessEqual(now[0],3)
            self.assertEqual(timeouts,[3] if cancel else [3,2,1])

    def test_total_deadline_interrupts_a_blocked_header_read_and_closes_connection(self):
        interrupted=threading.Event();closed=[]
        class Socket:
            def settimeout(self,value):pass
            def shutdown(self,how):interrupted.set()
        class Connection:
            sock=Socket()
            def __init__(self,*a,**kw):pass
            def request(self,*a,**kw):pass
            def getresponse(self):
                if not interrupted.wait(5):raise AssertionError('deadline did not interrupt headers')
                raise OSError('deadline socket shutdown')
            def close(self):closed.append(True)
        with self.assertRaisesRegex(client.UpdateError,'download_failed'):
            client.transfer(client.RELEASES,100,connection_factory=Connection,deadline=time.monotonic()+.05)
        self.assertTrue(interrupted.is_set());self.assertEqual(closed,[True])


if __name__=='__main__':unittest.main()
