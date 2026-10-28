import json
from contextlib import ExitStack
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/installer'), str(ROOT / 'tools/lan')]
import graphics_profile as graphics
import compatibility as content


def dll_fixture(*, machine=0x14c, magic=0x10b, flags=0x2102):
    value = bytearray(512)
    value[:2] = b'MZ'
    struct.pack_into('<I', value, 0x3c, 0x80)
    value[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<H', value, 0x84, machine)
    struct.pack_into('<HH', value, 0x94, 224, flags)
    struct.pack_into('<H', value, 0x98, magic)
    return bytes(value)


class GraphicsProfileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT'))
        self.addCleanup(self.temp.cleanup)
        self.parent = Path(self.temp.name)
        self.root = self.parent / 'owned-copy'
        self.root.mkdir()
        self.dll = dll_fixture()
        self.source = self.parent / 'explicit-dxvk.dll'
        self.source.write_bytes(self.dll)
        values = {graphics.EXE: b'fixture executable', graphics.LOADER: self.dll + b'local loader',
                  graphics.MENU: b'known interface', 'Base/BBA/Large.bba': b'game archive',
                  'Server/service': b'service', 'Launcher/start': b'launcher'}
        for name, value in values.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(value)
        manifest = {'format': 's7-local-experimental-v1', 'runtime_verified': False,
                    'files': {name: graphics._hash(values[name]) for name in
                              (graphics.EXE, graphics.LOADER, graphics.MENU)}}
        (self.root / graphics.BUNDLE).write_bytes(graphics.encoded(manifest))
        baseline = content.create(self.root)
        baseline['lan_join_verified'] = True  # A changed graphics build cannot inherit this claim.
        (self.root / graphics.COMPAT).write_bytes(graphics.encoded(baseline))
        self.before = self.snapshot()
        for name, value in [('EXE_SHA256', graphics._hash(values[graphics.EXE])),
                            ('DXVK_SHA256', graphics._hash(self.dll))]:
            mock = patch.object(graphics, name, value)
            mock.start(); self.addCleanup(mock.stop)
        mock = patch.object(graphics, 'ensure_closed')
        self.closed = mock.start(); self.addCleanup(mock.stop)

    def snapshot(self):
        return {p.relative_to(self.root).as_posix(): p.read_bytes()
                for p in self.root.rglob('*') if p.is_file() and 'LocalData' not in p.parts}

    def install(self):
        return graphics.install(self.root, self.source)

    def assert_original(self):
        self.assertEqual(self.snapshot(), self.before)
        content.verify(self.root)

    def state_dir(self):
        return self.root / graphics.STATE

    def test_install_restore_idempotence_and_actual_compatibility(self):
        original_id = content.verify(self.root)['content_id']
        self.assertEqual(self.install(), 'enabled')
        active = content.verify(self.root)
        self.assertNotEqual(active['content_id'], original_id)
        self.assertEqual(active['graphics_profile'], graphics.PROFILE)
        self.assertFalse(active['lan_join_verified'])
        self.assertFalse(active['graphics_runtime_verified'])
        self.assertEqual(active['files'][graphics.DLL.lower()], graphics.DXVK_SHA256)
        self.assertEqual(active['files'][graphics.CONF.lower()], graphics._hash(graphics.CONFIG))
        self.assertEqual((self.root / graphics.BUNDLE).read_bytes(), self.before[graphics.BUNDLE])
        self.assertEqual((self.state_dir() / 'compatibility.before.json').read_bytes(), self.before[graphics.COMPAT])
        self.assertEqual(self.install(), 'already-enabled')
        self.assertEqual(graphics.restore(self.root), 'disabled')
        self.assert_original()
        self.assertEqual(graphics.restore(self.root), 'already-disabled')
        self.assertEqual(self.install(), 'enabled')
        self.assertEqual(graphics.restore(self.root), 'disabled')
        self.assert_original()

    def test_original_installation_without_manifests_is_rejected_without_writes(self):
        for name in (graphics.BUNDLE, graphics.COMPAT):
            saved = (self.root / name).read_bytes()
            (self.root / name).unlink()
            before = self.snapshot()
            with self.assertRaises(ValueError): self.install()
            with self.assertRaises(ValueError): graphics.restore(self.root)
            self.assertEqual(self.snapshot(), before)
            self.assertFalse((self.root / 'LocalData').exists())
            (self.root / name).write_bytes(saved)

    def test_foreign_or_unrecorded_known_dll_is_never_adopted(self):
        for value in (b'unknown Direct3D proxy', self.dll):
            (self.root / graphics.DLL).write_bytes(value)
            # Even an inventory which records this preexisting DLL cannot confer ownership.
            (self.root / graphics.COMPAT).write_bytes(graphics.encoded(content.create(self.root)))
            before = self.snapshot()
            with self.assertRaises(ValueError): self.install()
            with self.assertRaises(ValueError): graphics.restore(self.root)
            self.assertEqual(self.snapshot(), before)
            self.assertFalse(self.state_dir().exists())

    def test_foreign_or_equal_unowned_configuration_is_preserved(self):
        for value in (b'foreign settings', graphics.CONFIG):
            (self.root / graphics.CONF).write_bytes(value)
            (self.root / graphics.COMPAT).write_bytes(graphics.encoded(content.create(self.root)))
            with self.assertRaises(ValueError): self.install()
            self.assertEqual((self.root / graphics.CONF).read_bytes(), value)
            self.assertFalse((self.root / graphics.DLL).exists())

    def test_wrong_source_hash_and_pe_shapes_rejected(self):
        cases = [b'not DXVK', dll_fixture(machine=0x8664), dll_fixture(magic=0x20b),
                 dll_fixture(flags=0x102), b'MZ' + bytes(16)]
        for value in cases:
            with self.subTest(size=len(value), prefix=value[:2]):
                self.source.write_bytes(value)
                # Pin only fixture bytes, retaining the independent PE32 check.
                with patch.object(graphics, 'DXVK_SHA256', graphics._hash(value)):
                    with self.assertRaises(ValueError): self.install()
                self.assert_original()
                self.assertFalse(self.state_dir().exists())
        self.source.write_bytes(self.dll + b'altered')
        with self.assertRaises(ValueError): self.install()

    def test_changed_exe_is_rejected_even_when_manifests_agree(self):
        (self.root / graphics.EXE).write_bytes(b'unknown game version')
        bundle = json.loads((self.root / graphics.BUNDLE).read_bytes())
        bundle['files'][graphics.EXE] = content.digest(self.root / graphics.EXE)
        (self.root / graphics.BUNDLE).write_bytes(graphics.encoded(bundle))
        (self.root / graphics.COMPAT).write_bytes(graphics.encoded(content.create(self.root)))
        with self.assertRaises(ValueError): self.install()
        self.assertFalse(self.state_dir().exists())

    def test_loader_requires_hash_agreement_and_pe32(self):
        (self.root / graphics.LOADER).write_bytes(b'unknown loader')
        (self.root / graphics.COMPAT).write_bytes(graphics.encoded(content.create(self.root)))
        with self.assertRaises(ValueError): self.install()
        bundle = json.loads((self.root / graphics.BUNDLE).read_bytes())
        bundle['files'][graphics.LOADER] = content.digest(self.root / graphics.LOADER)
        (self.root / graphics.BUNDLE).write_bytes(graphics.encoded(bundle))
        with self.assertRaises(ValueError): self.install()

    def test_unrelated_file_tampering_is_not_blessed(self):
        (self.root / 'Server/service').write_bytes(b'changed')
        before = self.snapshot()
        with self.assertRaises(ValueError): self.install()
        self.assertEqual(self.snapshot(), before)

    def test_running_game_and_held_launcher_lock_reject(self):
        self.closed.side_effect = ValueError('game running')
        with self.assertRaises(ValueError): self.install()
        self.assertFalse((self.root / 'LocalData').exists())
        self.closed.side_effect = None
        with graphics.session_lock(self.root):
            with self.assertRaises(ValueError): self.install()
        self.assert_original()

    def test_game_start_during_preparation_does_not_mutate_game(self):
        self.closed.side_effect = [None, ValueError('game started')]
        with self.assertRaises(ValueError): self.install()
        self.assert_original()

    def test_incomplete_installation_rejected(self):
        (self.root / 'INSTALLATION-INCOMPLETE.txt').write_text('incomplete')
        with self.assertRaises(ValueError): self.install()
        self.assertFalse((self.root / 'LocalData').exists())

    def fail_once(self, name, exception=OSError):
        failed = False
        def wrapper(original):
            def write(path, value, staging):
                nonlocal failed
                if path.name == name and not failed:
                    failed = True
                    raise exception('fixture failure')
                original(path, value, staging)
            return write
        stack = ExitStack()
        for function in ('atomic_write', 'atomic_create'):
            stack.enter_context(patch.object(graphics, function, side_effect=wrapper(getattr(graphics, function))))
        return stack

    def test_install_failures_roll_back_at_each_write_boundary(self):
        for name in ('d3d9.dll', 'dxvk.conf', graphics.COMPAT, 'status.json'):
            with self.subTest(name=name):
                with self.fail_once(name):
                    with self.assertRaises(OSError): self.install()
                self.assert_original()
                self.assertFalse((self.state_dir() / 'transaction.json').exists())

    def test_ctrl_c_rolls_back_and_retry_works(self):
        with self.fail_once('dxvk.conf', KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt): self.install()
        self.assert_original()
        self.assertEqual(self.install(), 'enabled')

    def test_restore_failure_restores_active_profile_instead_of_leaving_half_removed(self):
        self.install()
        before = self.snapshot()
        with self.fail_once(graphics.COMPAT):
            with self.assertRaises(OSError): graphics.restore(self.root)
        self.assertEqual(self.snapshot(), before)
        content.verify(self.root)
        self.assertEqual(graphics.restore(self.root), 'disabled')
        self.assert_original()

    def interrupted_install(self):
        self.install()
        graphics.restore(self.root)
        (self.state_dir() / 'transaction.json').write_bytes(graphics.encoded(
            {'format': graphics.PROFILE, 'from': False, 'to': True}))
        (self.root / graphics.DLL).write_bytes(self.dll)

    def test_persisted_interruption_recovers_to_previous_committed_state(self):
        self.interrupted_install()
        self.assertEqual(graphics.restore(self.root), 'already-disabled')
        self.assert_original()
        self.assertFalse((self.state_dir() / 'transaction.json').exists())

    def test_committed_journal_is_cleared_without_undoing_complete_operation(self):
        self.install()
        (self.state_dir() / 'transaction.json').write_bytes(graphics.encoded(
            {'format': graphics.PROFILE, 'from': False, 'to': True}))
        self.assertEqual(self.install(), 'already-enabled')
        content.verify(self.root)

    def test_unknown_file_after_interruption_prevents_every_restore_write(self):
        self.interrupted_install()
        (self.root / graphics.CONF).write_bytes(b'new user configuration')
        before = self.snapshot()
        with self.assertRaises(ValueError): graphics.restore(self.root)
        self.assertEqual(self.snapshot(), before)

    def test_foreign_change_during_atomic_failure_survives(self):
        original = graphics.atomic_create
        def write(path, value, staging):
            if path.name == 'dxvk.conf':
                path.write_bytes(b'concurrent foreign file')
                raise OSError('disk failure')
            return original(path, value, staging)
        with patch.object(graphics, 'atomic_create', side_effect=write):
            with self.assertRaises(ValueError): self.install()
        self.assertEqual((self.root / graphics.CONF).read_bytes(), b'concurrent foreign file')
        self.assertEqual((self.root / graphics.DLL).read_bytes(), self.dll)
        self.assertTrue((self.state_dir() / 'transaction.json').is_file())

    def test_new_file_publication_does_not_overwrite_concurrent_file(self):
        original = os.rename
        def rename(source, destination, *args, **kwargs):
            if Path(destination).name == 'd3d9.dll':
                Path(destination).write_bytes(b'concurrently installed foreign DLL')
            return original(source, destination, *args, **kwargs)
        with patch.object(os, 'rename', side_effect=rename):
            with self.assertRaises(ValueError): self.install()
        self.assertEqual((self.root / graphics.DLL).read_bytes(), b'concurrently installed foreign DLL')
        self.assertFalse((self.root / graphics.CONF).exists())

    def test_tampered_backup_payload_or_start_manifest_prevents_restore(self):
        self.install()
        for path in (self.state_dir() / 'compatibility.before.json',
                     self.state_dir() / 'd3d9.payload', self.root / graphics.BUNDLE):
            old = path.read_bytes()
            path.write_bytes(old + b'bad')
            before = self.snapshot()
            with self.assertRaises((ValueError, json.JSONDecodeError)): graphics.restore(self.root)
            self.assertEqual(self.snapshot(), before)
            path.write_bytes(old)

    def test_changed_active_profile_not_removed_or_overwritten(self):
        self.install()
        for name in (graphics.DLL, graphics.CONF):
            old = (self.root / name).read_bytes()
            (self.root / name).write_bytes(b'foreign')
            before = self.snapshot()
            with self.assertRaises(ValueError): graphics.restore(self.root)
            with self.assertRaises(ValueError): self.install()
            self.assertEqual(self.snapshot(), before)
            (self.root / name).write_bytes(old)

    def test_traversal_in_journal_cannot_change_target_set(self):
        self.install()
        before = self.snapshot()
        (self.state_dir() / 'transaction.json').write_bytes(graphics.encoded(
            {'format': graphics.PROFILE, 'from': True, 'to': False, 'files': {'../../outside': None}}))
        with self.assertRaises(ValueError): graphics.restore(self.root)
        self.assertEqual(self.snapshot(), before)

    def test_duplicate_json_and_oversized_sources_are_rejected(self):
        (self.root / graphics.BUNDLE).write_bytes(b'{"files":{},"files":{}}')
        with self.assertRaises(ValueError): self.install()
        (self.root / graphics.BUNDLE).write_bytes(self.before[graphics.BUNDLE])
        with patch.object(graphics, 'MAX_DLL', 64):
            with self.assertRaises(ValueError): self.install()
        self.assert_original()

    def test_environment_is_child_only_and_outputs_stay_outside_inventory(self):
        self.install()
        before_environment = dict(os.environ)
        inherited = {'PATH': 'fixture path', 'dxvk_config_file': 'foreign',
                     'DXVK_FILTER_DEVICE_NAME': 'inherited GPU', 'DXVK_CONFIG': 'foreign',
                     'DXVK_STATE_CACHE_PATH': 'foreign'}
        original_inherited = dict(inherited)
        inventory = content.verify(self.root)
        # Startup must not rescan the full game on a slow disk.
        with patch.object(content, 'inventory', side_effect=AssertionError('full scan at startup')):
            environment = graphics.launch_environment(self.root, inherited)
            selected = graphics.launch_environment(self.root, inherited, device_name='Fixture GPU')
        self.assertEqual(dict(os.environ), before_environment)
        self.assertEqual(inherited, original_inherited)
        self.assertEqual(environment['PATH'], 'fixture path')
        self.assertNotIn('DXVK_FILTER_DEVICE_NAME', environment)
        self.assertNotIn('DXVK_CONFIG', environment)
        self.assertNotIn('dxvk_config_file', environment)
        self.assertEqual(selected['DXVK_FILTER_DEVICE_NAME'], 'Fixture GPU')
        self.assertEqual(Path(environment['DXVK_CONFIG_FILE']), self.root / graphics.CONF)
        for key in ('DXVK_LOG_PATH', 'DXVK_SHADER_CACHE_PATH'):
            path = Path(environment[key])
            self.assertTrue(path.is_relative_to(self.root / 'LocalData'))
            (path / 'fixture-output').write_bytes(b'generated output')
        self.assertEqual(content.verify(self.root), inventory)
        graphics.restore(self.root)
        for key in ('DXVK_LOG_PATH', 'DXVK_SHADER_CACHE_PATH'):
            self.assertEqual((Path(environment[key]) / 'fixture-output').read_bytes(), b'generated output')

    def test_environment_rejects_inactive_partial_or_modified_profile_and_bad_device(self):
        self.install()
        for name in ('', ' ', 'x\ny', 'nul\0', 'x' * 257, 12, '\ud800'):
            with self.assertRaises(ValueError):
                graphics.launch_environment(self.root, {}, device_name=name)
        (self.state_dir() / 'transaction.json').write_bytes(graphics.encoded(
            {'format': graphics.PROFILE, 'from': False, 'to': True}))
        with self.assertRaises(ValueError): graphics.launch_environment(self.root, {})
        (self.state_dir() / 'transaction.json').unlink()
        (self.root / graphics.CONF).write_bytes(b'changed')
        with self.assertRaises(ValueError): graphics.launch_environment(self.root, {})
        (self.root / graphics.CONF).write_bytes(graphics.CONFIG)
        graphics.restore(self.root)
        with self.assertRaises(ValueError): graphics.launch_environment(self.root, {})

    def test_reparse_points_rejected_even_without_path_is_junction(self):
        original = Path.lstat
        folder = self.root / 'Base'
        class Reparse:
            st_file_attributes = stat_reparse = 0x400
        def lstat(path, *args, **kwargs):
            if path == folder:
                return Reparse()
            return original(path, *args, **kwargs)
        with patch.object(Path, 'lstat', lstat):
            with self.assertRaises(ValueError): self.install()
        self.assert_original()

    def test_actual_symbolic_link_target_is_preserved(self):
        target = self.parent / 'outside.dll'
        target.write_bytes(b'outside untouched')
        link = self.root / graphics.DLL
        try:
            link.symlink_to(target)
        except OSError as exc:
            self.skipTest('Host disallows creating test symlinks: ' + str(exc.winerror))
        with self.assertRaises(ValueError): self.install()
        self.assertEqual(target.read_bytes(), b'outside untouched')
        self.assertTrue(link.is_symlink())
        link.unlink()


if __name__ == '__main__':
    unittest.main()
