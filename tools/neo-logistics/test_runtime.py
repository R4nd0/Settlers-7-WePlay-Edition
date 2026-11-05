"""Owned Lua state/lifecycle and UI checks; not native save/resume evidence."""
from pathlib import Path
import sys
import unittest
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parents[1]/'work/lua-test-runtime'))
from lupa.lua51 import LuaRuntime

BACKEND=r'''
C={network=false,supported=true,configured=false,started=0,loaded=0,pickled=0,destroyed=0,cache={},commands={},messages=0}
G={Network={InNetworkMode=function() return C.network end},UI={},ui={messageBox=function() C.messages=C.messages+1 end}}
function G.UI.WriteToConsole(command)
 C.commands[#C.commands+1]=command
 local cmd=command:sub(15)
 if not C.supported then return 0 end
 if cmd=='capabilities' then return 'NEO1' end
 if cmd=='selection' then return C.configured and '1' or '0' end
 if cmd=='network-state' then return C.networkChoice end
 if cmd=='stage:1' then C.configured=true;return true end
 if cmd=='stage:0' then C.configured=false;return true end
 if cmd=='begin:1' then C.configured=true;return true end
 if cmd=='begin:0' or cmd=='load' or cmd=='legacy' or cmd=='end' then C.configured=false;return true end
 if cmd=='status' then return C.configured and '1' or '0' end
 if cmd=='snapshot' then return C.configured and 'NEO1\n10\n0\n' or 'NEO0\n' end
 if cmd:sub(1,8)=='restore:' then if C.reject then return false end; C.configured=cmd:sub(9,12)=='NEO1';return true end
 if cmd=='ready' then return not C.reject end
 return false
end
J={put=function(k,v) C.cache[k]=v end,get=function(k) local v=C.cache[k];C.cache[k]=nil;return v end,isPickled=function(k) return C.cache[k]~=nil end}
P={multiplayerSavegameSlot=1000,startNetwork=function() C.started=C.started+1 end,
startSingleplayer=function() C.started=C.started+1;P.started=true end,loadSavegame=function() C.loaded=C.loaded+1;P.started=true;return true end,
pickle=function() C.pickled=C.pickled+1 end,destroy=function() C.destroyed=C.destroyed+1;P.started=false end}
'''

class RuntimeTests(unittest.TestCase):
    def setUp(self):
        self.lua=LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute(BACKEND)
        self.lua.execute('M=(function(...)\n'+(HERE/'runtime.lua').read_text()+'\nend)(G);M.hookGame(P,J)')
        self.c=self.lua.globals().C;self.m=self.lua.globals().M;self.p=self.lua.globals().P
    def test_default_off_and_per_map_reset(self):
        self.assertFalse(self.m.setup('a'));self.m.select(True);self.assertTrue(self.m.setup('a'));self.assertFalse(self.m.setup('b'))
        self.p.startSingleplayer();self.assertFalse(self.m.active());self.assertEqual(self.c.started,1)
    def test_enabled_start_keeps_individual_state_in_native_payload(self):
        self.m.setup('a');self.m.select(True);self.p.startSingleplayer();self.assertTrue(self.m.active())
        self.p.pickle();self.assertEqual(self.c.pickled,1)
        saved=self.c.cache['weplay_neo_logistics_v1'];self.assertEqual(saved.version,1);self.assertTrue(saved.payload.startswith('NEO1'))
        self.p.destroy();self.assertFalse(self.m.active());self.p.loadSavegame();self.assertTrue(self.m.active())
    def test_legacy_save_cannot_inherit_enabled_selection(self):
        self.m.setup('a');self.m.select(True);self.p.loadSavegame();self.assertFalse(self.m.active())
    def test_pre_start_cleanup_does_not_discard_selected_map_option(self):
        self.m.setup('a');self.m.select(True);self.p.destroy();self.p.startSingleplayer();self.assertTrue(self.m.active())
    def test_map_start_lua_reload_keeps_native_choice(self):
        self.m.setup('a');self.m.select(True)
        self.lua.execute('N=(function(...)\n'+(HERE/'runtime.lua').read_text()+'\nend)(G)')
        self.assertTrue(self.lua.globals().N.beforeStart());self.assertTrue(self.lua.globals().N.active())
    def test_rejected_priority_restore_prevents_claiming_enabled(self):
        self.lua.execute("C.cache.weplay_neo_logistics_v1={version=1,payload='NEO1\\n10\\n0\\n'};C.reject=true")
        with self.assertRaises(Exception):self.p.loadSavegame()
        self.assertFalse(self.m.active());self.assertEqual(self.c.messages,1)
    def test_unknown_adapter_does_not_offer_false_capability(self):
        self.c.supported=False;self.assertFalse(self.m.available());self.assertFalse(self.m.select(True))
        self.p.startSingleplayer();self.assertEqual(self.c.started,1)
    def test_multiplayer_requires_received_configuration(self):
        self.c.network=True
        self.assertTrue(self.m.available());self.assertFalse(self.m.networkReady())
        with self.assertRaises(Exception):self.p.startNetwork()
        self.assertEqual(self.c.started,0)
        self.c.networkChoice='1';self.m.select(True);self.p.startNetwork()
        self.assertTrue(self.m.active());self.assertEqual(self.c.started,1)
    def test_multiplayer_resume_refuses_missing_enabled_state(self):
        self.c.network=True;self.c.networkChoice='1'
        with self.assertRaises(Exception):self.p.startNetwork(1000)
        self.assertFalse(self.m.active());self.assertEqual(self.c.messages,1)
    def test_multiplayer_save_choice_uses_existing_validated_slot_metadata(self):
        self.lua.execute('''
        S={weplayMPResumeSlots={}}
        R={saved=function(slot) S.weplayMPResumeSlots[tostring(slot)]={date='bound',map='bound'};return true end,
           lookup=function(slot) return S.weplayMPResumeSlots[tostring(slot)] end}
        C.stores=0
        ''')
        self.lua.execute('Q=(function(...)\n'+(HERE/'runtime.lua').read_text()+'\nend)(G,S,function() C.stores=C.stores+1 end,R)')
        q=self.lua.globals().Q
        q.setup('a');q.select(True);q.beforeStart();self.lua.execute("R.saved(1002,'save')")
        self.assertTrue(self.lua.globals().S.weplayMPResumeSlots['1002'].neoLogistics)
        self.assertTrue(q.selectSaved(1002));self.assertEqual(self.c.stores,1)
        self.assertFalse(q.selectSaved(1003))
        self.lua.execute("S.weplayMPResumeSlots['1002'].neoLogistics='wrong'")
        self.assertFalse(q.selectSaved(1002))
    def test_supported_adapter_rejects_failed_off_configuration_before_start(self):
        self.lua.execute("local old=G.UI.WriteToConsole;G.UI.WriteToConsole=function(c) if c=='weplay-neo-v1:begin:0' then return false end;return old(c) end")
        with self.assertRaises(Exception):self.p.startSingleplayer()
        self.assertEqual(self.c.started,0)
    def test_group_drag_disabled_without_replacing_task_callbacks(self):
        self.lua.execute('''
        G.require=function() return {weplayNeoLogistics=M} end
        C.disabled={};C.text={};C.taskCalls=0
        G.ui.enable=function(id,v) C.disabled[id]=not v end;G.ui.setAlpha=function() end
        G.label={create=function() end,setContent=function(id,text) C.text[id]=text[1] end}
        G.font={small=1};G.UI.Anchor={Top=1,Left=2}
        G.logisticsSystem={create=function() end,update=function() end,isCreated=function() return true end,
          startPanelDragging=function() C.drag=true end,stopPanelDragging=function() C.stop=true end,
          frame=1,panels={{panelButton=2,icon=3,rankingLabel=4,taskButton=5}}}
        C.task=function() C.taskCalls=C.taskCalls+1 end
        ''')
        self.lua.execute('(function(...)\n'+(HERE/'logistics_ui.lua').read_text()+'\nend)(G)')
        self.m.select(True);self.p.startSingleplayer();self.lua.execute('G.logisticsSystem.create();G.logisticsSystem.startPanelDragging();C.task()')
        self.assertTrue(self.c.disabled[2]);self.assertIsNone(self.c.disabled[5]);self.assertEqual(self.c.text[4],'-');self.assertIsNone(self.c.drag);self.assertEqual(self.c.taskCalls,1)

if __name__=='__main__':unittest.main()
