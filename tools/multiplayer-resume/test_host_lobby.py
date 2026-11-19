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
local C={now=0,session=true,host=true,slot=1,map=true,ready=false,roster=true,enabled=false,created=true,
    updates=0,refreshes=0,destroys=0}
C.State={startButton={},allHumansAreReady=false}
C.getState=function() return C.State end
C.Panel={isCreated=function() return C.created end,
    create=function() C.created=true;C.State={startButton={},allHumansAreReady=false} end,
    update=function() C.updates=C.updates+1;C.enabled=C.State.allHumansAreReady
        if C.onUpdate then C.onUpdate() end end,
    destroy=function() C.destroys=C.destroys+1;C.created=false;C.State={} end,
    updatePlayerSlots=function()
        assert(C.created and C.session and C.host,"native slot update after lobby ownership ended")
        C.refreshes=C.refreshes+1;C.State.allHumansAreReady=C.ready end}
C.G={Game={GetRunTime=function() return C.now end},
    require=function() return {weplayMPResume={ready=function() return C.roster end,
        rememberRoomLobby=function() return false end}} end,
    ui={enable=function(button,enabled) assert(button==C.State.startButton);C.enabled=enabled end},
    Network={SessionIsInSession=function() return C.session end,IsHost=function() return C.host end,
        GetLocalPlayerSlot=function() return C.slot end},MapSetup={IsValid=function() return C.map end}}
return C
'''


@unittest.skipIf(LuaRuntime is None, 'Lua5.1 runtime required')
class HostLobbyTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime()
        self.c = self.lua.execute(BACKEND)
        self.install()
        self.callback = self.lua.eval('function() return true end')
        self.record = self.lua.table()

    def install(self):
        self.panel = self.lua.execute((HERE / 'host_lobby.lua').read_text(encoding='utf8'),
                                      self.c.G, self.c.Panel, self.c.getState)

    def use_room(self):
        self.c = self.lua.execute(BACKEND)
        self.c.commands = self.lua.table_from({'mode': 'room-host'})
        self.c.G.UI = self.lua.eval('function(C) return {WriteToConsole=function(key) '
                                    'local name=key:match("^weplay%-crossplay%-v1:(.+)$") '
                                    'if name=="room-sync-teams" then C.teamRequests=(C.teamRequests or 0)+1 end; '
                                    'return C.commands[name] end} end')(self.c)
        self.install()

    def test_room_host_synchronizes_native_teams_before_enabling_start(self):
        self.use_room()
        self.c.ready = True
        self.c.State.allHumansAreReady = True
        self.c.commands['room-all-ready'] = '1'
        self.c.commands['room-sync-teams'] = '1'
        self.panel.update()
        self.panel.update()
        self.assertEqual(self.c.teamRequests, 1)
        self.assertFalse(self.c.enabled)
        self.c.commands['room-teams-confirmed'] = '1'
        self.panel.update()
        self.panel.update()
        self.assertTrue(self.c.enabled)

    def test_saved_room_waits_for_native_file_selection_and_guests(self):
        self.use_room()
        callback = self.lua.eval('function(C) return function() C.applied=true; return true end end')(self.c)
        self.panel.weplayPrepareResume(callback, self.record)
        self.c.ready = True
        self.c.roster = False
        self.c.now = 10000
        self.panel.update()
        self.assertIsNone(self.c.applied)
        self.c.commands['room-saved-state'] = 'selection-available'
        self.panel.update()
        self.assertTrue(self.c.applied)
        self.assertFalse(self.c.enabled)
        self.c.commands['room-saved-state'] = 'ready-verified'
        self.panel.update()
        self.assertTrue(self.c.enabled)

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
        self.panel.create()
        self.panel.weplayPrepareResume(self.lua.eval('function() return false end'), self.record)
        self.c.now = 3500
        self.panel.update()
        self.assertEqual(self.c.refreshes, 0)

    def test_late_configuration_event_after_destroy_never_updates_native_slots(self):
        self.panel.destroy()
        self.panel.updatePlayerSlots()
        self.assertEqual(self.c.refreshes, 0)

    def test_session_loss_cancels_cached_ready_refresh(self):
        self.panel.weplayPrepareResume(self.callback, self.record)
        self.c.now = 1500
        self.panel.update()
        before = self.c.refreshes
        self.c.session = False
        self.c.now = 1700
        self.panel.update()
        self.panel.updatePlayerSlots()
        self.assertEqual(self.c.refreshes, before)
        self.assertFalse(self.c.enabled)

    def test_destroy_during_original_update_cancels_deferred_work(self):
        self.panel.weplayPrepareResume(self.callback, self.record)
        self.c.onUpdate = self.panel.destroy
        self.c.now = 1500
        self.panel.update()
        self.assertEqual(self.c.refreshes, 0)
        self.assertEqual(self.c.destroys, 1)

    def test_resume_callback_leaving_session_does_not_refresh_native_slots(self):
        callback = self.lua.eval('function(C) return function() C.session=false;return true end end')(self.c)
        self.panel.weplayPrepareResume(callback, self.record)
        self.c.now = 1500
        self.panel.update()
        self.assertEqual(self.c.refreshes, 0)

    def test_recreated_lobby_reads_current_original_state(self):
        self.panel.destroy()
        self.panel.create()
        self.panel.weplayPrepareResume(self.callback, self.record)
        self.c.ready = True
        self.c.now = 1500
        self.panel.update()
        self.panel.update()
        self.assertTrue(self.c.enabled)
        self.assertTrue(self.c.State.allHumansAreReady)

    def test_lost_host_role_disables_start_without_native_slot_updates(self):
        for field, value in [('host', False), ('slot', 2)]:
            with self.subTest(field=field):
                self.setUp()
                self.c.State.allHumansAreReady = True
                self.c[field] = value
                self.panel.update()
                self.panel.updatePlayerSlots()
                self.assertFalse(self.c.enabled)
                self.assertEqual(self.c.refreshes, 0)


if __name__ == '__main__':
    unittest.main()
