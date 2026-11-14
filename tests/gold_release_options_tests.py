"""Authored lifecycle model: Gold removes dead routes without breaking callers."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/gold'),str(ROOT/'work/lua-test-runtime')]
import release_interface as release
from lupa.lua51 import LuaRuntime


class GoldReleaseOptionsTests(unittest.TestCase):
    def test_game_menu_keeps_working_routes_and_never_registers_retired_social_actions(self):
        body=('local e=...;local i,l=0,0;local t={actions={options=1,chat=2,contacts=3,mailbox=4,exit=5}};'
            'local o=function(x,y,label,action) e.record(label,action);return x,y+1 end;'
            'i,l=o(i,l,"OPTIONS",t.actions.options);'
            'if e["Network"]["IsLoggedIn"]()then '
            'i,l=o(i,l,"Global Chat",t.actions.chat);i,l=o(i,l,"CONTACTS",t.actions.contacts);'
            'local f;f,l=o(i,l,"INBOX",t["actions"]["mailbox"])end;'
            'i,l=o(i,l,"EXIT",t.actions.exit)')
        body=body.replace(';','\n')
        with patch.object(release.menu.profile,'decoded_view',side_effect=lambda raw:(raw.decode(),raw.decode(),list(range(len(raw)+1)))):
            patched=release.game_menu_script(body.encode()).decode()
        lua=LuaRuntime(unpack_returned_tuples=True)
        run=lua.eval('''function(source)
          local labels={};local api={record=function(label) labels[#labels+1]=label end,
            Network={IsLoggedIn=function() error("retired service must not be queried") end}}
          assert(loadstring(source))(api);return table.concat(labels,",")
        end''')
        self.assertEqual(run(patched),'OPTIONS,EXIT')
        with patch.object(release.menu.profile,'decoded_view',return_value=('unknown','','')):
            with self.assertRaisesRegex(ValueError,'anchors changed'):release.game_menu_script(b'unknown')

    def test_castle_control_tracks_the_actual_menu_map_and_no_multiplayer_is_created(self):
        body=('local e=...;local i={sessionListCB=function()end,castleForgeCB=function()end};local l,o=1,1;'
            'e["fancyDialog"]["addButton"](1,25,300+o,l,"Multiplayer",i["sessionListCB"],'
            '"Play against other Players. Create your own session or enter an already existing one.");'
            'e["fancyDialog"]["setButtonState"](1,"Multiplayer",l);'
            'e["fancyDialog"]["addButton"](2,45,250+l,o,"Castle Forge",i["castleForgeCB"],"Construct your Castle.");'
            'e["fancyDialog"]["setButtonState"](2,"Castle Forge",l);return 42')
        body=body.replace(';','\n')
        with patch.object(release.menu.profile,'decoded_view',side_effect=lambda raw:(raw.decode(),raw.decode(),list(range(len(raw)+1)))):
            patched=release.hud_script(body.encode()).decode()
        lua=LuaRuntime(unpack_returned_tuples=True)
        run=lua.eval('''function(source,map)
          local labels,states={},{};local api={}
          api.require=function(name) assert(name=="local.tools.boot");return {mapInMainMenu=map} end
          api.fancyDialog={addButton=function(group,x,y,w,label) labels[#labels+1]=label end,
            setButtonState=function(group,label) states[#states+1]=label end}
          assert(loadstring(source))(api)
          return table.concat(labels,","),table.concat(states,",")
        end''')
        self.assertEqual(run(patched,False),('',''))
        self.assertEqual(run(patched,True),('Castle Forge','Castle Forge'))

    def test_unknown_release_archive_is_rejected_before_build_or_parse(self):
        with patch.object(release.menu,'build') as build:
            with self.assertRaisesRegex(ValueError,'exact original'):release.build(b'foreign')
            build.assert_not_called()


if __name__=='__main__':unittest.main()
