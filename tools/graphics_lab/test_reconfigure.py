"""Pure guard/dispatch fixtures. No live process is opened or reconfigured."""
from contextlib import nullcontext
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import MagicMock, patch

from tools.graphics_lab import reconfigure as r


def config():
    row = struct.pack('<5I', 285, 286, 3, 1, 0) + struct.pack('<3I', 1, 16, 7) + bytes(7 * 12)
    return struct.pack('<4IQ', 952, 1, 1, 1, 0) + row + bytes(7 * 116)


class ReconfigureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.case = self.root / 'work/case'
        self.bundle = self.root / 'work/game'
        self.case.mkdir(parents=True)
        self.bin = self.bundle / 'Base/Bin'
        self.bin.mkdir(parents=True)
        self.exe = self.bin / 'Settlers7R.exe'
        self.exe.write_bytes(b'synthetic original EXE fixture')
        self.pin = r.load_hook.digest(self.exe)
        self.critical = {'Base/Bin/Settlers7R.exe': self.pin}
        for name in ('uplay_r1_loader.dll', 'd3d9.dll', 'Vision90.dll', 'vBase90.dll'):
            path = self.bin / name; path.write_bytes(('synthetic ' + name).encode())
            self.critical['Base/Bin/' + name] = r.load_hook.digest(path)
        p = patch.object(r.game.graphics_profile, 'DXVK_SHA256', self.critical['Base/Bin/d3d9.dll'])
        p.start(); self.addCleanup(p.stop)
        self.dll = self.root / 'work/build/WePlaySupersamplingLab.dll'
        self.dll.parent.mkdir()
        self.dll.write_bytes(b'synthetic source-bound hook fixture')
        self.build = {'files': {r.load_hook.DLL_NAME: r.load_hook.digest(self.dll)}}
        self.owned = {'pid': 1234, 'parent': 1000, 'path': str(self.exe), 'sha256': self.pin,
                      'native_initialized': True, 'kernel_isolation': 7}
        self.emit('owned.private.json', self.owned)
        self.emit('hook-initialized.private.json', {'initialized': True, 'succeeded': True,
                  'export': 'WePlaySupersamplingInitialize', 'original_entry_guarded': True,
                  'worker_code_rx': True, 'dll_sha256': self.build['files'][r.load_hook.DLL_NAME]})
        self.emit('hook-build.private.json', self.build)
        sources = {}
        for name in r.REQUIRED_SOURCES:
            for path in (self.root / name, self.case / 'source' / name):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(name.encode())
            sources[name] = hashlib.sha256(name.encode()).hexdigest()
        self.emit('checkpoint.private.json', {'files': self.critical, 'sources': sources})
        self.emit('recovery-journal.private.json', {'state': 'prepared', 'root': str(self.bundle),
                  'critical': self.critical})
        for target, value in ((r, self.root), (r.game, self.root)):
            p = patch.object(target, 'ROOT', value); p.start(); self.addCleanup(p.stop)
        p = patch.object(r, 'KNOWN_EXES', {self.pin}); p.start(); self.addCleanup(p.stop)
        p = patch.dict(r.isolated_process.PROFILES, {self.pin: (1,)}, clear=True); p.start(); self.addCleanup(p.stop)
        p = patch.object(r.game.platform, 'load', return_value=({}, self.exe, self.bin / 'uplay_r1_loader.dll'))
        p.start(); self.addCleanup(p.stop)
        p = patch.object(r.load_hook, 'verify_build', return_value=(self.dll, self.build))
        p.start(); self.addCleanup(p.stop)

    def emit(self, relative, value):
        path = self.case / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value), encoding='utf-8')

    def files(self):
        return r._case_files(self.case, self.dll)

    def live(self, files):
        ticks = (files['record_mtime_ns'] - 1_000_000_000 + r.FILETIME_UNIX_OFFSET_NS) // 100
        renderer = self.bin / 'd3d9.dll'
        return {'path': self.exe, 'parent_path': Path(sys.executable), 'created': ticks,
                'parent_created': ticks - 50_000_000,
                'modules': [{'name': self.dll.name, 'path': str(self.dll)},
                            {'name': 'd3d9.dll', 'path': str(renderer)}]}

    def test_exact_unfinished_original_case_and_fixed_observer_config_are_accepted(self):
        files = self.files()
        self.assertEqual(files['root'], self.bundle.resolve())
        self.assertEqual(r.validate_config(config(), hashlib.sha256(config()).hexdigest())['targets'], 1)
        self.assertEqual(r.validate_live_state(self.live(files), files), self.live(files)['created'])

    def test_wrong_size_hash_stage_duplicates_and_hidden_targets_are_refused(self):
        cases = [config()[:-1]]
        for offset, value in ((4, 2), (8, 9), (24, 0), (32, 9), (44, 9), (48, 256), (52, 0), (140, 1)):
            raw = bytearray(config()); struct.pack_into('<I', raw, offset, value); cases.append(bytes(raw))
        for raw in cases:
            with self.subTest(raw=raw[:32]), self.assertRaises(ValueError):
                r.validate_config(raw, hashlib.sha256(raw).hexdigest())
        with self.assertRaises(ValueError):
            r.validate_config(config(), '0' * 64)

    def test_completed_wrong_source_changed_receipt_and_uninitialized_cases_refuse(self):
        (self.case / 'result.private.json').write_text('{}')
        with self.assertRaises(ValueError):self.files()
        (self.case / 'result.private.json').unlink()
        source = self.case / 'source/tools/graphics_lab/game.py'
        original = source.read_bytes(); source.write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'frozen'):self.files()
        source.write_bytes(original)
        self.owned['native_initialized'] = False; self.emit('owned.private.json', self.owned)
        with self.assertRaisesRegex(ValueError, 'original initialized'):self.files()

    def test_reused_pid_new_parent_external_path_and_wrong_loaded_dll_are_refused(self):
        files = self.files(); live = self.live(files)
        for mutation in ('future', 'ancient', 'new-parent', 'external-exe', 'other-python', 'wrong-dll', 'duplicate-dll'):
            state = deepcopy(live)
            if mutation == 'future':state['created'] += 30_000_000
            elif mutation == 'ancient':state['created'] -= 2_000_000_000; state['parent_created'] -= 2_000_000_000
            elif mutation == 'new-parent':state['parent_created'] = state['created'] + 1
            elif mutation == 'external-exe':state['path'] = self.root / 'unrelated.exe'
            elif mutation == 'other-python':state['parent_path'] = self.root / 'other-python.exe'
            elif mutation == 'wrong-dll':state['modules'][0]['path'] = str(self.root / 'other.dll')
            elif mutation == 'duplicate-dll':state['modules'].append(state['modules'][0])
            with self.subTest(mutation=mutation), self.assertRaises(RuntimeError):r.validate_live_state(state, files)

    def test_borrowed_owner_rechecks_creation_and_closes_only_its_handle(self):
        files = self.files(); state = self.live(files)
        api = SimpleNamespace(k=SimpleNamespace(OpenProcess=MagicMock(return_value=17), CloseHandle=MagicMock()),
                              check=lambda value: value)
        with patch.object(r.isolated_process, 'Windows', return_value=api), patch.object(r, '_bind'), \
             patch.object(r, '_live_state', return_value=state):
            owner = r._CaseOwner(files)
            owner.owned()
            state['created'] -= 1
            with self.assertRaisesRegex(RuntimeError, 'creation identity'):owner.owned()
            owner.close()
        api.k.OpenProcess.assert_called_once_with(0x43a, False, 1234)
        api.k.CloseHandle.assert_called_once_with(17)
        self.assertFalse(hasattr(owner, 'terminate'))
        self.assertFalse(hasattr(owner, 'wait'))
        self.assertFalse(hasattr(owner, 'job'))

    def test_configure_invokes_only_fixed_operation_and_preserves_prior_requests(self):
        raw = config(); path = self.root / 'work/camera-config.bin'; path.write_bytes(raw)
        owner = MagicMock(); owner.pid = 1234
        with patch.object(r, '_CaseOwner', return_value=owner), \
             patch.object(r.load_hook, 'file_lease', return_value=nullcontext()), \
             patch.object(r.load_hook, 'invoke', return_value={'succeeded': True}) as invoke:
            result = r.configure(self.case, self.dll, path, hashlib.sha256(raw).hexdigest(), label='camera-before')
            invoke.assert_called_once_with(owner, self.dll, 'configure', raw)
            self.assertTrue(result['read_only_observer'])
            owner.close.assert_called_once()
            with self.assertRaisesRegex(ValueError, 'new request label'):
                r.configure(self.case, self.dll, path, hashlib.sha256(raw).hexdigest(), label='camera-before')

    def test_failed_worker_still_closes_borrowed_handle_without_stopping_the_game(self):
        raw = config(); path = self.root / 'work/camera-config.bin'; path.write_bytes(raw)
        owner = MagicMock(); owner.pid = 1234
        with patch.object(r, '_CaseOwner', return_value=owner), \
             patch.object(r.load_hook, 'file_lease', return_value=nullcontext()), \
             patch.object(r.load_hook, 'invoke', side_effect=RuntimeError('worker refused')):
            with self.assertRaises(RuntimeError):
                r.configure(self.case, self.dll, path, hashlib.sha256(raw).hexdigest(), label='camera-failed')
        owner.close.assert_called_once()
        owner.terminate.assert_not_called()
        owner.wait.assert_not_called()


if __name__ == '__main__':
    unittest.main()
