"""Original widget automation across recreated Lua pages; modeled native fact."""
from pathlib import Path
import sys,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/gold'),str(ROOT/'work/lua-test-runtime')]
import pair_interface
from lupa.lua51 import LuaRuntime

class ReadyActionsTests(unittest.TestCase):
    def page(self,native,mode='guest'):
        lua=LuaRuntime(unpack_returned_tuples=True)
        def query(command):
            if command=='weplay-crossplay-v1:mode':return mode
            self.assertEqual(command,'weplay-crossplay-v1:guest-ready-available')
            return native['available'] if not native['observed'] else '0'
        def activate(widget):
            self.assertEqual(widget,17);native['presses']+=1;native['observed']=True
        lua.globals().query=query;lua.globals().activate=activate
        lua.execute('''
C={session=true,host=false,slot=2,ready=false,enabled=true}
l={UI={WriteToConsole=query},Network={SessionIsInSession=function()return C.session end,
 IsHost=function()return C.host end,GetLocalPlayerSlot=function()return C.slot end,
 GetPlayerSlotReadyState=function(slot)assert(slot==2);return C.ready end},
 ui={isEnabled=function(id)assert(id==17);return C.enabled end},button={activate=activate}}
o={frame=9,slots={[2]={isLocalPlayerSlot=true,readyButtonID=17}}};c={}
''')
        lua.execute(pair_interface.GUEST_ACTIONS)
        return lua

    def test_new_lua_state_does_not_repeat_already_forwarded_original_ready(self):
        native={'available':'1','observed':False,'presses':0}
        first=self.page(native);self.assertTrue(first.globals().c.weplayCanAutoReady())
        first.globals().c.weplayAutoReady();self.assertEqual(native['presses'],1)
        # The displayed original Ready state is still false while its native
        # round trip is pending. A fresh Lua VM has lost every local latch.
        second=self.page(native);self.assertFalse(second.globals().C.ready)
        self.assertFalse(second.globals().c.weplayCanAutoReady())
        second.globals().c.weplayAutoReady();self.assertEqual(native['presses'],1)

    def test_unknown_or_rejected_native_metadata_never_activates_widget(self):
        for available in ('0',False,None,True,1,'true'):
            with self.subTest(available=available):
                native={'available':available,'observed':False,'presses':0};lua=self.page(native)
                self.assertFalse(lua.globals().c.weplayCanAutoReady());lua.globals().c.weplayAutoReady()
                self.assertEqual(native['presses'],0)

    def test_all_original_widget_and_session_conditions_remain_required(self):
        for field,value in [('session',False),('host',True),('slot',1),('ready',True),('enabled',False)]:
            with self.subTest(field=field):
                native={'available':'1','observed':False,'presses':0};lua=self.page(native);lua.globals().C[field]=value
                lua.globals().c.weplayAutoReady();self.assertEqual(native['presses'],0)

    def test_legacy_nonmixed_page_uses_original_conditions(self):
        native={'available':False,'observed':False,'presses':0};lua=self.page(native,'solo')
        lua.globals().c.weplayAutoReady();self.assertEqual(native['presses'],1)

if __name__=='__main__':unittest.main()
