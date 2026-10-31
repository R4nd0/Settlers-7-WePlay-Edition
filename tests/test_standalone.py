"""Relocation, corruption and ownership boundaries using invented game files."""
import json
import io
from contextlib import redirect_stdout
from pathlib import Path
import shutil
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/installer'), str(Path(__file__).parent)]
import compatibility as content
import desktop_backend as backend
import graphics_choice as choice
import graphics_profile as graphics
import standalone
import test_installer as fixture


class StandaloneTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixture.InstallerTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.root = self.fixture.root / 'export'
        self.app = self.root / 'App'
        self.app.mkdir(parents=True)
        shutil.copytree(self.fixture.package, self.app / 'Package')
        self.instance = 'a' * 32
        container = self.app / 'Instances'
        container.mkdir()
        self.game = container / self.instance
        backend.install.prepare(self.app / 'Package', self.fixture.source, self.game,
                                emit=lambda _: None, publish_starters=False)
        self.data = {'format': standalone.FORMAT, 'instance': self.instance,
                     'package_id': backend.Desktop(self.app).package_id()}
        self.save_descriptor()
        for target, options in [
                ((graphics, 'ensure_closed'), {}),
                ((graphics, '_bundle'), {}),  # Invented bytes, not a real PE image.
                ((backend.Desktop, 'probe'), {'return_value': {'devices': [], 'failure': 'unsupported'}}),
                ((backend.Desktop, 'displays'), {'return_value': []}),
                ((backend.Desktop, 'renderer'), {'return_value': 'native'})]:
            mocked = patch.object(*target, **options)
            mocked.start()
            self.addCleanup(mocked.stop)

    def save_descriptor(self):
        (self.root / standalone.RECORD).write_text(json.dumps(self.data), encoding='utf-8')

    def test_fresh_binding_and_relocation_preserve_saves_and_do_not_follow_old_paths(self):
        first = standalone.bind(self.root)
        self.assertEqual(first.game(), self.game)
        self.assertEqual(first.installed()['source'], '')
        saves = self.game / 'LocalData/saves'
        saves.mkdir()
        (saves / '1.s7local').write_bytes(b'personal save')
        # Deliberately retain an obsolete GPU selection in the old location.
        first.write(self.game / choice.CHOICE_FILE, {'format': choice.FORMAT, 'choice': 'Old GPU'})
        moved = self.fixture.root / 'different location'
        shutil.move(str(self.root), moved)
        # Place unrelated data at the old absolute location. Never touch it.
        self.game.mkdir(parents=True)
        sentinel = self.game / 'do-not-touch.txt'
        sentinel.write_bytes(b'foreign data')
        second = standalone.bind(moved)
        self.assertNotEqual(first.owner, second.owner)
        self.assertEqual(second.game(), moved / 'App/Instances' / self.instance)
        self.assertEqual((second.game() / 'LocalData/saves/1.s7local').read_bytes(), b'personal save')
        self.assertIsNone(choice._load_choice(second.game())['choice'])
        self.assertEqual(sentinel.read_bytes(), b'foreign data')
        self.assertEqual(second.installed()['previous'], [])

    def test_repeat_start_keeps_current_graphics_selection(self):
        first = standalone.bind(self.root)
        choice.save_choice(self.game, {'format': choice.FORMAT, 'choice': None, 'simple_menu': False})
        self.assertEqual(standalone.bind(self.root).game(), first.game())
        self.assertFalse(choice._load_choice(self.game)['simple_menu'])

    def test_corrupt_game_cannot_create_an_installation_record(self):
        (self.game / 'Base/BBA/map.bba').write_bytes(b'corrupt')
        with self.assertRaises(ValueError):
            standalone.bind(self.root)
        self.assertFalse((self.app / 'State/installation.json').exists())
        self.assertFalse((self.game / 'desktop-owner.json').exists())

    def test_corrupt_runtime_cannot_be_adopted(self):
        (self.game / 'Runtime/python/python.exe').write_bytes(b'corrupt')
        with self.assertRaises(ValueError):
            standalone.bind(self.root)
        self.assertFalse((self.app / 'State/installation.json').exists())

    def test_self_consistent_foreign_payload_cannot_be_adopted(self):
        (self.game / 'Base/Bin/uplay_r1_loader.dll').write_bytes(b'foreign loader')
        (self.game / 'compatibility.json').write_bytes(graphics.encoded(content.create(self.game)))
        with self.assertRaises(ValueError):
            standalone.bind(self.root)
        self.assertFalse((self.app / 'State/installation.json').exists())

    def test_package_contract_mismatch_and_traversal_are_rejected(self):
        self.data['package_id'] = 'b' * 64
        self.save_descriptor()
        with self.assertRaises(ValueError):
            standalone.bind(self.root)
        self.data['instance'] = '../outside'
        self.save_descriptor()
        with self.assertRaises(ValueError):
            standalone.bind(self.root)
        self.assertFalse((self.app / 'State/installation.json').exists())

    def test_external_managed_storage_is_never_rebound(self):
        desktop = standalone.bind(self.root)
        value = desktop.installed()
        value['container'] = str(self.fixture.root / 'external')
        value['previous'] = [{'container': str(self.fixture.root / 'external'), 'instance': 'c' * 32}]
        desktop.write(desktop.state / 'installation.json', value)
        with self.assertRaises(ValueError):
            standalone.bind(self.root)
        self.assertEqual(desktop.read(desktop.state / 'installation.json'), value)

    def test_overlaid_native_patch_repins_existing_copy_without_original_source(self):
        desktop = standalone.bind(self.root)
        value = desktop.installed()
        value['package_id'] = 'b' * 64
        desktop.write(desktop.state / 'installation.json', value)
        # The ZIP already supplied the complete, current game and manifests.
        with patch.object(backend.install, 'prepare', side_effect=AssertionError('must not prepare again')):
            adopted = standalone.bind(self.root)
        self.assertEqual(adopted.installed()['package_id'], self.data['package_id'])
        self.assertEqual(adopted.installed()['source'], '')

    def test_pristine_zip_overlay_preserves_saves_and_retires_owned_renderer_state(self):
        desktop = standalone.bind(self.root)
        saved = self.game / 'LocalData/saves'
        saved.mkdir(); (saved / '1.s7local').write_bytes(b'personal save')
        profile = self.game / graphics.STATE
        profile.mkdir(); (profile / 'cache').mkdir()
        (profile / 'cache/keep.bin').write_bytes(b'owned graphics cache')
        (self.game / graphics.DLL).write_bytes(b'owned dxvk')
        (self.game / graphics.CONF).write_bytes(graphics.CONFIG)
        with patch.object(graphics, 'DXVK_SHA256', content.digest(self.game / graphics.DLL)):
            adopted = standalone.bind(self.root)
        self.assertEqual(adopted.installed()['source'], '')
        self.assertEqual((saved / '1.s7local').read_bytes(), b'personal save')
        backups = list(profile.parent.glob(profile.name + '-standalone-*'))
        self.assertEqual(len(backups), 1)
        self.assertEqual((backups[0] / 'cache/keep.bin').read_bytes(), b'owned graphics cache')
        self.assertFalse((self.game / graphics.DLL).exists())
        self.assertFalse((self.game / 'standalone-renderer-recovery.json').exists())
        content.verify(self.game)

    def test_unknown_renderer_is_preserved_instead_of_adopted(self):
        standalone.bind(self.root)
        profile = self.game / graphics.STATE; profile.mkdir()
        (self.game / graphics.DLL).write_bytes(b'unknown user renderer')
        with self.assertRaises(ValueError):
            standalone.bind(self.root)
        self.assertEqual((self.game / graphics.DLL).read_bytes(), b'unknown user renderer')
        self.assertTrue(profile.exists())

    def test_owned_renderer_overlay_recovers_after_interrupted_file_removal(self):
        desktop = standalone.bind(self.root)
        profile = self.game / graphics.STATE; profile.mkdir()
        (self.game / graphics.DLL).write_bytes(b'owned dxvk')
        (self.game / graphics.CONF).write_bytes(graphics.CONFIG)
        pinned = content.digest(self.game / graphics.DLL)
        original_unlink = Path.unlink
        def interrupt(path, *args, **kwargs):
            if path == self.game / graphics.CONF:
                raise OSError('invented interruption')
            return original_unlink(path, *args, **kwargs)
        with patch.object(graphics, 'DXVK_SHA256', pinned), patch.object(Path, 'unlink', interrupt):
            with self.assertRaises(OSError):
                standalone.bind(self.root)
        self.assertTrue((self.game / 'standalone-renderer-recovery.json').exists())
        with patch.object(graphics, 'DXVK_SHA256', pinned):
            recovered = standalone.bind(self.root)
        self.assertEqual(recovered.installed()['source'], '')
        content.verify(self.game)

    def test_supported_steam_copy_is_adopted_without_disabling_steam_ownership(self):
        executable = self.game / 'Base/Bin/Settlers7R.exe'
        executable.write_bytes(b'invented original Steam executable')
        steam_hash = content.digest(executable)
        self.data['package_id'] = backend.Desktop(self.app).package_id()
        manifest_path = self.app / 'Package/package.json'
        manifest = json.loads(manifest_path.read_text())
        manifest['steam_history'] = True
        manifest_path.write_bytes(graphics.encoded(manifest))
        (self.game / 'compatibility.json').write_bytes(graphics.encoded(content.create(self.game)))
        with patch.object(backend.install, 'STEAM_EXE', steam_hash), \
                patch.object(backend.install, 'STEAM_ADAPTER_SHA256', manifest['files']['payload/uplay_r1_loader.dll']):
            adopted = standalone.bind(self.root)
        self.assertEqual(adopted.game(), self.game)
        self.assertEqual(executable.read_bytes(), b'invented original Steam executable')

    def test_steam_export_builds_from_supported_original_and_documents_client_requirement(self):
        source_exe = self.fixture.source / 'Base/Bin/Settlers7R.exe'
        source_exe.write_bytes(b'invented original Steam executable')
        steam_hash = content.digest(source_exe)
        manifest = json.loads((self.app / 'Package/package.json').read_text())
        manifest['steam_history'] = True
        (self.app / 'Package/package.json').write_bytes(graphics.encoded(manifest))
        clean = self.fixture.root / 'clean-release-app'; clean.mkdir()
        shutil.copytree(self.app / 'Package', clean / 'Package')
        output = self.fixture.root / 'steam-export'
        with patch.object(backend.install, 'STEAM_EXE', steam_hash), \
                patch.object(backend.install, 'STEAM_ADAPTER_SHA256', manifest['files']['payload/uplay_r1_loader.dll']), \
                redirect_stdout(io.StringIO()):
            standalone.build(clean, self.fixture.source, output)
        info = standalone.descriptor(output)
        exported = output / 'App/Instances' / info['instance']
        self.assertEqual((exported / 'Base/Bin/Settlers7R.exe').read_bytes(), source_exe.read_bytes())
        self.assertFalse((exported / 'LocalData').exists())
        self.assertIn('Steam geoeffnet lassen', (output / 'LIESMICH.txt').read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main()
