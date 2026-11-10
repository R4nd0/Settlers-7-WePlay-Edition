"""Invented Gold-style script headers and authored local profile fallback only."""
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('gold_profile_interface', ROOT / 'tools/gold/profile_interface.py')
gold = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gold)


def script(body):
    values = ['caption', 'print']
    rows = []
    for value in values:
        previous, row = len(values), []
        for char in value:
            row.append(previous + ord(char))
            previous = ord(char)
        rows.append(row)
    table = json.dumps(rows, separators=(',', ':')).replace('[', '{').replace(']', '}')
    return ('local l,n,a,f=_G\ndo\nlocal rows,cache,value=' + table + ',{},nil\n'
            'n,a,f=function(x)return x end,function(x)return x end,function(x)return x end\n'
            'end\nend\n' + body).encode()


class GoldProfileTests(unittest.TestCase):
    def test_gold_getter_and_global_roles_are_decoded(self):
        _, view, _ = gold.decoded_view(script('return n(1),a(2)\n'))
        self.assertEqual(view, 'return "caption",_G["print"]\n')

    def test_only_neutral_fallback_and_local_label_are_added(self):
        original = script(gold.TITLE_ANCHOR + '\nlocal label=' + gold.ACCOUNT_LABEL + '\nreturn n(1)\n')
        patched = gold.profile_script(original)
        _, view, _ = gold.decoded_view(patched)
        self.assertIn(gold.NO_TITLE, view)
        self.assertIn(gold.LOCAL_LABEL, view)
        self.assertNotIn(gold.ACCOUNT_LABEL, view)
        self.assertIn('return "caption"', view)
        self.assertIn('TitleType=28,CurrentRank=0', view)
        self.assertNotIn('PlayerProfileUpdate', view)
        self.assertNotIn('BindToUbiDotCom', view)

    def test_missing_duplicate_or_changed_anchors_are_rejected(self):
        for body in (gold.TITLE_ANCHOR, gold.ACCOUNT_LABEL,
                     gold.TITLE_ANCHOR * 2 + gold.ACCOUNT_LABEL,
                     gold.TITLE_ANCHOR + gold.ACCOUNT_LABEL * 2):
            with self.assertRaisesRegex(ValueError, 'anchor'):
                gold.profile_script(script(body))

    def test_history_decoder_roles_and_bad_indices_are_rejected(self):
        original = script('return n(1)')
        with self.assertRaisesRegex(ValueError, 'roles'):
            gold.decoded_view(original.replace(b'n,a,f=function(', b'a,f,n=function('))
        with self.assertRaisesRegex(ValueError, 'out of bounds'):
            gold.decoded_view(script('return n(3)'))

    def test_unknown_archive_is_rejected_before_parsing(self):
        with self.assertRaisesRegex(ValueError, 'exact original Gold'):
            gold.build(b'invented archive')

    def test_disconnect_edit_requires_exactly_one_original_anchor(self):
        for body in ('return false', gold.DISCONNECT_ANCHOR * 2):
            with self.assertRaisesRegex(ValueError, 'disconnect anchor'):
                gold.game_script(script(body))
        _, view, _ = gold.decoded_view(gold.game_script(script(gold.DISCONNECT_ANCHOR + '\nreturn n(1)\nend')))
        self.assertIn(gold.LOCAL_DISCONNECT, view)
        self.assertIn('return "caption"', view)

    def test_disconnect_condition_preserves_all_network_modes(self):
        try:
            from lupa.lua51 import LuaRuntime
        except ImportError:
            self.skipTest('Optional Lua 5.1 runtime unavailable')
        lua = LuaRuntime(unpack_returned_tuples=True)
        run = lua.eval('function(connected,disconnected,multiplayer,network) '
                       'local l=connected; local e={game={isDisconnected=disconnected,isMultiplayerGame=multiplayer},'
                       'Network={InNetworkMode=function() return network end}}; '
                       + gold.LOCAL_DISCONNECT + ' return true end return false end')
        for connected in (False, True):
            for disconnected in (False, True):
                for multiplayer in (False, True):
                    for network in (False, True):
                        self.assertEqual(run(connected, disconnected, multiplayer, network),
                                         not connected and not disconnected and (multiplayer or network))

    def test_authored_fallback_preserves_existing_titles_and_uses_no_earned_rank(self):
        try:
            from lupa.lua51 import LuaRuntime
        except ImportError:
            self.skipTest('Optional Lua 5.1 runtime unavailable')
        lua = LuaRuntime(unpack_returned_tuples=True)
        run = lua.eval('function(value) e={titleData=value}; ' + gold.NO_TITLE + ' return e.titleData end')
        result = run(None)
        self.assertEqual(result[1]['TitleType'], 28)
        self.assertEqual(result[1]['CurrentRank'], 0)
        empty = run(lua.table())
        self.assertEqual(empty[1]['TitleType'], 28)
        existing = lua.table_from([lua.table_from({'TitleType': 2, 'CurrentRank': 4, 'CurrentTitleValue': 123})])
        same = run(existing)
        self.assertEqual(same[1]['TitleType'], 2)
        self.assertEqual(same[1]['CurrentRank'], 4)
        self.assertEqual(same[1]['CurrentTitleValue'], 123)


if __name__ == '__main__':
    unittest.main()
