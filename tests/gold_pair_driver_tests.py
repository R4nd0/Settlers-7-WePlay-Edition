"""Lua 5.1 job suspension and original-action guards; not game qualification."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'work/lua-test-runtime'))
from lupa.lua51 import LuaRuntime

BACKEND = r'''
local C={now=0,logged=true,inSession=false,host=false,slot=2,valid=true,
    browserReady=true,
    maps={'Map/Monhaim'},current='Map/Monhaim',success=true,creates=0,
    loads=0,discoveries=0,readies=0,starts=0,closes=0,transitions=0,private=0,coop=0,phases={}}
local function page()return {isCreated=function()return true end}end
C.Pages={Main=page(),Profile=page(),Browser=page(),Host=page(),Guest=page()}
C.Pages.Browser.weplayCanAutoCreate=function()return C.browserReady end
C.State={currentPageModule=C.Pages.Browser,actionManager={createCallbacks=function()
    return {shutdown=function()C.closes=C.closes+1 end}end}}
C.Pages.Host.weplayCanAutoStart=function()return C.canStart==true end
C.Pages.Host.weplayCanConfigureAI=function()return C.otherHuman~=false end
C.Pages.Host.weplayAutoStart=function()C.starts=C.starts+1;if C.yieldStart then coroutine.yield('start')end end
C.Pages.Guest.weplayCanAutoReady=function()return C.canReady==true end
C.Pages.Guest.weplayAutoReady=function()C.readies=C.readies+1;if C.yieldReady then coroutine.yield('ready')end end
C.actions={toMainMenu=4,toSessionList=105,toHostLobby=108}
C.transition=function(a)C.transitions=C.transitions+1;C.action=a end
C.options={duration=90000,gold=false,host=false}
C.report=function(p)C.phases[#C.phases+1]=p end
C.G={release=true,Game={GetRunTime=function()return C.now end},
 Network={IsLoggedIn=function()return C.logged end,SessionIsInSession=function()return C.inSession end,
 IsHost=function()return C.host end,GetLocalPlayerSlot=function()return C.slot end,
 SetIsPrivate=function(v)assert(v==false);C.private=C.private+1 end,
 SessionCreate=function(...)C.creates=C.creates+1;C.args={n=select('#',...),...};
    if C.reenter then C.driver.tick()end;if C.yieldCreate then coroutine.yield('create')end;return C.success end},
 MapSetup={GetSkirmishMaps=function(...)assert(select('#',...)==1 and select(1,...)==true);C.discoveries=C.discoveries+1;
    if C.reenter then C.driver.tick()end;if C.yieldDiscovery then coroutine.yield('discovery')end;return C.maps end,
 GetMapDataByFilename=function(p)return {Name='Monhaim',MapPath=p,Locked=C.locked}end,
 GetCurrentMapData=function()return {FilePath=C.current}end,IsValid=function()return C.valid end,
 SetCoopMode=function(v)assert(v==0);C.coop=C.coop+1 end},
 map={loadSetupMapConfig=function(p,t)C.loads=C.loads+1;assert(t==nil);
    if C.reenter then C.driver.tick()end;if C.yieldLoad then coroutine.yield('load')end end}}
return C
'''


class PairDriverTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.c = self.lua.execute(BACKEND)
        c = self.c
        self.driver = self.lua.execute((ROOT / 'tools/gold/native_pair_driver.lua').read_text(),
            c.G, c.State, c.Pages, c.transition, c.actions, c.options, c.report)
        c.driver = self.driver

    def test_native_jobs_suspend_and_resume_without_protected_c_boundary(self):
        self.c.yieldDiscovery = self.c.yieldLoad = self.c.yieldCreate = self.c.reenter = True
        resume = self.lua.eval('function(driver) local co=coroutine.create(driver.tick); '
            'return function()return coroutine.resume(co)end end')(self.driver)
        self.assertEqual(resume(), (True, 'discovery'))
        self.assertEqual(self.driver.state(), 'map-discovery')
        self.driver.tick();self.assertEqual(self.c.discoveries,1)
        self.assertEqual(resume(), (True, 'load'))
        self.assertEqual(self.driver.state(), 'map-loading')
        self.driver.tick()
        self.assertEqual(self.c.loads, 1)
        self.assertEqual(resume(), (True, 'create'))
        self.assertEqual(self.driver.state(), 'session-creating')
        self.driver.tick()
        self.assertTrue(resume())
        self.assertEqual((self.c.loads, self.c.creates, self.c.transitions), (1, 1, 1))
        self.assertEqual(self.driver.state(), 'created')

    def test_original_gold_and_history_arities(self):
        for gold, arity in ((False, 8), (True, 7)):
            with self.subTest(gold=gold):
                self.setUp(); self.c.options.gold = gold; self.driver.tick()
                self.assertEqual(self.c.args.n, arity)
                self.assertEqual(self.c.private, 0 if gold else 1)
                self.assertEqual(self.c.action.actionType, 108)

    def test_map_discovery_uses_original_literal_without_reading_release_global(self):
        self.lua.execute('''local C=...;C.G.release=nil
          setmetatable(C.G,{__index=function(_,key)error('Unexpected global read: '..key)end})''',self.c)
        self.driver.tick()
        self.assertEqual((self.c.discoveries,self.c.creates),(1,1))

    def test_false_create_never_selects_host_or_coop(self):
        self.c.success = False; self.driver.tick(); self.driver.tick()
        self.assertEqual(self.driver.state(), 'failed-native-create')
        self.assertEqual((self.c.creates, self.c.transitions, self.c.coop), (1, 0, 0))

    def test_login_before_discovery_completion_does_not_attempt_native_create(self):
        self.c.browserReady = False
        self.driver.tick(); self.driver.tick()
        self.assertEqual((self.c.discoveries, self.c.loads, self.c.creates), (0, 0, 0))
        self.assertEqual(self.c.phases[1], 'waiting-discovery')
        self.assertEqual(len(self.c.phases), 1)
        self.c.browserReady = True
        self.driver.tick(); self.driver.tick()
        self.assertEqual((self.c.discoveries, self.c.loads, self.c.creates), (1, 1, 1))
        self.assertEqual(self.driver.state(), 'created')

    def test_browser_guard_requires_native_end_event_and_consumed_result(self):
        sys.path.insert(0, str(ROOT / 'tools/gold'))
        from pair_interface import BROWSER_GUARD
        panel, state, kinds = self.lua.table(), self.lua.table(frame=True), self.lua.table(idle=7)
        check = self.lua.execute('local a,l,r=...\n' + BROWSER_GUARD + '\nreturn a.weplayCanAutoCreate',
            panel, state, kinds)
        self.assertFalse(check())
        state.sessionListState = kinds.idle
        self.assertFalse(check())  # Initial idle is insufficient without the native event.
        state.lastDiscoveryFinished = True; state.sessionListState = 8
        self.assertFalse(check())  # Event alone is insufficient while the result is pending.
        state.sessionListState = kinds.idle
        self.assertTrue(check())   # Empty completed lists are valid too.
        state.frame = None
        self.assertFalse(check())

    def test_invalid_or_wrong_loaded_map_never_creates(self):
        for invalid in (True, False):
            self.setUp()
            if invalid: self.c.valid = False
            else: self.c.current = 'different'
            self.driver.tick(); self.assertEqual(self.c.creates, 0)
            self.assertEqual(self.driver.state(), 'failed-map-not-loaded')

    def test_locked_missing_and_ambiguous_maps_stop_before_native_load(self):
        for maps, locked in (([], False), (['a','b'], False), (['a'], True)):
            self.setUp(); self.c.maps=self.lua.table_from(maps); self.c.locked=locked
            self.driver.tick(); self.assertEqual(self.c.loads, 0)
            self.assertIn(self.driver.state(), ('failed-map-missing','failed-map-ambiguous'))

    def test_existing_native_session_and_no_login_cannot_create(self):
        self.c.logged=False; self.driver.tick(); self.assertEqual(self.c.loads, 0)
        self.c.logged=True; self.c.inSession=True; self.driver.tick()
        self.assertEqual(self.c.creates, 0)

    def test_profile_and_browser_actions_are_once_only(self):
        self.c.State.currentPageModule=self.c.Pages.Profile
        self.driver.tick(); self.driver.tick(); self.assertEqual(self.c.transitions, 1)
        self.assertEqual(self.c.action.actionType, 4)
        self.c.State.currentPageModule=self.c.Pages.Main
        self.driver.tick(); self.driver.tick(); self.assertEqual(self.c.transitions, 2)
        self.assertEqual(self.c.action.actionType, 105)

    def prepare_lobby(self, host):
        self.driver.tick(); self.c.inSession=True; self.c.host=host; self.c.options.host=host
        self.c.State.currentPageModule=self.c.Pages.Host if host else self.c.Pages.Guest

    def test_guest_ready_requires_actual_guest_page_role_slot_and_enabled_control(self):
        self.prepare_lobby(False); self.driver.tick(); self.assertEqual(self.c.readies, 0)
        self.c.canReady=True; self.c.slot=1; self.driver.tick(); self.assertEqual(self.c.readies, 0)
        self.c.slot=2; self.c.host=True; self.driver.tick(); self.assertEqual(self.c.readies, 0)
        self.c.host=False; self.c.State.currentPageModule=self.c.Pages.Host
        self.driver.tick(); self.assertEqual(self.c.readies, 0)
        self.c.State.currentPageModule=self.c.Pages.Guest
        self.driver.tick(); self.driver.tick(); self.assertEqual(self.c.readies, 1)
        self.assertEqual(self.c.starts, 0)

    def test_host_start_requires_actual_host_page_role_and_original_ui_guard(self):
        self.prepare_lobby(True); self.driver.tick(); self.assertEqual(self.c.starts, 0)
        self.c.canStart=True; self.c.host=False; self.driver.tick(); self.assertEqual(self.c.starts, 0)
        self.c.host=True; self.driver.tick(); self.driver.tick(); self.assertEqual(self.c.starts, 1)
        self.assertEqual(self.c.readies, 0)

    def test_ready_and_start_callbacks_can_yield_without_double_activation(self):
        for host in (False, True):
            self.setUp(); self.prepare_lobby(host)
            self.c.canStart=self.c.canReady=self.c.yieldStart=self.c.yieldReady=True
            resume=self.lua.eval('function(d)local co=coroutine.create(d.tick);'
                'return function()return coroutine.resume(co)end end')(self.driver)
            self.assertEqual(resume(), (True, 'start' if host else 'ready'))
            self.driver.tick(); self.assertTrue(resume()); self.driver.tick()
            self.assertEqual(self.c.starts if host else self.c.readies, 1)

    def test_deadline_requests_original_shutdown_only_once_even_after_failure(self):
        self.c.success=False; self.driver.tick(); self.c.now=90000
        self.driver.tick(); self.driver.tick(); self.assertEqual(self.c.closes, 1)

    def test_requested_map_uses_original_unique_unlocked_discovery_and_loaded_path(self):
        self.c.options.map_token='grassland'
        self.c.maps=self.lua.table_from(['Map/GrasslandMesa'])
        self.c.current='Map/GrasslandMesa'
        self.c.G.MapSetup.GetMapDataByFilename=self.lua.eval(
            'function(p)return {Name="Grassland Mesa",MapPath=p,Locked=false}end')
        self.driver.tick()
        self.assertEqual((self.c.loads,self.c.creates),(1,1))
        self.assertEqual(self.c.args[1],'Map/GrasslandMesa')

    def test_host_waits_for_observed_ai_configuration_before_original_start_guard(self):
        self.prepare_lobby(True);self.c.canStart=True
        self.lua.execute('''local C=...;C.aiCalls=0;C.aiDone=false
 C.options.configure_ai=function()C.aiCalls=C.aiCalls+1;return C.aiDone end
 C.options.ai_state=function()return 'requested' end''',self.c)
        self.driver.tick();self.assertEqual(self.c.starts,0)
        self.c.aiDone=True;self.driver.tick();self.driver.tick()
        self.assertEqual(self.c.starts,1);self.assertEqual(self.c.aiCalls,2)

    def test_failed_ai_preparation_never_starts_even_if_original_control_is_enabled(self):
        self.prepare_lobby(True);self.c.canStart=True
        self.lua.execute('''local C=...;C.options.configure_ai=function()return false end
 C.options.ai_state=function()return 'failed' end''',self.c)
        self.driver.tick();self.driver.tick()
        self.assertEqual(self.driver.state(),'failed-ai-roster');self.assertEqual(self.c.starts,0)

    def test_ai_preparation_waits_for_original_lobby_human_observation(self):
        self.prepare_lobby(True);self.c.canStart=True;self.c.otherHuman=False
        self.lua.execute('''local C=...;C.aiCalls=0
 C.options.configure_ai=function()C.aiCalls=C.aiCalls+1;return true end
 C.options.ai_state=function()return 'configured' end''',self.c)
        for _ in range(5):self.driver.tick()
        self.assertEqual((self.c.aiCalls,self.c.starts),(0,0))
        self.c.otherHuman=True;self.driver.tick()
        self.assertEqual((self.c.aiCalls,self.c.starts),(1,1))


if __name__ == '__main__': unittest.main()
