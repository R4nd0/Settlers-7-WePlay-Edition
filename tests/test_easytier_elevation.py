import ctypes
import json
import subprocess
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/installer'))
sys.path.insert(0, str(ROOT / 'tools/offline-ui'))
import easytier_elevation as elevation
from internet_room import Room, InternetError
from lobby_directory import LanInterface

ROOM = Room('a' * 32, 'b' * 64, '10.203.77.0/24')


class ElevationTests(unittest.TestCase):
    def test_shell_execute_structure_has_native_windows_64_bit_layout(self):
        self.assertEqual(ctypes.sizeof(elevation.ShellExecuteInfo), 112 if ctypes.sizeof(ctypes.c_void_p) == 8 else 60)

    def test_atomic_status_replace_retries_a_windows_sharing_race(self):
        with tempfile.TemporaryDirectory() as folder:
            original=elevation.os.replace
            calls=[]
            def replace(source,target):
                calls.append(1)
                if len(calls)==1:raise PermissionError('fixture sharing violation')
                return original(source,target)
            with patch.object(elevation.os,'replace',side_effect=replace),patch.object(elevation.time,'sleep'):
                elevation.write_state(Path(folder),{'phase':'ready'})
            state = json.loads((Path(folder) / 'state.json').read_text())
            self.assertEqual(state['phase'], 'ready')
            self.assertIsInstance(state['written_monotonic_ns'], int)
            self.assertEqual(len(calls),2)

    def test_uac_cancellation_removes_request_and_starts_no_game(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for path in ('Runtime/python/python.exe', 'Launcher/easytier_elevation.py'):
                target = root / path; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(b'fixture')
            overlay = elevation.ElevatedEasyTier(root, ROOM, host=True)
            api = Mock()
            def cancel(python, script, request):
                self.assertNotIn(ROOM.secret, str(request))
                self.assertEqual(json.loads(request.read_text())['room'], ROOM.token())
                raise InternetError('internet_elevation_cancelled')
            with patch('easytier_runtime.verify_runtime'), \
                    patch.object(elevation,'verify_worker'), \
                    patch.object(elevation, 'parent_process', return_value=(api, 1, 123, Path(sys.executable))), \
                    patch.object(elevation, 'elevate', side_effect=cancel):
                with self.assertRaises(InternetError) as error: overlay.start()
            self.assertEqual(error.exception.code, 'internet_elevation_cancelled')
            self.assertEqual(list((root / 'LocalData').iterdir()), [])

    def test_state_requires_session_nonce_and_actual_overlay(self):
        with tempfile.TemporaryDirectory() as folder:
            overlay = elevation.ElevatedEasyTier(Path(folder), ROOM, host=False)
            overlay.private = Path(folder)
            interface = LanInterface('10.203.77.2', 24, 'WePlay-12345678')
            value = dict(format=elevation.FORMAT, nonce=overlay.nonce, phase='ready',
                         address=interface.address, device=interface.name, peers={ROOM.coordinator: 'p2p'})
            elevation.write_state(overlay.private, value)
            with patch.object(elevation, 'enumerate_interfaces', return_value=[interface]):
                self.assertEqual(overlay.snapshot(), (interface, {ROOM.coordinator: 'p2p'}))
                elevation.write_state(overlay.private, dict(value, nonce='x' * 32))
                with self.assertRaises(InternetError): overlay.snapshot()
                elevation.write_state(overlay.private, dict(value, peers={'192.168.1.2': 'p2p'}))
                with self.assertRaises(InternetError): overlay.snapshot()

    def test_snapshot_uses_elapsed_time_across_wall_clock_change(self):
        with tempfile.TemporaryDirectory() as folder:
            overlay = elevation.ElevatedEasyTier(Path(folder), ROOM, host=False)
            overlay.private = Path(folder)
            interface = LanInterface('10.203.77.2', 24, 'WePlay-12345678')
            state = dict(format=elevation.FORMAT, nonce=overlay.nonce, phase='ready',
                         address=interface.address, device=interface.name, peers={})
            with patch.object(elevation.time, 'monotonic_ns', return_value=1_000_000_000):
                elevation.write_state(overlay.private, state)
            with patch.object(elevation, 'enumerate_interfaces', return_value=[interface]), \
                    patch.object(elevation.time, 'time', return_value=10_000_000_000), \
                    patch.object(elevation.time, 'monotonic_ns', side_effect=[2_000_000_000, 12_000_000_000]):
                self.assertEqual(overlay.snapshot(), (interface, {}))
                with self.assertRaisesRegex(InternetError, 'internet_disconnected'):
                    overlay.snapshot()

    def test_missing_status_is_retryable_only_during_start(self):
        with tempfile.TemporaryDirectory() as folder:
            overlay = elevation.ElevatedEasyTier(Path(folder), ROOM, host=False)
            overlay.private = Path(folder)
            with self.assertRaises(FileNotFoundError):
                overlay.snapshot(wait_for_initial_state=True)
            with self.assertRaisesRegex(InternetError, 'internet_disconnected'):
                overlay.snapshot()

    def test_recent_ready_status_is_rejected_after_owned_worker_exit(self):
        with tempfile.TemporaryDirectory() as folder:
            overlay = elevation.ElevatedEasyTier(Path(folder), ROOM, host=False)
            overlay.private = Path(folder)
            overlay.handle = 123
            interface = LanInterface('10.203.77.2', 24, 'WePlay-12345678')
            value = dict(format=elevation.FORMAT, nonce=overlay.nonce, phase='ready',
                         address=interface.address, device=interface.name, peers={})
            elevation.write_state(overlay.private, value)
            api = Mock()
            api.WaitForSingleObject.return_value = 0
            with patch.object(elevation, 'enumerate_interfaces', return_value=[interface]), \
                    patch.object(elevation, 'winapi', return_value=api), \
                    self.assertRaisesRegex(InternetError, '^internet_disconnected$'):
                overlay.snapshot()
            api.WaitForSingleObject.assert_called_once_with(123, 0)

    def test_close_checks_worker_exit_and_cleans_owned_ipc(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            overlay = elevation.ElevatedEasyTier(root, ROOM, host=True)
            directory = root / 'LocalData/easytier-uac-fixture'
            directory.mkdir(parents=True)
            overlay.private, overlay.handle = directory, 123
            api = Mock()
            api.WaitForSingleObject.side_effect = [258, 0]
            def get_exit_code(_handle, result):
                result._obj.value = 2
                return True
            api.GetExitCodeProcess.side_effect = get_exit_code
            with patch.object(elevation, 'winapi', return_value=api):
                with self.assertRaisesRegex(InternetError, 'internet_cleanup_failed'):
                    overlay.close()
                self.assertTrue((directory / 'stop').exists())
                api.CloseHandle.assert_not_called()
                with self.assertRaisesRegex(InternetError, 'internet_cleanup_failed'):
                    overlay.close()
            api.CloseHandle.assert_called_once_with(123)
            self.assertIsNone(overlay.handle)
            self.assertFalse(directory.exists())

    def test_crossplay_state_must_echo_scope_and_tracks_verified_interface(self):
        with tempfile.TemporaryDirectory() as folder:
            overlay=elevation.ElevatedEasyTier(Path(folder),ROOM,host=False,crossplay=True)
            overlay.private=Path(folder)
            interface=LanInterface('10.203.77.2',24,'WePlay-12345678')
            value=dict(format=elevation.FORMAT,nonce=overlay.nonce,phase='ready',
                address=interface.address,device=interface.name,peers={ROOM.coordinator:'p2p'})
            elevation.write_state(overlay.private, value)
            with patch.object(elevation,'enumerate_interfaces',return_value=[interface]):
                with self.assertRaises(InternetError):overlay.snapshot()
                elevation.write_state(overlay.private, dict(value,crossplay=True))
                self.assertEqual(overlay.snapshot(),(interface,{ROOM.coordinator:'p2p'}))
                self.assertEqual((overlay.address,overlay.device_name),(interface.address,interface.name))
        for setting in (1,'true',None):
            with self.assertRaises(InternetError):elevation.ElevatedEasyTier(ROOT,ROOM,host=True,crossplay=setting)

    def test_crossplay_uac_request_is_explicit_and_owned_handle_liveness_is_used(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            for name in ('Runtime/python/python.exe','Launcher/easytier_elevation.py'):
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'fixture')
            overlay=elevation.ElevatedEasyTier(root,ROOM,host=True,crossplay=True)
            api=Mock()
            def cancel(_python,_script,request):
                self.assertIs(json.loads(request.read_text())['crossplay'],True)
                raise InternetError('internet_elevation_cancelled')
            with patch('easytier_runtime.verify_runtime'),patch.object(elevation,'verify_worker'), \
                    patch.object(elevation,'parent_process',return_value=(api,1,123,Path(sys.executable))), \
                    patch.object(elevation,'elevate',side_effect=cancel):
                with self.assertRaises(InternetError):overlay.start()
            self.assertFalse(overlay.alive())
            overlay.handle=123
            with patch.object(elevation,'winapi',return_value=api):
                api.WaitForSingleObject.return_value=0x102;self.assertTrue(overlay.alive())
                api.WaitForSingleObject.return_value=0;self.assertFalse(overlay.alive())

    def test_worker_refuses_arbitrary_request_path_before_opening_parent(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'other.json'; path.write_text('{}')
            with patch.object(elevation, 'parent_process') as parent:
                with self.assertRaises(InternetError): elevation.worker(path)
            parent.assert_not_called()

    def test_failed_worker_removes_secret_and_reaps_ipc_when_exact_parent_exited(self):
        for parent_exited in (False,True):
            with self.subTest(parent_exited=parent_exited),tempfile.TemporaryDirectory() as folder:
                root=Path(folder);private=root/'LocalData/easytier-uac-fixture';private.mkdir(parents=True)
                image=root/'Runtime/python/python.exe';image.parent.mkdir(parents=True);image.write_bytes(b'python')
                request=private/'request.json'
                request.write_text(json.dumps(dict(format=elevation.FORMAT,root=str(root),room=ROOM.token(),
                    host=True,parent_pid=123,parent_start=456,nonce='a'*32)))
                api=Mock();api.WaitForSingleObject.return_value=0 if parent_exited else 0x102
                overlay=Mock();overlay.start.side_effect=InternetError('internet_cancelled')
                with patch.object(elevation,'__file__',str(root/'Launcher/easytier_elevation.py')), \
                        patch.object(elevation,'parent_process',return_value=(api,1,456,image)), \
                        patch('easytier_runtime.EasyTier',return_value=overlay), \
                        patch('launch_local.verify_bundle'),patch.object(elevation,'firewall'):
                    elevation.worker(request)
                self.assertFalse(request.exists())
                self.assertEqual(private.exists(),not parent_exited)
                overlay.close.assert_called_once();api.CloseHandle.assert_called_once_with(1)

    def test_firewall_only_invokes_fixed_helper_with_structured_arguments(self):
        root = ROOT / 'work/fictional-internet-copy'
        interface = LanInterface('10.203.77.1', 24, 'WePlay-12345678')
        with patch.object(elevation.subprocess, 'run', return_value=Mock(returncode=0,stdout=b'{}')) as command:
            elevation.firewall(root, 'a' * 32, interface)
        args = command.call_args.args[0]
        self.assertEqual(Path(args[0]).name,'WePlay.WindowsHelper.exe')
        self.assertEqual(args[1:],['internet-firewall',str(root),'a'*32,'apply',interface.address,interface.name])
        self.assertNotIn(ROOM.secret, repr(command.call_args))
        self.assertNotIn('shell', command.call_args.kwargs)

    def test_crossplay_firewall_uses_fixed_scope_for_apply_and_removal(self):
        import windows_system
        root=ROOT/'work/fictional-internet-copy';key='a'*32
        interface=LanInterface('10.203.77.1',24,'WePlay-12345678')
        with patch.object(windows_system,'command',return_value=b'{}') as command:
            windows_system.internet_firewall(root,key,interface,crossplay=True)
            self.assertEqual(command.call_args.args,('internet-firewall',root,key,'apply',interface.address,interface.name,'crossplay'))
            windows_system.internet_firewall(root,key,crossplay=True)
            self.assertEqual(command.call_args.args,('internet-firewall',root,key,'remove','crossplay'))
            with self.assertRaises(ValueError):windows_system.internet_firewall(root,key,crossplay=1)

    def test_crossplay_adapter_cleanup_keeps_its_explicit_gold_scope(self):
        import windows_system
        root=ROOT/'work/fictional-internet-copy'
        with patch.object(windows_system,'command',return_value=b'{}') as command:
            windows_system.remove_adapter(root,'WePlay-12345678','10.203.77.1',crossplay=True)
            self.assertEqual(command.call_args.args,('adapter-cleanup',root,'WePlay-12345678','10.203.77.1','crossplay'))
            with self.assertRaises(ValueError):windows_system.remove_adapter(root,'WePlay-12345678','10.203.77.1',crossplay='true')

    def test_crossplay_worker_forwards_scope_after_approved_native_profile_and_cleans_up(self):
        import compatibility,crossplay_session
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);private=root/'LocalData/easytier-uac-fixture';private.mkdir(parents=True)
            image=root/'Runtime/python/python.exe';image.parent.mkdir(parents=True);image.write_bytes(b'python')
            executable=root/'Data/Base/_Dbg/Bin/Release/Settlers7R.exe'
            executable.parent.mkdir(parents=True);executable.write_bytes(b'fixture executable')
            adapter=root/'Launcher/weplay_gold_startup.dll'
            profile={'files':{'Launcher/weplay_gold_startup.dll':'approved adapter'}}
            request=private/'request.json'
            request.write_text(json.dumps(dict(format=elevation.FORMAT,root=str(root),room=ROOM.token(),
                host=True,parent_pid=123,parent_start=456,nonce='a'*32,crossplay=True)))
            api=Mock();api.WaitForSingleObject.return_value=0x102
            interface=LanInterface(ROOM.coordinator,24,'WePlay-12345678')
            overlay=Mock();overlay.start.return_value=interface;overlay.snapshot.return_value=(interface,{})
            def stop(_seconds):(private/'stop').touch()
            with patch.object(elevation,'__file__',str(root/'Launcher/easytier_elevation.py')), \
                    patch.object(elevation,'parent_process',return_value=(api,1,456,image)), \
                    patch('easytier_runtime.EasyTier',return_value=overlay) as factory, \
                    patch('platform_standalone.load',return_value=(profile,executable,adapter)), \
                    patch.dict(crossplay_session.EXECUTABLE_EDITIONS,{compatibility.digest(executable):1}), \
                    patch.dict(crossplay_session.ADAPTER_PINS,{1:('approved adapter',)}), \
                    patch.object(elevation.time,'sleep',side_effect=stop), \
                    patch.object(elevation,'write_state') as write,patch.object(elevation,'firewall') as firewall:
                elevation.worker(request)
            self.assertIs(factory.call_args.kwargs['crossplay'],True)
            firewall.assert_any_call(root,'a'*32,interface,crossplay=True)
            firewall.assert_any_call(root,'a'*32,crossplay=True)
            self.assertTrue(any(call.args[1].get('crossplay') is True for call in write.call_args_list))
            overlay.close.assert_called_once();self.assertFalse(request.exists())

    def test_temporary_status_command_timeout_does_not_end_owned_room(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            private = root / 'LocalData/easytier-uac-fixture'
            private.mkdir(parents=True)
            image = root / 'Runtime/python/python.exe'
            image.parent.mkdir(parents=True)
            image.write_bytes(b'python')
            request = private / 'request.json'
            request.write_text(json.dumps(dict(format=elevation.FORMAT, root=str(root), room=ROOM.token(),
                                               host=True, parent_pid=123, parent_start=456, nonce='a'*32)))
            api = Mock()
            api.WaitForSingleObject.return_value = 0x102
            interface = LanInterface(ROOM.coordinator, 24, 'WePlay-fixture')
            overlay = Mock(start=Mock(return_value=interface), snapshot=Mock(side_effect=[
                subprocess.TimeoutExpired('status fixture', 3), (interface, {})]))

            def publish(_directory, state):
                if state['phase'] == 'ready':
                    (private / 'stop').touch()

            with patch.object(elevation, '__file__', str(root / 'Launcher/easytier_elevation.py')), \
                    patch.object(elevation, 'parent_process', return_value=(api, 1, 456, image)), \
                    patch('easytier_runtime.EasyTier', return_value=overlay), \
                    patch('launch_local.verify_bundle'), patch.object(elevation, 'firewall'), \
                    patch.object(elevation.time, 'sleep'), \
                    patch.object(elevation, 'write_state', side_effect=publish) as write:
                elevation.worker(request)
            self.assertEqual([call.args[1]['phase'] for call in write.call_args_list], ['ready', 'error'])
            self.assertEqual(write.call_args.args[1]['code'], 'internet_cancelled')
            overlay.close.assert_called_once()

    def test_worker_imports_and_python_must_match_trusted_package_inventory(self):
        import compatibility, install
        with tempfile.TemporaryDirectory() as folder:
            package=Path(folder)/'Package';root=Path(folder)/'Game'
            sources=[('installer/easytier_elevation.py','easytier_elevation.py')]
            files={}
            for relative,data in [('Launcher/easytier_elevation.py',b'approved helper'),
                                  ('Launcher/LICENSE.txt',b'license'),('Runtime/python/python.exe',b'python'),
                                  ('Runtime/python/python314._pth',b'approved path')]:
                path=root/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
            files['tools/installer/easytier_elevation.py']=compatibility.digest(root/'Launcher/easytier_elevation.py')
            files['tools/license.txt']=compatibility.digest(root/'Launcher/LICENSE.txt')
            files.update(compatibility.inventory(root,('Runtime/python',)))
            with patch.object(elevation,'__file__',str(package/'Tools/installer/easytier_elevation.py')), \
                    patch.object(install,'package_manifest',return_value={'files':files}), \
                    patch.object(install,'launcher_sources',return_value=sources):
                elevation.verify_worker(root)
                (root/'Runtime/python/python314._pth').write_bytes(b'untrusted search path')
                with self.assertRaises(InternetError):elevation.verify_worker(root)
                (root/'Runtime/python/python314._pth').write_bytes(b'approved path')
                (root/'Launcher/extra.pyc').write_bytes(b'untrusted bytecode')
                with self.assertRaises(InternetError):elevation.verify_worker(root)


if __name__ == '__main__': unittest.main()
