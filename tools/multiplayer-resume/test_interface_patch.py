"""Archive safety and real-fixture compilation; no game content is distributed."""
from pathlib import Path
import sys
import unittest

import interface_patch

REPO = Path(__file__).resolve().parents[2]
FIXTURE = REPO / 'work/mp-resume-20261102/fixture-backup/B-Interface.bba'
sys.path.insert(0, str(REPO / 'work/lua-test-runtime'))
try:
    from lupa.lua51 import LuaRuntime
except ImportError:
    LuaRuntime = None


class InterfacePatchTests(unittest.TestCase):
    def test_unrecognized_inputs_rejected_before_archive_parsing(self):
        for value in (b'', b'BAF\x06' + b'\0' * 32, b'unknown modified archive'):
            with self.assertRaisesRegex(ValueError, 'fingerprinted'):
                interface_patch.build(value)

    @unittest.skipUnless(FIXTURE.is_file() and LuaRuntime is not None,
                         'Private native fixture and Lua5.1 runtime required')
    def test_known_fixture_preserves_sp_autosave_and_compiles_every_changed_member(self):
        source = FIXTURE.read_bytes()
        before = interface_patch.Archive(source)
        result = interface_patch.build(source)
        after = interface_patch.Archive(result)
        self.assertEqual(before.entries.keys(), after.entries.keys())
        compile_ = LuaRuntime().eval('function(s) local f,e=loadstring(s);assert(f,e);return true end')
        changed = []
        for name, entry in before.entries.items():
            if entry.values[4] == 256:
                continue
            old, new = before.read(name), after.read(name)
            if old != new:
                self.assertTrue(name.endswith('.lua'))
                self.assertTrue(compile_(new.decode('utf-8-sig')))
                changed.append(name)
        self.assertEqual(len(changed), 7)
        game = 'Script/local/menu/game.lua'
        old = interface_patch.decoded_view(before.read(game))[1]
        new = interface_patch.decoded_view(after.read(game))[1]
        begin = old.index('if e["game"]["lastSaveTime"]+5*60*1000')
        end = old.index('\nend,["handleFirstUpdate"]', begin)
        self.assertIn(old[begin:end], new)
        self.assertEqual(old.count('pcall'), new.count('pcall'))
        self.assertEqual(FIXTURE.read_bytes(), source)


if __name__ == '__main__':
    unittest.main()
