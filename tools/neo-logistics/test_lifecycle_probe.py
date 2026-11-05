"""Lua 5.1 lifecycle/call-context regression; separate from native game evidence."""
from pathlib import Path
import sys
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1]/'work/lua-test-runtime'))
from lupa.lua51 import LuaRuntime

BACKEND = r'''
C={enabled=true,saved=false,exited=false,cache={},buttons={},logs={}}
G={game={started=true},Game={},Network={InNetworkMode=function() return false end},UI={},ui={},state={currentMode=1,mode={saveGame=2}}}
function G.Game.InMapMode() return true end
function G.Game.GetCurrentTurn() return 2005 end
function G.Game.SetGameTurnDuration() end
function G.Game.CacheValue(k,v) C.cache[k]=v end
function G.Game.GetCachedValue(k) return C.cache[k] end
function G.Game.SaveGameExists() return C.saved end
function G.UI.WriteToConsole(s)
 if s=='weplay-neo-v1:status' then return C.enabled and '1' or '0' end
 if s=='weplay-neo-v1:snapshot' then return C.payload end
 C.logs[#C.logs+1]=s
end
function G.require(name)
 if name=='local.tools.settings' then return {weplayNeoLogistics={active=function() return C.enabled end}} end
 if name=='local.file.saveGames' then return {getFreeSaveGameSlot=function() return 32 end} end
 error('unexpected module')
end
G.logisticsSystem={panelDragging=nil,panels={}}
local P=G.logisticsSystem
function P.create()
 C.panel=true;P.panels={}
 for i=1,4 do
  P.panels[i]={panelButton=i,tasks={{buttonID=i+10}}}
  C.buttons[i]=not C.enabled;C.buttons[i+10]=true
 end
end
function P.isCreated() return C.panel end
function P.destroy() C.panel=false end
function P.startPanelDragging() if not C.enabled then P.panelDragging=true end end
function G.ui.isEnabled(id) return C.buttons[id] end
function G.game.save() C.saved=true;coroutine.yield('native-save') end
function G.game.restartWithSaveGame() coroutine.yield('native-restart') end
G.system={exitApi=function() C.exited=true;coroutine.yield('native-exit') end}
local probes={}
function makeProbe()
 probes[#probes+1]=coroutine.create(assert(loadstring(SOURCE))(G,C.enabled))
 return #probes
end
function advance(id)
 local co=probes[id]
 local okay,event=coroutine.resume(co)
 return okay,event,coroutine.status(co)
end
'''


class LifecycleProbeTests(unittest.TestCase):
    def backend(self, enabled):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.globals().SOURCE = (HERE/'lifecycle_probe.lua').read_text(encoding='utf8')
        lua.execute(BACKEND)
        c = lua.globals().C
        c.enabled = enabled
        c.payload = 'NEO1\n2005\n1\n65537,0,1\n' if enabled else 'NEO0\n'
        return lua

    def saved(self, lua):
        probe = lua.globals().makeProbe()
        advance = lua.globals().advance
        self.assertEqual(advance(probe), (True, 'native-save', 'suspended'))
        self.assertEqual(advance(probe), (True, 'native-restart', 'suspended'))
        self.assertEqual(advance(probe), (True, None, 'dead'))

    def cycle(self, enabled):
        lua = self.backend(enabled)
        self.saved(lua)
        lua.globals().G.state.currentMode = 2
        self.assertEqual(lua.globals().advance(lua.globals().makeProbe()),
                         (True, 'native-exit', 'suspended'))
        self.assertTrue(lua.globals().C.exited)
        self.assertIn('PASS start-save-load exact-ledger', lua.globals().C.logs[len(lua.globals().C.logs)])

    def test_direct_lifecycle_preserves_engine_yield_context(self):
        self.cycle(True)

    def test_off_mode_retains_group_controls(self):
        self.cycle(False)

    def test_lost_ledger_never_reports_pass(self):
        lua = self.backend(True)
        self.saved(lua)
        lua.globals().C.payload = 'NEO1\n2005\n0\n'
        lua.globals().G.state.currentMode = 2
        okay, message, status = lua.globals().advance(lua.globals().makeProbe())
        self.assertFalse(okay)
        self.assertIn('saved intent ledger changed', message)
        self.assertEqual(status, 'dead')
        self.assertFalse(lua.globals().C.exited)


if __name__ == '__main__':
    unittest.main()
