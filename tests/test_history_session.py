"""Owned History bootstrap lifetime; authored process failures, no game launch."""
import base64
import os
from pathlib import Path
import subprocess
import sys,tempfile,unittest
from unittest.mock import Mock,patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/installer'),str(ROOT/'tools/offline-ui')]
import history_session as service
import launch_local

ID='00112233-4455-6677-8899-aabbccddeeff'


class HistorySessionTests(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory(dir=ROOT/'work');self.addCleanup(temporary.cleanup)
        self.root=Path(temporary.name);self.data=self.root/'LocalData';self.data.mkdir()
        self.config={'public_key':base64.b64encode(b'ECS1'+bytes((32,0,0,0))+bytes(64)).decode(),
                     'prudp_urls':['prudp:/address=127.0.0.1;port=9111']}
        self.process=Mock();self.process.poll.return_value=None
        self.popen=self.enterContext(patch.object(service.subprocess,'Popen',return_value=self.process))
        self.ready=self.enterContext(patch.object(launch_local,'service_ready',return_value=(self.config,9111)))

    def owner(self):
        result=service.HistorySessionService(self.root,profile_id=ID,name='History Guest')
        self.addCleanup(result.close);return result

    def test_explicit_identity_never_reads_or_changes_persistent_solo_identity(self):
        identity=self.data/'local-profile-id.txt';identity.write_bytes(b'private solo file must stay unchanged')
        with patch.object(launch_local,'local_identity',side_effect=AssertionError('must not read solo identity')):
            owner=self.owner().start()
        content=(self.data/'s7-local-runtime.ini').read_text()
        self.assertIn('profile_id='+ID+'\n',content);self.assertIn('ticket=local-s7:'+ID+'\n',content)
        self.assertIn('display_name=History Guest\n',content);self.assertIn('connect_probe=1\n',content)
        command=self.popen.call_args.args[0]
        self.assertIn('--local-catalogue-probe',command);self.assertNotIn('--seconds',command)
        self.assertNotIn('--lan-address',command);self.assertNotIn('--lan-lobby',command)
        owner.close();self.assertFalse((self.data/'s7-local-runtime.ini').exists())
        self.assertEqual(identity.read_bytes(),b'private solo file must stay unchanged')
        self.process.terminate.assert_called_once();self.process.wait.assert_called_once_with(timeout=10)

    def test_previous_known_runtime_bytes_and_timestamps_are_restored(self):
        path=self.data/'s7-local-runtime.ini';old=b'format=s7-local-runtime-v1\nold-owned-config\n';path.write_bytes(old)
        os.utime(path,ns=(1700000000000000000,1700000000000000000));before=path.stat().st_mtime_ns
        owner=self.owner().start();self.assertNotEqual(path.read_bytes(),old)
        owner.close();self.assertEqual(path.read_bytes(),old);self.assertEqual(path.stat().st_mtime_ns,before)

    def test_changed_runtime_is_retained_and_cleanup_failure_is_reported(self):
        owner=self.owner().start();path=self.data/'s7-local-runtime.ini';path.write_bytes(b'newer different configuration')
        with self.assertRaisesRegex(RuntimeError,'cleanup_failed'):owner.close()
        self.assertEqual(path.read_bytes(),b'newer different configuration');self.assertTrue(owner.closed)
        self.process.terminate.assert_called_once()

    def test_unknown_existing_runtime_is_not_overwritten_and_no_service_starts(self):
        path=self.data/'s7-local-runtime.ini';path.write_bytes(b'unknown file')
        with self.assertRaises(ValueError):self.owner().start()
        self.popen.assert_not_called();self.assertEqual(path.read_bytes(),b'unknown file')

    def test_startup_failure_stops_only_owned_child_and_preserves_original_runtime(self):
        self.ready.side_effect=TimeoutError('fixture startup deadline')
        path=self.data/'s7-local-runtime.ini';path.write_bytes(b'format=s7-local-runtime-v1\nold\n')
        with self.assertRaises(TimeoutError):self.owner().start()
        self.assertEqual(path.read_bytes(),b'format=s7-local-runtime-v1\nold\n');self.process.terminate.assert_called_once()

    def test_early_service_exit_is_detected_and_closes_owned_runtime(self):
        owner=self.owner().start();self.process.poll.return_value=9
        with self.assertRaisesRegex(RuntimeError,'service_failed'):owner.pump(0)
        owner.close();self.process.terminate.assert_not_called()
        self.assertFalse((self.data/'s7-local-runtime.ini').exists())

    def test_history_cleanup_can_resume_after_child_exit_timeout(self):
        owner = self.owner().start()
        self.process.wait.side_effect = [subprocess.TimeoutExpired('fixture', 10),
                                         subprocess.TimeoutExpired('fixture', 5)]
        with self.assertRaises(subprocess.TimeoutExpired):
            owner.close()
        self.assertFalse(owner.closed)
        self.assertFalse(owner.lock.closed)
        self.assertTrue(owner.runtime.is_file())
        self.process.poll.return_value = 0
        owner.close()
        self.assertTrue(owner.closed)
        self.assertFalse(owner.runtime.exists())

    def test_restore_retries_temporary_sharing_conflict_after_shutdown(self):
        runtime = self.data / 's7-local-runtime.ini'
        previous = b'format=s7-local-runtime-v1\nprevious profile\n'
        runtime.write_bytes(previous)
        stamp = 1700000000000000000
        os.utime(runtime, ns=(stamp, stamp))
        owner = self.owner().start()
        move = Path.replace
        attempts = []
        def retry(path, target):
            attempts.append(target)
            if len(attempts) == 1:
                raise PermissionError('temporary sharing conflict')
            return move(path, target)
        with patch.object(Path, 'replace', retry):
            owner.close()
        self.assertEqual(runtime.read_bytes(), previous)
        self.assertEqual(runtime.stat().st_mtime_ns, stamp)
        self.assertEqual(attempts, [runtime, runtime])

    def test_parallel_owner_fails_without_stopping_the_first_service(self):
        first=self.owner().start()
        with self.assertRaises(OSError):self.owner().start()
        self.assertFalse(first.closed);self.process.terminate.assert_not_called();self.assertEqual(self.popen.call_count,1)

    def test_invalid_explicit_identity_cannot_create_configuration(self):
        for identity in ('0'*36,'00000000-0000-0000-0000-000000000000',ID.upper(),'not-a-guid',None):
            if identity is None:continue # None deliberately retains ordinary solo behavior in the shared writer.
            with self.subTest(identity=identity),self.assertRaises(ValueError):
                launch_local.write_configuration(self.data,self.config,9111,True,profile_id=identity)
        self.assertFalse((self.data/'s7-local-runtime.ini').exists())
        self.assertFalse((self.data/'local-profile-id.txt').exists())


if __name__=='__main__':unittest.main()
