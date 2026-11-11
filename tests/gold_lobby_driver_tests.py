"""Real Lua driver tests: engine outcomes, edition arities and once-only jobs."""
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'work/lua-test-runtime'))
from lupa.lua51 import LuaRuntime

BACKEND='''
local C={now=0,loggedIn=false,natReady=true,browserCreated=true,inSession=false,success=true,creates=0,transitions=0,closes=0,privateCalls=0,phases={},maps={"Maps/Skirmish/Monhaim"}}
C.Main={};C.Profile={};C.Browser={isCreated=function() return C.browserCreated end};C.State={};C.State.currentPageModule=C.Browser
C.State.actionManager={createCallbacks=function(sender) assert(sender==C.Browser);return {shutdown=function() C.closes=C.closes+1 end} end}
C.actions={toMainMenu=4,toSessionList=105,toHostLobby=108};C.options={gold=false,duration=90000}
C.report=function(phase) C.phases[#C.phases+1]=phase end
C.transition=function(action) C.transitions=C.transitions+1;C.action=action end
C.G={release=true,Game={GetRunTime=function() return C.now end},system={exitApi=function() error('must use original shutdown action') end},
 Network={IsLoggedIn=function() return C.loggedIn end,IsNATReady=function() return C.natReady end,SessionIsInSession=function() return C.inSession end,
 SetIsPrivate=function(value) assert(value==false);C.privateCalls=C.privateCalls+1 end,
 SessionCreate=function(...) C.creates=C.creates+1;C.args={n=select('#',...),...};if C.reenter then C.driver.tick() end;return C.success end,
 StartGame=function() error('driver must not start a game') end,TogglePlayerReadyState=function() error('driver must not forge readiness') end},
 MapSetup={GetSkirmishMaps=function(...) assert(select('#',...)==1 and select(1,...)==true);return C.maps end,
 GetMapDataByFilename=function(path) return {Name=C.mapName or path,MapPath=C.mapPath or path,Locked=false} end,
 SetCoopMode=function(mode) C.coop=mode end},
 map={loadSetupMapConfig=function(path,template) C.map=path;C.template=template end}}
return C
'''

class DriverTests(unittest.TestCase):
    def setUp(self):
        self.lua=LuaRuntime(unpack_returned_tuples=True)
        self.c=self.lua.execute(BACKEND)
        c=self.c
        self.driver=self.lua.execute((ROOT/'tools/gold/native_lobby_driver.lua').read_text(),c.G,c.State,c.Main,c.Profile,c.Browser,c.transition,c.actions,c.options,c.report)
        c.driver=self.driver

    def test_waits_for_actual_login_and_main_page(self):
        self.driver.tick();self.assertEqual(self.c.creates,0)
        self.c.loggedIn=True;self.c.State.currentPageModule=self.lua.table()
        self.driver.tick();self.assertEqual(self.c.creates,0)
        self.c.State.currentPageModule=self.c.Browser;self.driver.tick()
        self.assertEqual(self.c.creates,1);self.assertEqual(self.driver.state(),'created')

    def test_history_native_argument_list_and_original_transition(self):
        self.c.loggedIn=True;self.driver.tick()
        self.assertEqual(self.c.args.n,8)
        self.assertEqual([self.c.args[i] for i in range(1,9)],['Maps/Skirmish/Monhaim',False,False,2,False,False,None,False])
        self.assertEqual(self.c.privateCalls,1);self.assertEqual(self.c.coop,0)
        self.assertEqual(self.c.action.actionType,108)
        self.assertFalse(self.c.action.data.fixedTeams or self.c.action.data.fixedPositions)

    def test_gold_retains_its_different_seven_argument_api(self):
        self.c.options.gold=True;self.c.loggedIn=True;self.driver.tick()
        self.assertEqual(self.c.args.n,7)
        self.assertEqual([self.c.args[i] for i in range(1,8)],['Maps/Skirmish/Monhaim',False,2,False,False,None,False])
        self.assertEqual(self.c.privateCalls,0)

    def test_native_false_result_stops_without_host_page_or_retry(self):
        self.c.success=False;self.c.loggedIn=True
        self.driver.tick();self.driver.tick()
        self.assertEqual(self.driver.state(),'failed');self.assertEqual(self.c.creates,1)
        self.assertEqual(self.c.transitions,0);self.assertIsNone(self.c.coop)

    def test_reentrant_job_callback_cannot_create_second_session(self):
        self.c.reenter=True;self.c.loggedIn=True;self.driver.tick()
        self.assertEqual(self.c.creates,1)

    def test_ambiguous_map_is_rejected_before_native_job(self):
        self.c.loggedIn=True;self.c.maps=self.lua.table_from(['Monhaim','another_Monhaim'])
        self.driver.tick();self.assertEqual(self.c.creates,0)
        self.assertEqual(self.driver.state(),'failed')

    def test_existing_session_is_excluded(self):
        self.c.loggedIn=True;self.c.inSession=True;self.driver.tick()
        self.assertEqual(self.c.creates,0);self.assertEqual(self.driver.state(),'failed')

    def test_profile_exit_uses_original_action_once_then_waits(self):
        self.c.loggedIn=True;self.c.State.currentPageModule=self.c.Profile
        self.driver.tick();self.driver.tick()
        self.assertEqual(self.c.transitions,1);self.assertEqual(self.c.action.actionType,4)
        self.assertEqual(self.c.creates,0)
        self.c.State.currentPageModule=self.c.Browser;self.driver.tick()
        self.assertEqual(self.c.creates,1)

    def test_opens_original_browser_once_and_hosts_without_quickmatch_nat_gate(self):
        self.c.loggedIn=True;self.c.State.currentPageModule=self.c.Main
        self.driver.tick();self.driver.tick()
        self.assertEqual(self.c.transitions,1);self.assertEqual(self.c.action.actionType,105)
        self.assertEqual(self.c.creates,0)
        self.c.State.currentPageModule=self.c.Browser;self.c.natReady=False
        self.driver.tick();self.assertEqual(self.c.creates,1)

    def test_bounded_run_closes_normally_only_once(self):
        self.c.now=90000;self.driver.tick();self.driver.tick()
        self.assertEqual(self.c.closes,1);self.assertEqual(self.c.creates,0)
        self.assertEqual(self.driver.state(),'closing')

    def test_queued_page_waits_until_original_browser_is_created(self):
        self.c.loggedIn=True;self.c.browserCreated=False;self.driver.tick()
        self.assertEqual(self.c.creates,0)
        self.c.browserCreated=True;self.driver.tick();self.assertEqual(self.c.creates,1)

    def test_map_selection_uses_native_metadata_and_canonical_path(self):
        self.c.loggedIn=True;self.c.maps=self.lua.table_from(['map_001'])
        self.c.mapName='Monhaim';self.c.mapPath='Maps/Canonical/001'
        self.driver.tick();self.assertEqual(self.c.args[1],'Maps/Canonical/001')

    def test_lua_job_error_fails_then_reaches_normal_shutdown_deadline(self):
        self.c.loggedIn=True
        self.lua.execute("local c=...;c.G.map.loadSetupMapConfig=function() error('synthetic loader failure') end",self.c)
        self.driver.tick();self.assertEqual(self.driver.state(),'failed')
        self.assertEqual(self.c.creates,0)
        self.c.now=90000;self.driver.tick();self.assertEqual(self.c.closes,1)

if __name__=='__main__':unittest.main()
