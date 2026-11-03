"""Exercise real lobby hook against a cached-UI/native-ready boundary."""
from pathlib import Path
import sys
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / 'work/lua-test-runtime'))
try:
    from lupa.lua51 import LuaRuntime
except ImportError:
    LuaRuntime = None

BACKEND = r'''
local C={now=0,session=true,host=true,map=true,ready=false,roster=true,enabled=false,
    updates=0,refreshes=0,destroys=0}
C.State={startButton={},allHumansAreReady=false}
C.Panel={update=function() C.updates=C.updates+1;C.enabled=C.State.allHumansAreReady end,
    destroy=function() C.destroys=C.destroys+1 end,
    updatePlayerSlots=function() C.refreshes=C.refreshes+1;C.State.allHumansAreReady=C.ready end}
C.G={Game={GetRunTime=function() return C.now end},
    require=function() return {weplayMPResume={ready=function() return C.roster end}} end,
    ui={enable=function(button,enabled) assert(button==C.State.startButton);C.enabled=enabled end},
    Network={SessionIsInSession=function() return C.session end,IsHost=function() return C.host end,
        GetLocalPlayerSlot=function() return 1 end},MapSetup={IsValid=function() return C.map end}}
return C
'''


@unittest.skipIf(LuaRuntime is None, 'Lua5.1 runtime required')
class HostLobbyTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime()
        self.c = self.lua.execute(BACKEND)
        self.panel = self.lua.execute((HERE / 'host_lobby.lua').read_text(encoding='utf8'),
                                      self.c.G, self.c.Panel, self.c.State)
        self.callback = self.lua.eval('function() return true end')
        self.record = self.lua.table()

    def test_native_queue_completion_refreshes_cached_ready_without_new_ready_event(self):
        self.panel.weplayPrepareResume(self.callback, self.record)
        self.c.now = 1499.75
        self.panel.update()
        self.assertFalse(self.c.enabled)
        self.assertEqual(self.c.refreshes, 0)
        self.c.now = 1500.25
        self.panel.update()
        self.assertFalse(self.c.State.allHumansAreReady)
        self.c.ready = True  # Real queue drained; native ready bit was already true.
        self.c.now = 1600.25
        self.panel.update()
        self.assertTrue(self.c.State.allHumansAreReady)
        self.panel.update()
        self.assertTrue(self.c.enabled)
        before = self.c.refreshes
        self.panel.update()
        self.assertEqual(self.c.refreshes, before)

    def test_saved_roster_mismatch_holds_start_even_when_peers_ready(self):
        self.panel.weplayPrepareResume(self.callback, self.record)
        self.c.now = 1500
        self.c.ready = True
        self.c.roster = False
        self.panel.update()
        self.panel.update()
        self.assertFalse(self.c.enabled)
        self.c.roster = True
        self.panel.update()
        self.assertTrue(self.c.enabled)

    def test_destroy_cancels_deferred_setup_and_failed_setup_never_refreshes_rows(self):
        self.panel.weplayPrepareResume(self.callback, self.record)
        self.panel.destroy()
        self.c.now = 2000
        self.panel.update()
        self.assertEqual(self.c.refreshes, 0)
        self.assertEqual(self.c.destroys, 1)
        self.panel.weplayPrepareResume(self.lua.eval('function() return false end'), self.record)
        self.c.now = 3500
        self.panel.update()
        self.assertEqual(self.c.refreshes, 0)


if __name__ == '__main__':
    unittest.main()
