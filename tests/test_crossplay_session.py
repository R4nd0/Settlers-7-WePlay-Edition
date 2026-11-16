"""Launcher admission boundaries with authored files; no game/world proof."""
from dataclasses import replace
import hashlib
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/installer'))
import crossplay_session as session
import crossplay_content as content
import platform_standalone as platform


def binding(edition=1, peer=3, role='host'):
    return session.Binding(edition, peer, role, 12345, b's' * 16, b'k' * 32,
        (42).to_bytes(4, 'little') + bytes(12) if edition == 1 else b'i' * 16,
        b'c' * 32, 'WePlay Player')


class CrossplaySessionTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(dir=ROOT / 'work')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.exe = self.root / 'Data/Base/_Dbg/Bin/Release/Settlers7R.exe'
        self.exe.parent.mkdir(parents=True)
        self.exe.write_bytes(b'authored executable')
        self.profile = self.root / 'LocalData/ProfileRoot/Profiles'
        self.profile.mkdir(parents=True)
        self.digest = hashlib.sha256(self.exe.read_bytes()).hexdigest()
        guard = patch.dict(session.EXECUTABLE_EDITIONS, {self.digest: 1})
        guard.start(); self.addCleanup(guard.stop)

    def test_only_gold_host_and_one_history_guest_are_admitted(self):
        for local, peer, role in ((1, 2, 'host'), (1, 3, 'host'), (2, 1, 'guest'), (3, 1, 'guest')):
            binding(local, peer, role)
        for local in (0, 1, 2, 3, 4, True):
            for peer in (0, 1, 2, 3, 4, True):
                for role in ('host', 'guest', 'Host', '', None):
                    if type(local) is int and type(peer) is int and (local, peer, role) in (
                            (1, 2, 'host'), (1, 3, 'host'), (2, 1, 'guest'), (3, 1, 'guest')):
                        continue
                    with self.subTest(local=local, peer=peer, role=role), self.assertRaises(ValueError):
                        binding(local, peer, role)

    def test_rejects_malformed_authentication_names_and_ports(self):
        good = binding()
        changes = [{'port': value} for value in (0, 65536, True, '12345', 1.0)]
        changes += [{'name': value} for value in ('', 'x' * 65, 'line\nname', 'Nul\0', 'Ü', None)]
        for field, size in (('session', 16), ('key', 32), ('identity', 16), ('content', 32)):
            changes += [{field: value} for value in (bytes(size), b'x' * (size - 1), b'x' * (size + 1),
                                                       'a' * (size * 2), bytearray(b'x' * size), None)]
        changes += [{'identity': value} for value in (b'i' * 16, (0x80000000).to_bytes(4, 'little') + bytes(12))]
        for change in changes:
            with self.subTest(field=next(iter(change))), self.assertRaises(ValueError):
                replace(good, **change)

    def test_identity_generation_keeps_independent_native_namespaces(self):
        for edition in (1, 2, 3):
            left, right = session.native_identity(edition), session.native_identity(edition)
            self.assertNotEqual(left, right)
            self.assertEqual(len(left), 16); self.assertTrue(any(left))
            if edition == 1:
                self.assertTrue(0 < int.from_bytes(left[:4], 'little') <= 0x7fffffff)
                self.assertEqual(left[4:], bytes(12))
        for edition in (True, 0, 4, '1'):
            with self.assertRaises(ValueError): session.native_identity(edition)

    def test_handoff_uses_owned_profile_and_explicit_material_without_mutating_parent(self):
        before = {'PATH': 'authored', 'weplay_crossplay_v1': 'stale',
                  'WEPLAY_CROSSPLAY_V1_KEY': 'stale secret', 'WEPLAY_SETTING': 'keep'}
        result = binding().environment(self.root, self.exe, before)
        self.assertEqual(result['WEPLAY_CROSSPLAY_V1_PROFILE'], str(self.profile))
        self.assertEqual(result['WEPLAY_CROSSPLAY_V1'], '1')
        self.assertEqual(result['WEPLAY_CROSSPLAY_V1_KEY'], (b'k' * 32).hex())
        self.assertEqual(result['WEPLAY_CROSSPLAY_V1_IDENTITY'], ((42).to_bytes(4, 'little') + bytes(12)).hex())
        self.assertEqual(result['WEPLAY_CROSSPLAY_V1_CONTENT'], (b'c' * 32).hex())
        self.assertEqual(result['WEPLAY_CROSSPLAY_V1_PORT'], '12345')
        self.assertNotIn('weplay_crossplay_v1', result)
        self.assertEqual(result['WEPLAY_SETTING'], 'keep')
        self.assertEqual(before['WEPLAY_CROSSPLAY_V1_KEY'], 'stale secret')
        self.assertNotIn((b'k' * 32).hex(), repr(binding()))

    def test_wrong_executable_or_missing_or_linked_portable_profile_fails(self):
        with self.assertRaisesRegex(ValueError, 'edition_mismatch'):
            binding(3, 1, 'guest').environment(self.root, self.exe, {})
        self.exe.write_bytes(b'changed executable')
        with self.assertRaisesRegex(ValueError, 'edition_mismatch'):
            binding().environment(self.root, self.exe, {})
        self.exe.write_bytes(b'authored executable')
        self.profile.rmdir()
        with self.assertRaisesRegex(ValueError, 'profile_invalid'):
            binding().environment(self.root, self.exe, {})
        target = self.root / 'ForeignProfile'; target.mkdir()
        self.profile.symlink_to(target, target_is_directory=True)
        with self.assertRaises(ValueError): binding().environment(self.root, self.exe, {})

    def test_ordinary_launch_strips_every_inherited_mixed_setting(self):
        inherited = {'PATH': 'authored', 'WEPLAY_SETTING': 'keep', 'WEPLAY_CROSSPLAY_V1': '1',
                     'WePlay_CrossPlay_V1_KEY': 'secret', 'WEPLAY_CROSSPLAY_FUTURE': 'stale'}
        for edition in ('gold', 'history'):
            result = platform.launch_environment({'edition': edition}, self.exe, inherited)
            self.assertFalse(any(key.upper().startswith(session.PREFIX) for key in result))
            self.assertEqual(result['PATH'], 'authored')
        self.assertEqual(inherited['WEPLAY_CROSSPLAY_V1'], '1')

    def test_platform_passes_explicit_handoff_after_graphics_and_keeps_native_loopback_policy(self):
        adapter = self.root / 'Launcher/weplay_gold_startup.dll'; adapter.parent.mkdir(); adapter.write_bytes(b'adapter')
        digest = hashlib.sha256(b'adapter').hexdigest()
        descriptor = {'edition': 'gold', 'files': {adapter.relative_to(self.root).as_posix(): digest}}
        editions = []
        for edition in (1, 2, 3):
            exe = self.digest if edition == 1 else next(pin for pin, local in session.EXECUTABLE_EDITIONS.items() if local == edition)
            files = [{'id': name, 'sha256': pin} for name, pin in zip(content.REQUIRED[edition], (exe, digest))]
            editions.append({'edition': edition, 'files': sorted(files, key=lambda row: row['id'])})
        catalog = content.Catalog(json.dumps({'format': content.FORMAT, 'native_protocol': 1, 'neo_features': 19,
            'editions': editions}, ensure_ascii=True, sort_keys=True, separators=(',', ':')).encode())
        game = SimpleNamespace()
        with patch.object(platform, 'load', return_value=(descriptor, self.exe, adapter)), \
                patch.object(platform.subprocess, 'run', return_value=SimpleNamespace(returncode=0)), \
                patch.dict(session.ADAPTER_PINS, {1: (digest,)}), \
                patch.object(content, 'APPROVED_DIGESTS', frozenset((catalog.digest.hex(),))), \
                patch('isolated_process.launch', return_value=game) as launch:
            supplied = replace(binding(), content=catalog.digest)
            result = platform.launch(self.root, crossplay=supplied, crossplay_catalog=catalog,
                                     environment={'WEPLAY_CROSSPLAY_V1_KEY': 'stale'})
            with self.assertRaisesRegex(ValueError, 'content_changed'):
                platform.launch(self.root, crossplay=binding(), crossplay_catalog=catalog)
            with self.assertRaisesRegex(ValueError, 'catalog_invalid'):
                platform.launch(self.root, crossplay=supplied)
            self.assertEqual(launch.call_count, 1)
        self.assertIs(result, game)
        arguments = launch.call_args.kwargs
        self.assertIsNone(game.network_policy)
        self.assertEqual(arguments['environment']['WEPLAY_CROSSPLAY_V1_PROFILE'], str(self.profile))
        self.assertEqual(arguments['environment']['DXVK_CONFIG_FILE'], str(self.exe.with_name('dxvk.conf')))
        self.assertEqual(arguments['dll_sha256'], digest)
        self.assertEqual(arguments['policy_sha256'], platform.POLICY)

    def test_invalid_handoff_cannot_reach_policy_or_process_creation(self):
        adapter = self.root / 'Launcher/adapter.dll'
        descriptor = {'edition': 'gold', 'files': {}}
        with patch.object(platform, 'load', return_value=(descriptor, self.exe, adapter)), \
                patch.object(platform.subprocess, 'run') as run:
            for supplied, lan in (({}, None), (binding(), {'peer': '192.168.1.2'})):
                with self.assertRaisesRegex(ValueError, 'configuration_invalid'):
                    platform.launch(self.root, crossplay=supplied, lan=lan)
            run.assert_not_called()

    def test_existing_release_adapters_have_no_mixed_launch_permission(self):
        adapter = self.root / 'Launcher/adapter.dll'
        descriptor = {'edition': 'gold', 'files': {'Launcher/adapter.dll': platform.GOLD}}
        with patch.object(platform, 'load', return_value=(descriptor, self.exe, adapter)), \
                patch.object(platform.subprocess, 'run') as run:
            with self.assertRaisesRegex(ValueError, 'adapter_unavailable'):
                platform.launch(self.root, crossplay=binding())
            run.assert_not_called()


if __name__ == '__main__': unittest.main()
