"""Exercise the actual guest-page extension against Lua 5.1 callbacks."""
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'work/lua-test-runtime'))
from lupa.lua51 import LuaRuntime

BACKEND='''
local C={updates=0,destroys=0,transitions=0,queries=0,loads=0,begins=0,commits=0,beginAllowed=true,commitAllowed=true,restored=true,inSession=true,host=false,slot=2,native=false,teams=false,
 map='Maps/Skirmish/Monhaim',current='Maps/Skirmish/Tower',valid=true,coop=false}
C.State={frame={},callbacks={toJoinLobby=function(options) assert(C.current==C.map and C.restored);C.transitions=C.transitions+1;C.options=options end}}
C.Panel={update=function() C.updates=C.updates+1;return 42 end,destroy=function() C.destroys=C.destroys+1;return 43 end}
C.G={Network={SessionIsInSession=function() return C.inSession end,IsHost=function() return C.host end,
 GetLocalPlayerSlot=function() return C.slot end,GetIsTeams=function() return C.teams end,
 GetMap=function() return C.map end,GetMapName=function() error('stale backend room name must not select the host map') end,
 GetMapForgeTemplateName=function() return C.template end,GetIsCoop=function() return C.coop end},
 map={loadSetupMapConfig=function(path,template) C.loads=C.loads+1;C.restored=false;C.loadedTemplate=template;if C.reenter then C.Panel.update() end;if C.loadError then error('synthetic load error') end;if not C.stale then C.current=path end end},
 MapSetup={GetCurrentMapData=function() return {FilePath=C.current} end,IsValid=function() return C.valid end,
 SetCoopMode=function(value) C.coopMode=value end},
 UI={WriteToConsole=function(command)
 if command=='weplay-crossplay-map-reload-begin-v1' then C.begins=C.begins+1;return C.beginAllowed end
 if command=='weplay-crossplay-map-reload-commit-v1' then C.commits=C.commits+1;C.restored=C.commitAllowed;return C.commitAllowed end
 assert(command=='weplay-crossplay-guest-presentation-v1');C.queries=C.queries+1;if not C.restored then return false end;return C.native end}}
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
        self.c.Panel.update();self.c.Panel.update();self.c.Panel.update()
        self.assertEqual(self.c.transitions,1)
        self.assertTrue(self.c.options.fixedTeams and self.c.options.fixedPositions)
        self.assertEqual(self.c.updates,2)
        self.assertEqual(self.c.loads,1)
        self.assertEqual(self.c.current,self.c.map)
        self.assertEqual(self.c.coopMode,0)

    def test_no_session_host_or_wrong_slot_never_queries_native_transition(self):
        self.c.native=True
        for key,value in [('inSession',False),('host',True),('slot',1)]:
            before=self.c[key];self.c[key]=value
            self.assertEqual(self.c.Panel.update(),42)
            self.c[key]=before
        self.assertEqual(self.c.transitions,0)
        self.assertEqual(self.c.queries,0)

    def test_native_map_load_precedes_guest_page_and_preserves_template_and_coop(self):
        self.c.native=True;self.c.template='WePlay QA Max25';self.c.coop=True
        self.c.Panel.update();self.c.Panel.update()
        self.assertEqual(self.c.current,'Maps/Skirmish/Monhaim')
        self.assertEqual(self.c.loadedTemplate,'WePlay QA Max25')
        self.assertEqual(self.c.coopMode,1)
        self.assertEqual(self.c.transitions,1)

    def test_failed_invalid_or_stale_load_never_opens_page_or_reloads_in_loop(self):
        for field in ('loadError','stale','valid'):
            with self.subTest(field=field):
                self.setUp();self.c.native=True;self.c[field]=False if field=='valid' else True
                if field=='loadError':
                    with self.assertRaisesRegex(Exception,'synthetic load error'):self.c.Panel.update()
                else:self.c.Panel.update()
                self.c.Panel.update()
                self.assertEqual(self.c.transitions,0)
                self.assertEqual(self.c.loads,1)

    def test_loader_reentry_cannot_load_or_transition_twice(self):
        self.c.native=True;self.c.reenter=True
        self.c.Panel.update();self.c.Panel.update()
        self.assertEqual(self.c.loads,1)
        self.assertEqual(self.c.transitions,1)

    def test_missing_native_map_never_loads_or_transitions(self):
        for value in (None,False,''):
            self.setUp();self.c.native=True;self.c.map=value
            self.c.Panel.update()
            self.assertEqual(self.c.loads,0)
            self.assertEqual(self.c.transitions,0)

    def test_engine_without_pcall_still_uses_original_loader_once(self):
        self.lua.globals().pcall=None;self.c.native=True
        self.c.Panel.update();self.c.Panel.update()
        self.assertEqual(self.c.loads,1)
        self.assertEqual(self.c.transitions,1)

    def test_engine_protected_call_override_cannot_intercept_native_loader(self):
        self.lua.execute("pcall=function() error('must use direct native call') end")
        self.c.native=True;self.c.Panel.update();self.c.Panel.update()
        self.assertEqual(self.c.loads,1)
        self.assertEqual(self.c.transitions,1)

    def test_only_boolean_true_can_select_guest_page(self):
        for value in [False,None,0,1,'true','ready']:
            self.c.native=value
            self.assertEqual(self.c.Panel.update(),42)
        self.assertEqual(self.c.transitions,0)

    def test_destroy_retains_original_result_and_resets_interval_latch(self):
        self.c.native=True;self.c.Panel.update();self.c.Panel.update()
        self.assertEqual(self.c.Panel.destroy(),43)
        self.c.native=False;self.c.Panel.update()
        self.assertEqual(self.c.transitions,1)
        self.c.native=True;self.c.Panel.update();self.c.Panel.update()
        self.assertEqual(self.c.transitions,2)
        self.assertEqual(self.c.destroys,1)

    def test_closed_frame_or_missing_callback_cannot_transition(self):
        self.c.native=True;self.c.State.frame=None
        self.c.Panel.update();self.c.State.frame=self.lua.table();self.c.State.callbacks.toJoinLobby=None
        self.c.Panel.update()
        self.assertEqual(self.c.transitions,0)
        self.assertEqual(self.c.queries,0)

    def test_native_restoration_must_be_observed_before_guest_page(self):
        self.c.native=True;self.c.commitAllowed=False
        self.c.Panel.update();self.c.Panel.update();self.c.Panel.update()
        self.assertEqual(self.c.loads,1)
        self.assertEqual(self.c.begins,1)
        self.assertEqual(self.c.commits,2)
        self.assertEqual(self.c.transitions,0)
        self.c.commitAllowed=True;self.c.Panel.update()
        self.assertEqual(self.c.transitions,1)

    def test_begin_requires_boolean_true_and_cannot_reload_on_failure(self):
        for value in (False,None,0,1,'true'):
            self.setUp();self.c.native=True;self.c.beginAllowed=value
            self.c.Panel.update();self.c.Panel.update()
            self.assertEqual(self.c.loads,0)
            self.assertEqual(self.c.begins,1)
            self.assertEqual(self.c.transitions,0)

    def test_wait_cancels_on_session_or_map_change_without_native_commit(self):
        for field,value in [('inSession',False),('host',True),('slot',1),('map','Maps/Other'),('current','Maps/Other'),('valid',False)]:
            with self.subTest(field=field):
                self.setUp();self.c.native=True;self.c.Panel.update()
                self.c[field]=value;self.c.Panel.update();self.c.Panel.update()
                self.assertEqual(self.c.loads,1)
                self.assertEqual(self.c.commits,0)
                self.assertEqual(self.c.transitions,0)

if __name__=='__main__':unittest.main()
