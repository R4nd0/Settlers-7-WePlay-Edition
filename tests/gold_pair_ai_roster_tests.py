"""Original AI-chooser call order, native ID handling and fail-closed readback."""
from pathlib import Path
import sys,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'work/lua-test-runtime'))
from lupa.lua51 import LuaRuntime

class AIRosterTests(unittest.TestCase):
    def model(self):
        lua=LuaRuntime()
        c=lua.execute('''
local C={now=0,slots=4,host=true,session=true,localSlot=1,calls={},reports={},
 players={1,2,7,8},states={3,3,1,2},profiles={},difficulty={},peaceful={},reenter=nil,consume=true}
local G={Network={Services={SessionConfig={PlayerSlotState={Open=1,Closed=2,CPU=4,Human=3}}}},MapSetup={},Game={}}
local N,M=G.Network,G.MapSetup
G.Game.GetRunTime=function()return C.now end
N.SessionIsInSession=function()return C.session end
N.IsHost=function()return C.host end
N.GetLocalPlayerSlot=function()return C.localSlot end
N.GetNbPlayerSlots=function()return C.slots end
N.GetPlayerSlotState=function(slot)return C.states[slot] end
M.GetActivePlayers=function(all)assert(all==true);return C.players end
M.IsNeutralPlayer=function(id)return id==24 end
M.IsAIPlayer=function(id)return C.profiles[id]~=nil end
C.names={{profileID='native-ai-a'},{profileID='native-ai-b'}}
M.GetAINamesListSorted=function()return C.names end
M.GetProfileIDByPlayerID=function(id)return C.profiles[id] end
M.GetAIDifficultyLevelByPlayerID=function(id)return C.difficulty[id] end
M.GetAIPeacefulFlagByPlayerID=function(id)return C.peaceful[id] end
local function call(kind,slot,value)C.calls[#C.calls+1]={kind,slot,value} end
M.SetProfileID=function(slot,profile)
 call('profile',slot,profile)
 if C.reenter then assert(C.reenter()==false) end
 if C.consume then C.profiles[C.players[slot]]=profile end
end
M.SetAIDifficultyLevelByPlayerID=function(id,level)call('difficulty',id,level);C.difficulty[id]=level end
M.SetAIPeacefulFlagByPlayerID=function(id,flag)call('peaceful',id,flag);C.peaceful[id]=flag end
N.SetPlayerSlotState=function(slot,state)call('state',slot,state);if C.consume then C.states[slot]=state end end
N.SetAIPlayerProfileID=function(slot,id)call('network-profile',slot,id) end
setmetatable(N,{__index=function()error('Unexpected network action or start')end})
setmetatable(M,{__index=function()error('Unexpected map operation')end})
C.G=G;C.report=function(phase)C.reports[#C.reports+1]=phase end
return C
''')
        opts=lua.table_from({'difficulty':2})
        module=lua.execute((ROOT/'tools/gold/pair_ai_roster.lua').read_text(),c.G,opts,c.report)
        return lua,c,opts,module

    def test_original_setters_use_slots_and_real_player_ids_then_wait_for_readback(self):
        lua,c,opts,m=self.model()
        self.assertFalse(m.tick());self.assertEqual(m.state(),'requested')
        self.assertEqual(len(c.calls),10)
        calls=[tuple(c.calls[i][j] for j in (1,2,3)) for i in range(1,11)]
        self.assertEqual(calls,[('profile',3,'native-ai-a'),('difficulty',7,2),('peaceful',7,False),
            ('state',3,4),('network-profile',3,'native-ai-a'),('profile',4,'native-ai-b'),
            ('difficulty',8,2),('peaceful',8,False),('state',4,4),('network-profile',4,'native-ai-b')])
        self.assertTrue(m.tick());self.assertEqual(m.state(),'configured')
        for _ in range(5):self.assertTrue(m.tick())
        self.assertEqual(len(c.calls),10);self.assertEqual((c.states[1],c.states[2]),(3,3))

    def test_native_reentry_does_not_repeat_or_start_during_configuration(self):
        lua,c,opts,m=self.model();c.reenter=m.tick
        self.assertFalse(m.tick());self.assertEqual(len(c.calls),10)
        self.assertTrue(m.tick());self.assertEqual(len(c.calls),10)

    def test_unconsumed_native_configuration_times_out_without_retry(self):
        lua,c,opts,m=self.model();c.consume=False
        self.assertFalse(m.tick());self.assertFalse(m.tick());self.assertEqual(len(c.calls),10)
        c.now=5000;self.assertFalse(m.tick());self.assertEqual(m.state(),'failed')
        self.assertFalse(m.tick());self.assertEqual(len(c.calls),10)

    def test_requires_host_session_and_original_local_slot(self):
        for field,value in (('host',False),('session',False),('localSlot',2)):
            with self.subTest(field=field):
                lua,c,opts,m=self.model();c[field]=value
                self.assertFalse(m.tick());self.assertEqual(m.state(),'waiting');self.assertEqual(len(c.calls),0)

    def test_wrong_capacity_or_occupied_ai_slot_never_modifies_roster(self):
        for capacity,occupied in ((2,False),(8,False),(4,True)):
            lua,c,opts,m=self.model();c.slots=capacity
            if occupied:c.states[3]=3
            self.assertFalse(m.tick());self.assertEqual(m.state(),'failed');self.assertEqual(len(c.calls),0)

    def test_waits_for_second_human_and_rejects_cpu_as_second_player(self):
        for state,phase in ((1,'waiting'),(2,'waiting'),(4,'failed'),(0,'failed'),(-1,'failed')):
            lua,c,opts,m=self.model();c.states[2]=state
            self.assertFalse(m.tick());self.assertEqual(m.state(),phase);self.assertEqual(len(c.calls),0)

    def test_invalid_or_neutral_player_identity_and_human_ai_profile_are_excluded(self):
        for case in ('duplicate','neutral','foreign-human','fractional'):
            lua,c,opts,m=self.model()
            if case=='duplicate':c.players[4]=c.players[3]
            elif case=='neutral':c.players[4]=24
            elif case=='fractional':c.players[4]=8.5
            else:c.profiles[2]='other-ai'
            self.assertFalse(m.tick());self.assertEqual(m.state(),'failed');self.assertEqual(len(c.calls),0)

    def test_duplicate_profiles_and_invalid_difficulty_are_excluded_before_writes(self):
        for case in ('duplicate-profiles','difficulty'):
            lua,c,opts,m=self.model()
            if case=='duplicate-profiles':c.names[2].profileID='native-ai-a'
            else:opts.difficulty=3
            self.assertFalse(m.tick());self.assertEqual(m.state(),'failed');self.assertEqual(len(c.calls),0)

    def test_pending_readback_cannot_belong_to_a_different_map_player_layout(self):
        lua,c,opts,m=self.model();self.assertFalse(m.tick())
        c.players=lua.table_from([1,2,7,9])
        self.assertFalse(m.tick());self.assertEqual(m.state(),'failed');self.assertEqual(len(c.calls),10)

    def test_original_map_default_ai_slots_are_configurable_without_touching_humans(self):
        lua,c,opts,m=self.model();c.states[3]=c.states[4]=4
        c.profiles[7]='default-ai-a';c.profiles[8]='default-ai-b'
        self.assertFalse(m.tick());self.assertTrue(m.tick())
        self.assertEqual((c.states[1],c.states[2]),(3,3));self.assertEqual(len(c.calls),10)

    def test_native_numeric_profile_catalog_is_used_without_invented_ids(self):
        lua,c,opts,m=self.model()
        c.names=lua.table_from([lua.table_from({'profileID':11}),lua.table_from({'profileID':12})])
        self.assertFalse(m.tick());self.assertTrue(m.tick())
        self.assertEqual((c.profiles[7],c.profiles[8]),(11,12))

if __name__=='__main__':unittest.main()
