"""Relocation, corruption and ownership boundaries using invented game files."""
import json
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


if __name__ == '__main__':
    unittest.main()
