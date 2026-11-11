"""Gold menu boundaries and the non-networking news module lifecycle."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/gold'), str(ROOT / 'work/lua-test-runtime')]
import menu_interface as menu
from lupa.lua51 import LuaRuntime


class GoldMenuTests(unittest.TestCase):
    def test_unknown_archive_is_rejected_before_archive_parser(self):
        with patch.object(menu.profile, 'Archive') as archive:
            with self.assertRaisesRegex(ValueError, 'exact original Gold'):
                menu.build(b'unknown or already modified archive')
            archive.assert_not_called()

    def test_change_rejects_missing_and_duplicate_anchors(self):
        for body in ('missing', 'anchor anchor'):
            with patch.object(menu.profile, 'decoded_view', return_value=(body, body, list(range(len(body)+1)))):
                with self.assertRaisesRegex(ValueError, 'exactly once'):
                    menu.change(body.encode(), 'anchor', 'replacement')

    def test_sections_reject_reversed_or_ambiguous_boundaries(self):
        for body in ('end start', 'start start end', 'start missing'):
            with patch.object(menu.profile, 'decoded_view', return_value=(body, body, [])):
                with self.assertRaises(ValueError):
                    menu.section(body.encode(), 'start', 'end', '')

    def test_quiet_news_supports_reentry_and_teardown_without_feeds_or_links(self):
        lua = LuaRuntime()
        state = lua.execute('''
local state = {created=0,destroyed=0}
local fail = function() error("unexpected service or UI dependency") end
l = {desktop={frame=17},defaultPriorities={base=2},frame={
 create=function(name,parent,x,y,w,h,priority)
  assert(parent==17 and x==0 and y==0 and w==0 and h==0 and priority==2)
  state.created=state.created+1; return state.created
 end,
 destroy=function(frame) assert(frame~=nil);state.destroyed=state.destroyed+1 end}}
setmetatable(l,{__index=fail})
return state
''')
        news = lua.execute(menu.QUIET_NEWS)
        self.assertFalse(news.isCreated())
        news.destroy()
        for _ in range(3):
            news.create(); news.create()
            self.assertTrue(news.isCreated())
            self.assertIsNotNone(news.getFrame())
            for _ in range(20):
                news.update()
            news.destroy(); news.destroy()
            self.assertFalse(news.isCreated())
            self.assertIsNone(news.getFrame())
        self.assertEqual((state.created, state.destroyed), (3, 3))


if __name__ == '__main__':
    unittest.main()
