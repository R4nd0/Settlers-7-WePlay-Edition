"""Exercise the actual guest-page extension against Lua 5.1 callbacks."""
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'work/lua-test-runtime'))
from lupa.lua51 import LuaRuntime

BACKEND='''
local C={updates=0,destroys=0,transitions=0,queries=0,inSession=true,host=false,slot=2,native=false,teams=false}
C.State={frame={},callbacks={toJoinLobby=function(options) C.transitions=C.transitions+1;C.options=options end}}
C.Panel={update=function() C.updates=C.updates+1;return 42 end,destroy=function() C.destroys=C.destroys+1;return 43 end}
C.G={Network={SessionIsInSession=function() return C.inSession end,IsHost=function() return C.host end,
 GetLocalPlayerSlot=function() return C.slot end,GetIsTeams=function() return C.teams end},
 UI={WriteToConsole=function(command) assert(command=='weplay-crossplay-guest-presentation-v1');C.queries=C.queries+1;return C.native end}}
return C
'''

class GuestLobbyTests(unittest.TestCase):
    def setUp(self):
        self.lua=LuaRuntime(unpack_returned_tuples=True)
        self.c=self.lua.execute(BACKEND)
        self.lua.execute((ROOT/'tools/gold/history_guest_lobby.lua').read_text(),self.c.G,self.c.Panel,self.c.State)

    def test_transition_uses_original_menu_callback_once_and_preserves_options(self):
        self.assertEqual(self.c.Panel.update(),42)
        self.c.native=True;self.c.teams=True
        self.c.Panel.update();self.c.Panel.update()
        self.assertEqual(self.c.transitions,1)
        self.assertTrue(self.c.options.fixedTeams and self.c.options.fixedPositions)
        self.assertEqual(self.c.updates,2)

    def test_no_session_host_or_wrong_slot_never_queries_native_transition(self):
        self.c.native=True
        for key,value in [('inSession',False),('host',True),('slot',1)]:
            before=self.c[key];self.c[key]=value
            self.assertEqual(self.c.Panel.update(),42)
            self.c[key]=before
        self.assertEqual(self.c.transitions,0)
        self.assertEqual(self.c.queries,0)

    def test_only_boolean_true_can_select_guest_page(self):
        for value in [False,None,0,1,'true','ready']:
            self.c.native=value
            self.assertEqual(self.c.Panel.update(),42)
        self.assertEqual(self.c.transitions,0)

    def test_destroy_retains_original_result_and_resets_interval_latch(self):
        self.c.native=True;self.c.Panel.update()
        self.assertEqual(self.c.Panel.destroy(),43)
        self.c.native=False;self.c.Panel.update()
        self.assertEqual(self.c.transitions,1)
        self.c.native=True;self.c.Panel.update()
        self.assertEqual(self.c.transitions,2)
        self.assertEqual(self.c.destroys,1)

    def test_closed_frame_or_missing_callback_cannot_transition(self):
        self.c.native=True;self.c.State.frame=None
        self.c.Panel.update();self.c.State.frame=self.lua.table();self.c.State.callbacks.toJoinLobby=None
        self.c.Panel.update()
        self.assertEqual(self.c.transitions,0)
        self.assertEqual(self.c.queries,0)

if __name__=='__main__':unittest.main()
