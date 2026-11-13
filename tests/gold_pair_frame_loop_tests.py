"""Keep native pair jobs reachable when original menu callbacks change."""
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'work/lua-test-runtime'))
from lupa.lua51 import LuaRuntime


class FrameLoopTests(unittest.TestCase):
    def setUp(self):
        self.lua=LuaRuntime(unpack_returned_tuples=True)
        self.c=self.lua.execute('''local C={frames=0,menu=0,world=0,ticks=0,checks=0,closed=false}
        C.frame=function()C.menu=C.menu+1 end
        C.original=function()C.frames=C.frames+1;C.frame()end
        C.driver=function()C.ticks=C.ticks+1;if C.yieldDriver then coroutine.yield('native-job')end end
        C.watch={tick=function()C.checks=C.checks+1 end,closing=function()return C.closed end}
        return C''')
        self.loop=self.lua.execute((ROOT/'tools/gold/pair_frame_loop.lua').read_text(),self.c.original,self.c.watch)

    def test_driver_continues_when_the_menu_frame_function_is_replaced(self):
        self.loop.register(self.c.driver);self.loop.update()
        self.c.frame=self.lua.eval('function(c)return function()c.world=c.world+1 end end')(self.c)
        self.loop.update();self.loop.update()
        self.assertEqual((self.c.menu,self.c.world,self.c.ticks,self.c.checks),(1,2,3,3))

    def test_registration_during_original_update_runs_after_that_update(self):
        self.c.frame=self.lua.eval('function(loop,c)return function()loop.register(c.driver)end end')(self.loop,self.c)
        self.loop.update();self.assertEqual(self.c.ticks,1)

    def test_registered_native_job_can_yield(self):
        self.c.yieldDriver=True;self.loop.register(self.c.driver)
        resume=self.lua.eval('function(loop)local co=coroutine.create(loop.update);'
            'return function()return coroutine.resume(co)end end')(self.loop)
        self.assertEqual(resume(),(True,'native-job'));self.assertTrue(resume())
        self.assertEqual((self.c.frames,self.c.ticks),(1,1))

    def test_original_frame_can_yield_before_the_driver_runs(self):
        self.c.frame=self.lua.eval('function()coroutine.yield("original-job")end')
        self.loop.register(self.c.driver)
        resume=self.lua.eval('function(loop)local co=coroutine.create(loop.update);'
            'return function()return coroutine.resume(co)end end')(self.loop)
        self.assertEqual(resume(),(True,'original-job'));self.assertEqual(self.c.ticks,0)
        self.assertTrue(resume());self.assertEqual(self.c.ticks,1)

    def test_deadline_stops_new_native_work_after_requesting_normal_exit(self):
        self.loop.register(self.c.driver);self.c.closed=True
        self.loop.update();self.assertEqual((self.c.checks,self.c.frames,self.c.ticks),(1,0,0))

    def test_replacing_driver_does_not_keep_the_previous_menu_callback(self):
        self.loop.register(self.c.driver);self.loop.update()
        self.loop.register(self.lua.eval('function(c)return function()c.world=c.world+1 end end')(self.c))
        self.loop.update();self.assertEqual((self.c.ticks,self.c.world),(1,1))


if __name__=='__main__':unittest.main()
