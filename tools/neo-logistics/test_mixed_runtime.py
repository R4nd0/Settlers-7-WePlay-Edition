"""Lua 5.1 mixed namespace and lifecycle guards; no native/game evidence."""
from pathlib import Path
import sys
import unittest

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parents[1]/'work/lua-test-runtime'))
from lupa.lua51 import LuaRuntime
from test_runtime import BACKEND

MIXED=r'''
C.modeReads=0;C.legacyReads=0;C.selection='1';C.networkChoice='1'
C.capability='NEO-FIFO-INTENT-1';C.stageAck='1';C.startAck='1';C.beginAck='1';C.readyAck='1'
C.trace={};C.begins=0;C.readies=0;C.stageCalls=0;C.startEvents=0;C.worldAvailable=true
function G.UI.WriteToConsole(command)
 C.commands[#C.commands+1]=command
 if command=='weplay-crossplay-v1:mode' then C.modeReads=C.modeReads+1;return C.mode end
 if command:sub(1,#'weplay-neo-v1:')=='weplay-neo-v1:' then
  C.legacyReads=C.legacyReads+1;error('Mixed mode used legacy NEO')
 end
 assert(command:sub(1,#'weplay-crossplay-neo-v1:')=='weplay-crossplay-neo-v1:','Lobby query after mode detection')
 local cmd=command:sub(#'weplay-crossplay-neo-v1:'+1)
 C.trace[#C.trace+1]=cmd
 if cmd=='capabilities' then return C.capability end
 if cmd=='selection' then return C.selection end
 if cmd=='network-state' then return C.networkChoice end
 if cmd:sub(1,12)=='select-file:' then C.fileSelections=(C.fileSelections or 0)+1;return C.fileAck or false end
 if cmd=='load' then return C.savedWorld and C.loadAck or false end
 if cmd=='restore-network' then
  if not C.savedWorld or not C.nativeRestored or C.started==0 then return false end
  if C.restoreAck=='1' then C.readyAccepted=true end
  return C.restoreAck
 end
 if cmd=='status' then return C.readyAccepted and C.selection or false end
 if cmd=='stage:0' or cmd=='stage:1' then
  C.stageCalls=C.stageCalls+1
  if C.mode~='host' then return false end
  if C.stageAck=='1' then C.selection=cmd:sub(-1) end
  return C.stageAck
 end
 if cmd=='start-event' then
  C.startEvents=C.startEvents+1
  if C.startAck=='1' then C.startFact=true end
  return C.startAck
 end
 if cmd=='begin:0' or cmd=='begin:1' then
  C.begins=C.begins+1
  if not C.startFact or cmd:sub(-1)~=C.selection then return false end
  if C.beginAck=='1' then C.begun=true end
  return C.beginAck
 end
 if cmd:sub(1,14)=='save-snapshot:' then
  C.snapshots=(C.snapshots or 0)+1
  if (C.mode~='host' and C.mode~='room-host') or not C.readyAccepted or not C.worldAvailable then return false end
  if C.snapshot~=nil then return C.snapshot end
  return C.selection=='1' and 'NEO2\n11\n10\n0\n' or 'NEO0\n'
 end
 if cmd=='ready' then
  C.readies=C.readies+1
  if not C.begun or not C.worldAvailable or C.started==0 then return false end
  if C.readyAck=='1' then C.readyAccepted=true end
  return C.readyAck
 end
 error('Unsupported mixed command reached native bridge: '..cmd)
end
P.startNetwork=function() C.trace[#C.trace+1]='original-start';C.started=C.started+1;P.started=true;if C.nestedLoad then P.loadSavegame() end end
_G.pcall=function()error('Protected engine calls forbidden')end
'''


class MixedRuntimeTests(unittest.TestCase):
    def build(self,mode='guest'):
        self.lua=LuaRuntime(unpack_returned_tuples=True)
        self.assertEqual(self.lua.eval('_VERSION'),'Lua 5.1')
        self.lua.execute(BACKEND+MIXED)
        self.c=self.lua.globals().C;self.c.mode=mode;self.c.network=True
        self.lua.execute('M=(function(...)\n'+(HERE/'runtime.lua').read_text('utf-8')+'\nend)(G);M.hookGame(P,J)')
        self.m=self.lua.globals().M;self.p=self.lua.globals().P

    def setUp(self):self.build()

    def test_explicit_mixed_roles_use_only_the_new_capability_and_preserve_state_strings(self):
        for mode in ('host','guest','room-host','room-guest'):
            with self.subTest(mode=mode):
                self.build(mode);self.assertTrue(self.m.available())
                for state in ('0','1'):
                    self.c.networkChoice=state
                    self.assertEqual(self.m.networkState(),state);self.assertTrue(self.m.networkReady())
                for state in (False,True,0,1,'','true',None):
                    self.c.networkChoice=state;self.assertFalse(self.m.networkReady())
                self.c.capability='NEO1';self.assertFalse(self.m.available())
                self.assertEqual(self.c.modeReads,1);self.assertEqual(self.c.legacyReads,0)

    def test_only_string_one_acknowledges_host_stage_and_guest_cannot_stage(self):
        for ack in (True,False,1,0,'0','true','',None):
            with self.subTest(ack=ack):
                self.build('host');self.c.stageAck=ack;self.assertFalse(self.m.select(True))
        self.build('host');self.assertTrue(self.m.select(True));self.assertEqual(self.c.selection,'1')
        self.build('guest');self.assertFalse(self.m.select(False));self.assertEqual(self.c.selection,'1')

    def test_native_start_event_begin_and_ready_are_required_in_original_order_for_on_and_off(self):
        for choice in ('0','1'):
            with self.subTest(choice=choice):
                self.build();self.c.selection=choice;self.c.networkChoice=choice
                self.assertTrue(self.m.acceptNetworkStart());self.p.startNetwork()
                self.assertEqual(self.c.started,1);self.assertTrue(self.c.readyAccepted)
                self.assertEqual(self.m.active(),choice=='1')
                trace=list(self.c.trace.values())
                self.assertLess(trace.index('start-event'),trace.index('begin:'+choice))
                self.assertLess(trace.index('begin:'+choice),trace.index('original-start'))
                self.assertLess(trace.index('original-start'),trace.index('ready'))
                self.assertEqual(self.c.modeReads,1);self.assertEqual(self.c.legacyReads,0)

    def test_noncanonical_action_results_never_become_native_acknowledgements(self):
        for ack in (True,False,1,'0','true',None):
            with self.subTest(action='start',ack=ack):
                self.build();self.c.startAck=ack;self.assertFalse(self.m.acceptNetworkStart())
                self.assertFalse(bool(self.c.startFact))
            with self.subTest(action='begin',ack=ack):
                self.build();self.assertTrue(self.m.acceptNetworkStart());self.c.beginAck=ack
                self.assertFalse(self.m.beforeStart());self.assertFalse(self.m.active())
            with self.subTest(action='ready',ack=ack):
                self.build();self.assertTrue(self.m.acceptNetworkStart());self.c.readyAck=ack
                with self.assertRaises(Exception):self.p.startNetwork()
                self.assertFalse(bool(self.c.readyAccepted))

    def test_missing_start_fact_capability_selection_or_world_never_falls_back_to_solo(self):
        with self.assertRaises(Exception):self.p.startNetwork()
        self.assertEqual(self.c.started,0)
        for selection in (False,True,0,1,'','true',None):
            with self.subTest(selection=selection):
                self.build();self.c.selection=selection;self.assertFalse(self.m.beforeStart())
                self.assertEqual(self.c.begins,0)
        self.build();self.c.capability='NEO1';self.assertFalse(self.m.beforeStart())
        self.build();self.c.network=False;self.c.selection='0';self.c.beginAck=False
        with self.assertRaises(Exception):self.p.startSingleplayer()
        self.assertEqual(self.c.started,0)
        self.build();self.assertTrue(self.m.acceptNetworkStart());self.c.worldAvailable=False
        with self.assertRaises(Exception):self.p.startNetwork()
        self.assertFalse(bool(self.c.readyAccepted))

    def test_unsupported_saved_lifecycle_stops_before_original_io_even_when_neo_is_off(self):
        for choice in ('0','1'):
            with self.subTest(choice=choice):
                self.build();self.c.selection=choice;self.c.networkChoice=choice
                self.assertFalse(self.m.selectSaved(1000));self.assertFalse(self.m.beforeLoad())
                self.assertFalse(self.m.restore(self.lua.globals().J));self.assertFalse(self.m.save(self.lua.globals().J))
                self.assertTrue(self.m.acceptNetworkStart())
                with self.assertRaises(Exception):self.p.startNetwork(1000)
                with self.assertRaises(Exception):self.p.loadSavegame()
                with self.assertRaises(Exception):self.p.pickle()
                self.assertEqual(self.c.started,0);self.assertEqual(self.c.loaded,0);self.assertEqual(self.c.pickled,0)
                self.assertFalse(self.m.finished())
                self.lua.execute('P.started=true');self.p.destroy();self.assertEqual(self.c.destroyed,1)
                self.assertEqual(self.c.legacyReads,0)

    def test_saved_metadata_is_not_written_or_reported_as_completed(self):
        self.lua.execute('''
C.savedCalls=0;C.stores=0
S={weplayMPResumeSlots={}}
R={saved=function()C.savedCalls=C.savedCalls+1;return true end,lookup=function()error('Mixed saved lookup forbidden')end}
''')
        self.lua.execute('N=(function(...)\n'+(HERE/'runtime.lua').read_text('utf-8')+'\nend)(G,S,function()C.stores=C.stores+1 end,R)')
        self.assertFalse(self.lua.globals().N.selectSaved(1000))
        self.assertFalse(self.lua.globals().R.saved(1000,'save'))
        self.assertEqual(self.c.savedCalls,0);self.assertEqual(self.c.stores,0)

    def test_saved_start_requires_native_load_and_actual_restored_ledger_in_original_order(self):
        for mode in ('host','guest'):
            self.build(mode);self.c.savedWorld=True;self.c.nativeRestored=True
            self.c.loadAck='1';self.c.restoreAck='1'
            self.assertTrue(self.m.acceptNetworkStart());self.p.startNetwork(1000)
            self.assertEqual(self.c.started,1);self.assertTrue(self.m.active())
            self.assertEqual(self.c.begins,0);self.assertEqual(self.c.readies,0)
            trace=list(self.c.trace.values())
            self.assertLess(trace.index('load'),trace.index('original-start'))
            self.assertLess(trace.index('original-start'),trace.index('restore-network'))
            self.assertEqual(self.c.legacyReads,0)

    def test_saved_ui_selection_is_host_only_and_requires_exact_native_file_ack(self):
        for ack in (False,True,0,1,'0','true','',None,'1'):
            self.build('host');self.c.fileAck=ack
            self.lua.execute("G.Game.GetSaveGameFileName=function(slot)assert(slot==1000);return 'Z:/Owned/Profiles/Savegames/s1000.s7s' end")
            self.assertEqual(self.m.selectSaved(1000),ack=='1')
            self.assertEqual(self.c.started,0);self.assertEqual(self.c.begins,0)
        self.build('guest');self.c.fileAck='1'
        self.lua.execute("G.Game.GetSaveGameFileName=function()error('Guest must not select a host file')end")
        self.assertFalse(self.m.selectSaved(1000));self.assertIsNone(self.c.fileSelections)
        for slot in (-1,1.5,4294967295,float('nan'),float('inf'),'1000',True):
            self.build('host');self.c.fileAck='1'
            self.assertFalse(self.m.selectSaved(slot));self.assertIsNone(self.c.fileSelections)

    def test_room_host_can_export_live_save_metadata(self):
        for role in ('room-host','room-guest'):
            self.build(role)
            self.assertTrue(self.m.acceptNetworkStart())
            self.p.startNetwork()
            self.assertEqual(self.m.save(self.lua.globals().J),role=='room-host')
            self.assertEqual(self.c.snapshots,1 if role=='room-host' else None)

    def test_saved_start_rejects_noncanonical_native_load_or_restore_ack(self):
        for ack in (False,True,0,1,'0','true','',None):
            self.build();self.c.savedWorld=True;self.c.nativeRestored=True
            self.c.loadAck=ack;self.c.restoreAck='1'
            with self.assertRaises(Exception):self.p.startNetwork(1000)
            self.assertEqual(self.c.started,0)
            self.build();self.c.savedWorld=True;self.c.nativeRestored=True
            self.c.loadAck='1';self.c.restoreAck=ack
            with self.assertRaises(Exception):self.p.startNetwork(1000)
            self.assertFalse(self.m.active());self.assertFalse(bool(self.c.readyAccepted))
        self.build();self.c.savedWorld=True;self.c.nativeRestored=False;self.c.loadAck='1';self.c.restoreAck='1'
        with self.assertRaises(Exception):self.p.startNetwork(1000)
        self.assertFalse(self.m.active());self.assertEqual(self.c.begins,0)

    def test_nested_original_saved_load_observes_same_native_restore_without_new_begin(self):
        self.c.savedWorld=True;self.c.nativeRestored=True;self.c.nestedLoad=True
        self.c.loadAck='1';self.c.restoreAck='1'
        self.assertTrue(self.m.acceptNetworkStart());self.p.startNetwork(1000)
        self.assertEqual(self.c.started,1);self.assertEqual(self.c.loaded,1)
        self.assertTrue(self.m.active());self.assertEqual(self.c.begins,0)
        self.assertTrue(self.m.restore(self.lua.globals().J))
        self.c.readyAccepted=False
        self.assertFalse(self.m.restore(self.lua.globals().J))

    def test_only_ready_host_writes_exact_current_native_snapshot_before_original_pickle(self):
        for choice in ('0','1'):
            with self.subTest(choice=choice):
                self.build('host');self.c.selection=choice;self.c.networkChoice=choice
                with self.assertRaises(Exception):self.p.pickle()
                self.assertEqual(self.c.pickled,0)
                self.assertTrue(self.m.acceptNetworkStart());self.p.startNetwork()
                self.p.pickle();self.assertEqual(self.c.pickled,1)
                saved=self.c.cache['weplay_neo_logistics_v1']
                self.assertEqual(saved.version,1)
                self.assertEqual(saved.payload,'NEO2\n11\n10\n0\n' if choice=='1' else 'NEO0\n')
                self.assertEqual(self.c.snapshots,2);self.assertEqual(self.c.legacyReads,0)
                self.assertFalse(self.m.beforeLoad());self.assertFalse(self.m.selectSaved(1000))

    def test_rejected_or_stale_host_snapshot_blocks_original_pickle(self):
        for payload in (False,True,1,'','NEO1\n10\n0\n','NEO2\n10\n9\n0\n','NEO0\n','NEO2\n11\n10\n0\n\0','x'*2097153):
            with self.subTest(payload_type=type(payload).__name__,size=len(payload) if isinstance(payload,str) else None):
                self.build('host');self.assertTrue(self.m.acceptNetworkStart());self.p.startNetwork()
                self.c.snapshot=payload
                with self.assertRaises(Exception):self.p.pickle()
                self.assertEqual(self.c.pickled,0);self.assertIsNone(self.c.cache['weplay_neo_logistics_v1'])
        self.build('host');self.assertTrue(self.m.acceptNetworkStart());self.p.startNetwork();self.c.worldAvailable=False
        with self.assertRaises(Exception):self.p.pickle()
        self.assertEqual(self.c.pickled,0)

    def test_guest_never_exports_or_writes_a_save_snapshot_even_after_world_ready(self):
        self.assertTrue(self.m.acceptNetworkStart());self.p.startNetwork()
        with self.assertRaises(Exception):self.p.pickle()
        self.assertEqual(self.c.pickled,0);self.assertIsNone(self.c.snapshots)

    def test_invalid_native_ui_turn_is_rejected_before_snapshot_or_pickle(self):
        for turn in (-1,1.5,4294967295,float('nan'),float('inf'),'10',False):
            with self.subTest(turn=turn):
                self.build('host');self.assertTrue(self.m.acceptNetworkStart());self.p.startNetwork()
                self.lua.globals().G.Game.GetCurrentTurn=lambda:turn
                with self.assertRaises(Exception):self.p.pickle()
                self.assertEqual(self.c.pickled,0);self.assertIsNone(self.c.snapshots)

    def test_mode_is_cached_and_queries_and_actions_remain_directly_yieldable(self):
        self.lua.execute('''
C.mode='solo'
assert(M.networkState()=='1' and C.modeReads==1 and C.legacyReads==0)
local old=G.UI.WriteToConsole
G.UI.WriteToConsole=function(command)
 if command=='weplay-crossplay-neo-v1:network-state' then coroutine.yield('state')end
 if command=='weplay-crossplay-neo-v1:start-event' then coroutine.yield('event')end
 return old(command)
end
local job=coroutine.create(M.acceptNetworkStart)
local okay,value=coroutine.resume(job);assert(okay and value=='state')
okay,value=coroutine.resume(job);assert(okay and value=='event')
okay,value=coroutine.resume(job);assert(okay and value==true and coroutine.status(job)=='dead')
assert(C.modeReads==1 and C.legacyReads==0)
''')

    def test_solo_unknown_or_older_mode_results_keep_the_existing_legacy_protocol(self):
        for mode in ('solo','unknown',False,None):
            with self.subTest(mode=mode):
                lua=LuaRuntime(unpack_returned_tuples=True);lua.execute(BACKEND)
                lua.globals().C.mode=mode
                lua.execute("local old=G.UI.WriteToConsole;G.UI.WriteToConsole=function(command) if command=='weplay-crossplay-v1:mode' then return C.mode end;return old(command)end")
                lua.execute('M=(function(...)\n'+(HERE/'runtime.lua').read_text('utf-8')+'\nend)(G);M.hookGame(P,J)')
                self.assertTrue(lua.globals().M.available());self.assertTrue(lua.globals().M.select(True))
                lua.globals().P.startSingleplayer();self.assertTrue(lua.globals().M.active())
                self.assertFalse(any(command.startswith('weplay-crossplay-neo-v1:')
                                     for command in lua.globals().C.commands.values()))


if __name__=='__main__':unittest.main()
