"""Native pacing requests stay behind real world entry and preserve failures."""
from pathlib import Path
import sys,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'work/lua-test-runtime'))
from lupa.lua51 import LuaRuntime,LuaError

class PacingTests(unittest.TestCase):
    def model(self):
        lua=LuaRuntime()
        state=lua.execute('''
local C={duration=100,calls=0,reports=0,inMap=false,turn=0,allow=true,now=0,defer=false}
local G={game={started=false},Game={}}
G.Game.InMapMode=function()return C.inMap end
G.Game.GetCurrentTurn=function()return C.turn end
G.Game.GetGameTurnDuration=function()return C.duration end
G.Game.GetRunTime=function()return C.now end
G.Game.SetGameTurnDuration=function(value)
 assert(value==5);C.calls=C.calls+1;if C.allow and not C.defer then C.duration=value end
end
setmetatable(G.Game,{__index=function()error('Unexpected engine operation')end})
C.G=G;C.report=function(row)C.reports=C.reports+1;C.row=row end
return C
''')
        tick=lua.execute((ROOT/'tools/gold/pair_pacing.lua').read_text(),state.G,state.report)
        return state,tick

    def test_requires_started_world_and_positive_original_turn(self):
        c,tick=self.model()
        for started,in_map,turn in ((False,False,0),(True,False,4),(False,True,4),(True,True,0)):
            c.G.game.started=started;c.inMap=in_map;c.turn=turn;tick()
        self.assertEqual((c.calls,c.reports,c.duration),(0,0,100))

    def test_original_setter_once_and_observed_readback(self):
        c,tick=self.model();c.G.game.started=True;c.inMap=True;c.turn=1
        for _ in range(10):tick()
        self.assertEqual((c.calls,c.reports,c.duration,c.turn),(1,1,5,1))
        self.assertTrue(c.row.accepted);self.assertEqual(c.row.before_ms,100)
        self.assertEqual(c.row.actual_ms,5);self.assertEqual(c.row.requested_speed,20)

    def test_already_accelerated_world_does_not_repeat_setter(self):
        c,tick=self.model();c.G.game.started=True;c.inMap=True;c.turn=5;c.duration=5
        tick();self.assertEqual((c.calls,c.reports),(0,1));self.assertTrue(c.row.accepted)

    def test_rejected_original_setter_is_reported_without_retry_or_ui_abort(self):
        c,tick=self.model();c.G.game.started=True;c.inMap=True;c.turn=1;c.allow=False
        tick();self.assertEqual((c.calls,c.reports),(1,0))
        c.now=3000;tick()
        self.assertEqual((c.calls,c.duration),(1,100));self.assertFalse(c.row.accepted)
        tick();self.assertEqual(c.calls,1)

    def test_pending_native_duration_can_apply_on_later_update(self):
        c,tick=self.model();c.G.game.started=True;c.inMap=True;c.turn=1;c.defer=True
        tick();self.assertEqual((c.calls,c.reports),(1,0))
        c.now=500;c.duration=5;tick()
        self.assertEqual((c.calls,c.reports),(1,1));self.assertTrue(c.row.accepted)

    def test_unrecognized_baseline_is_not_overwritten(self):
        c,tick=self.model();c.G.game.started=True;c.inMap=True;c.turn=1;c.duration=75
        with self.assertRaisesRegex(LuaError,'Unexpected original'):tick()
        self.assertEqual((c.calls,c.reports,c.duration),(0,0,75))

if __name__=='__main__':unittest.main()
