"""Engine-counter observation and normal exit survive menu/world transitions."""
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'work/lua-test-runtime'))
from lupa.lua51 import LuaRuntime


class WatchdogTests(unittest.TestCase):
    def setUp(self):
        self.lua=LuaRuntime(unpack_returned_tuples=True)
        self.c=self.lua.execute('''local C={now=1000,turn=0,gameTime=0,inMap=false,exits=0,rows={}}
        C.G={Game={GetRunTime=function()return C.now end,GetCurrentTurn=function()return C.turn end,
         GetGameTime=function()return C.gameTime end,InMapMode=function()return C.inMap end},
         system={exitApi=function()C.exits=C.exits+1 end}}
        C.options={duration=15000};C.report=function(row)C.rows[#C.rows+1]=row end;return C''')
        c=self.c
        self.watch=self.lua.execute((ROOT/'tools/gold/pair_watchdog.lua').read_text(),c.G,c.options,c.report)

    def test_records_only_actual_counter_values_and_observes_world_transition(self):
        self.watch.tick();self.c.now=2000;self.c.inMap=True
        self.c.turn=42;self.c.gameTime=1234;self.watch.tick()
        self.assertEqual((self.c.rows[1].turn,self.c.rows[2].turn),(None,42))
        self.assertEqual(self.c.rows[2].game_time_ms,1234)
        self.assertTrue(self.c.rows[2].in_map)
        self.assertEqual((self.c.turn,self.c.gameTime),(42,1234))

    def test_frame_frequency_does_not_create_extra_samples(self):
        self.watch.tick();self.c.now=1999
        for _ in range(10):self.watch.tick()
        self.assertEqual(len(self.c.rows),1)
        self.c.now=2000;self.watch.tick();self.assertEqual(len(self.c.rows),2)

    def test_application_exit_occurs_at_deadline_only_once(self):
        self.watch.tick();self.c.now=15999;self.watch.tick()
        self.assertEqual(self.c.exits,0)
        self.c.now=16000;self.watch.tick();self.watch.tick()
        self.assertEqual(self.c.exits,1)
        self.c.now=20000;self.watch.tick();self.assertEqual(self.c.exits,1)

    def test_observes_actual_loading_visibility_without_closing_it(self):
        self.lua.execute('''local C=...;C.visible=true;C.reads=0
        C.G.loadingScreen={isCreated=function()C.reads=C.reads+1;return C.visible end}''',self.c)
        self.watch.tick()
        self.assertTrue(self.c.rows[1].loading_screen_visible)
        self.assertTrue(self.c.visible)
        self.c.visible=False;self.c.now=2000;self.watch.tick()
        self.assertIs(self.c.rows[2].loading_screen_visible,False)
        self.assertEqual(self.c.reads,2)
        self.assertEqual(self.c.exits,0)

    def test_missing_or_unknown_visibility_is_not_reported_as_closed(self):
        self.lua.execute('''local C=...;setmetatable(C.G,{__index=function()error('Unknown global read')end})''',self.c)
        self.watch.tick()
        self.assertIsNone(self.c.rows[1].loading_screen_visible)
        self.lua.execute('local C=...;C.G.loadingScreen={isCreated=function()return 1 end}',self.c)
        self.c.now=2000;self.watch.tick()
        self.assertIsNone(self.c.rows[2].loading_screen_visible)


if __name__=='__main__':unittest.main()
