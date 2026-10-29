import ctypes
import json
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
            self.assertEqual(json.loads((Path(folder)/'state.json').read_text()),{'phase':'ready'})
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
            state = Path(folder) / 'state.json'
            state.write_text(json.dumps(value))
            with patch.object(elevation, 'enumerate_interfaces', return_value=[interface]):
                self.assertEqual(overlay.snapshot(), (interface, {ROOM.coordinator: 'p2p'}))
                state.write_text(json.dumps(dict(value, nonce='x' * 32)))
                with self.assertRaises(InternetError): overlay.snapshot()
                state.write_text(json.dumps(dict(value, peers={'192.168.1.2': 'p2p'})))
                with self.assertRaises(InternetError): overlay.snapshot()

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
        with patch.object(elevation.subprocess, 'run', return_value=Mock(returncode=0)) as command:
            elevation.firewall(root, 'a' * 32, interface)
        args = command.call_args.args[0]
        self.assertEqual(args[args.index('-InterfaceName') + 1], interface.name)
        self.assertEqual(args[args.index('-Address') + 1], interface.address)
        self.assertNotIn(ROOM.secret, repr(command.call_args))
        self.assertNotIn('shell', command.call_args.kwargs)

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
