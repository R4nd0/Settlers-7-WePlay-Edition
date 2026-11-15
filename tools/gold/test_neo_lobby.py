"""Host authority, native rejection and lifecycle checks for the Gold option."""
from pathlib import Path
import sys
import unittest

try:
    from lupa.lua51 import LuaRuntime
except ImportError:
    LuaRuntime = None


@unittest.skipIf(LuaRuntime is None, 'Lua 5.1 runtime unavailable')
class NeoLobbyTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime()
        self.c = self.lua.execute('''
local C={host=true,state="selected:0",capabilities="NEO-FIFO-INTENT-1",commands={},enabled=false,selected=false,accept=true}
local O={groupType={checkBoxGroup=1},arrangeOrder={heightFirst=1},buttonPlacement={toLeft=1}}
O.create=function(frame,pos,width,height,a,font,kind,order,columns,placement,callback)
 C.callback=callback;return {addItem=function(label,tooltip) C.label=label;return 1 end,
 setSelectedItem=function(id,value) C.selected=value;callback(id,value) end,getWidget=function()return 17 end}
end
C.G={require=function(name) assert(name=="local.ui.optionsGroup");return O end,
 UI={Anchor={}},font={small=1},system={getScreenSize=function()return 1920,1080 end},
 Network={IsHost=function()return C.host end},ui={enable=function(widget,value)assert(widget==17);C.enabled=value end,isEnabled=function()return C.enabled end}}
C.G.UI.WriteToConsole=function(command)
 assert(command:sub(1,18)=="weplay-neo-lab-v1:")
 command=command:sub(19)
 if command=="setup-capabilities" then return C.capabilities end
 if command=="setup-state" then return C.state end
 C.commands[#C.commands+1]=command
 if not C.accept then return false end
 if command=="setup:1" then C.state="selected:1" elseif command=="setup:0" then C.state="selected:0" else error(command) end
 return "1"
end
C.Panel={create=function()C.created=true end,update=function()C.updated=true end,destroy=function()C.destroyed=true end}
C.State={frame=7}
return C
''')
        self.lua.execute(Path(__file__).with_name('neo_lobby.lua').read_text(encoding='utf-8'),
                         self.c.G, self.c.Panel, self.lua.eval('function(c)return function()return c.State end end')(self.c))
        self.c.Panel.create()

    def test_host_can_enable_and_disable_through_same_visible_action(self):
        self.assertTrue(self.c.enabled)
        self.assertFalse(self.c.selected)
        self.assertEqual(len(self.c.commands), 0)  # programmatic refresh is not a new host action
        self.assertTrue(self.c.Panel.weplaySelectNEO(True))
        self.assertTrue(self.c.selected)
        self.assertTrue(self.c.Panel.weplaySelectNEO(False))
        self.assertFalse(self.c.selected)
        self.assertEqual([self.c.commands[i] for i in (1, 2)], ['setup:1', 'setup:0'])

    def test_guest_and_locked_match_cannot_change_choice(self):
        self.c.host = False
        self.c.Panel.update()
        self.assertFalse(self.c.Panel.weplaySelectNEO(True))
        self.c.host, self.c.state = True, 'locked:1'
        self.c.Panel.update()
        self.assertTrue(self.c.selected)
        self.assertFalse(self.c.enabled)
        self.assertFalse(self.c.Panel.weplaySelectNEO(False))
        self.assertEqual(len(self.c.commands), 0)

    def test_native_rejection_restores_authoritative_checkbox_state(self):
        self.c.accept = False
        self.assertFalse(self.c.Panel.weplaySelectNEO(True))
        self.assertFalse(self.c.selected)
        self.assertEqual(self.c.state, 'selected:0')

    def test_unknown_state_or_unsupported_adapter_disables_option(self):
        for value in (False, 'selected:2', '1', 'locked:0garbage'):
            self.c.state = value
            self.c.Panel.update()
            self.assertFalse(self.c.enabled)
        self.c.state, self.c.capabilities = 'selected:0', False
        self.c.Panel.update()
        self.assertFalse(self.c.enabled)

    def test_destroy_forgets_old_widgets(self):
        self.c.Panel.destroy()
        self.assertTrue(self.c.destroyed)
        self.assertFalse(self.c.Panel.weplaySelectNEO(True))


if __name__ == '__main__':
    unittest.main()
