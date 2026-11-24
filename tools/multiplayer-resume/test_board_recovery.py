"""Exercise the local History board scripts without redistributing game assets."""
import hashlib
import os
from pathlib import Path
import sys
import unittest

import interface_patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/installer'))
import install

sys.path.insert(0, str(ROOT / 'work/lua-test-runtime'))
try:
    from lupa.lua51 import LuaRuntime
except ImportError:
    LuaRuntime = None

FIXTURE = os.environ.get('S7_BOARD_ARCHIVE')
PANEL = 'Script/local/menu/panel/'
EXERCISE = b'''function(source, trade, scenario)
    local env, live, allocations = {}, 0, 0
    local function noop() end
    env._G = env
    env.assert, env.module, env.getfenv = assert, noop, getfenv
    env.type, env.pcall, env.next, env.pairs, env.ipairs = type, pcall, next, pairs, ipairs
    env.table, env.string, env.math, env.package = table, string, math, {seeall=noop}
    env.require = function() return {create=function() return {} end} end
    env.iconFactory={keys={}, sizes={32,64}, textureKeys={}, specialTypes={}, getGroupTexture=function()return{}end}
    env.Data={Groups={Castle=1}}; env.font={}; env.color={}; env.desktop={frame={}}
    env.Sound={PlayAsOneshot=noop}; env.Sounds={}; env.defaultPriorities={}
    env.system={getScreenSize=function() return 1920, 1080 end}
    env.GUI={GetCurrentPlayer=function() return 2 end}
    local slots = function() if scenario == 'unavailable' then return nil end return {[1]={}} end
    env.Technology={GetSlots=slots}; env.Trade={GetSlots=slots, GetStartSlot=function() return 1 end}
    env.misc={getTableCount=function(t) local n=0; for _ in pairs(t) do n=n+1 end; return n end}
    env.frame={create=function(name)
        if name=='techBoardFrame' or name=='tradeBoardMainFrame' then
            allocations=allocations+1; live=live+1
            return {root=true}
        end
        if scenario=='late' then error('fixture: widget allocation failed') end
        return {}
    end, destroy=function(frame) if frame.root then live=live-1 end end}
    env.UI={Anchor={}}; env.image={create=function() return {} end}
    env.ui={setSize=noop, setAlpha=noop, setPriority=noop,
            callback={leftDown=noop, leftUp=noop, leftRelease=noop}}
    local chunk=assert(loadstring(source, 'owned board fixture'))
    setfenv(chunk,env)
    local panel=chunk('fixture')
    local function setLayout()
        local nodes={[1]={x=10,y=20}}
        if scenario=='different-count' then nodes={} end
        if scenario=='wrong-id' then nodes={[2]={x=10,y=20}} end
        local layout={nodes=nodes, backgroundImage={name='fixture',w=2048,h=1080}}
        if trade then panel.setTradeboardLayout(layout) else panel.setTechboardLayout(layout) end
    end
    if scenario ~= 'missing' and scenario ~= 'retry' then setLayout() end
    local ok
    if trade then ok=pcall(panel.create) else ok=pcall(panel.show,true) end
    local created=panel.isCreated()
    panel.show(false)
    if scenario=='retry' then
        assert(ok and not created and allocations==0)
        setLayout()
        scenario='late'
        assert(panel.show(true)==false)
        created=panel.isCreated()
    end
    return ok, created, live, allocations
end'''


@unittest.skipUnless(FIXTURE and LuaRuntime is not None, 'Set S7_BOARD_ARCHIVE to the installed History archive and provide Lua 5.1')
class BoardRecoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        raw = Path(FIXTURE).read_bytes()
        assert hashlib.sha256(raw).hexdigest() == install.BOARD_RECOVERY_SOURCE
        updated = interface_patch.guard_boards(raw)
        assert hashlib.sha256(updated).hexdigest() == install.BOARD_RECOVERY_ARCHIVE
        cls.before = interface_patch.Archive(raw)
        cls.after = interface_patch.Archive(updated)

    def test_archive_changes_only_the_three_ui_members(self):
        self.assertEqual(self.before.entries.keys(), self.after.entries.keys())
        compile_lua = LuaRuntime(encoding=None).eval(b'function(s) assert(loadstring(s)) end')
        changed = set()
        for name, entry in self.before.entries.items():
            if entry.values[4] == 256:
                continue
            if self.before.read(name) != self.after.read(name):
                compile_lua(self.after.read(name))
                changed.add(name)
        self.assertEqual(changed, {PANEL + name for name in ('techBoardPanel.lua', 'tradeBoard.lua', 'bottomBar.lua')})

    def test_invalid_layout_never_creates_a_fullscreen_frame(self):
        run = LuaRuntime(encoding=None).eval(EXERCISE)
        for trade, name in ((False, 'techBoardPanel.lua'), (True, 'tradeBoard.lua')):
            for scenario in (b'missing', b'different-count', b'wrong-id', b'unavailable'):
                with self.subTest(board=name, scenario=scenario):
                    self.assertEqual(run(self.after.read(PANEL+name), trade, scenario), (True, False, 0, 0))

    def test_late_widget_failure_destroys_partial_board(self):
        run = LuaRuntime(encoding=None).eval(EXERCISE)
        for trade, name in ((False, 'techBoardPanel.lua'), (True, 'tradeBoard.lua')):
            with self.subTest(board=name):
                self.assertEqual(run(self.before.read(PANEL+name), trade, b'late'), (False, False, 1, 1))
                self.assertEqual(run(self.after.read(PANEL+name), trade, b'late'), (True, False, 0, 1))

    def test_opening_can_be_retried_when_the_layout_arrives(self):
        run = LuaRuntime(encoding=None).eval(EXERCISE)
        for trade, name in ((False, 'techBoardPanel.lua'), (True, 'tradeBoard.lua')):
            with self.subTest(board=name):
                self.assertEqual(run(self.after.read(PANEL+name), trade, b'retry'), (True, False, 0, 1))

    def test_toolbar_clears_only_a_failed_board_activation(self):
        raw = self.after.read(PANEL + 'bottomBar.lua')
        begin = raw.index(b'local panel = l.bottomBar.activePanel')
        end = raw.index(b'elseif(l.bottomBar.clickedButtonKey==t)then', begin)
        activation = raw[begin:end] + b'end\n'
        run = LuaRuntime(encoding=None).eval(b'''function(trade, opens, other)
            local t={show=function()return opens end}
            local o={show=function()return opens end}
            local panel=other and {show=function()return false end} or (trade and o or t)
            local l={bottomBar={activePanel=panel, preferredGameInfoMode=2}, gameInfo={modes={default=1}}}
''' + activation + b'''return l.bottomBar.activePanel==panel, l.bottomBar.preferredGameInfoMode end''')
        for trade in (False, True):
            self.assertEqual(run(trade, False, False), (False, 1))
            self.assertEqual(run(trade, True, False), (True, 2))
        self.assertEqual(run(False, False, True), (True, 2))


if __name__ == '__main__':
    unittest.main()
