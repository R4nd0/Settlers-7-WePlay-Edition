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
local C={host=true,state="selected:0",capabilities="NEO-FIFO-INTENT-1",commands={},enabled=false,selected=false,accept=true,width=1920}
local O={groupType={checkBoxGroup=1},arrangeOrder={heightFirst=1},buttonPlacement={toLeft=1}}
O.create=function(frame,pos,width,height,a,font,kind,order,columns,placement,callback)
 C.position=pos;C.optionWidth=width
 C.callback=callback;return {addItem=function(label,tooltip) C.label=label;return 1 end,
 setSelectedItem=function(id,value) C.selected=value;callback(id,value) end,getWidget=function()return 17 end}
end
C.G={require=function(name) assert(name=="local.ui.optionsGroup");return O end,
 UI={Anchor={}},font={small=1},system={getScreenSize=function()return C.width,1080 end},
 Network={IsHost=function()return C.host end},ui={enable=function(widget,value)assert(widget==17);C.enabled=value end,isEnabled=function()return C.enabled end}}
C.G.UI.WriteToConsole=function(command)
 if command=="weplay-crossplay-v1:mode" then return C.roomMode end
 if C.mixed then
  assert(command:sub(1,24)=="weplay-crossplay-neo-v1:")
  command=command:sub(25)
  if command=="capabilities" then return C.capabilities end
  if command=="selection" then return type(C.state)=="string" and C.state:match("^%a+:([01])$") or false end
  if command=="selectable" then
   if C.selectable~=nil then return C.selectable and "1" or "0" end
   return C.host and type(C.state)=="string" and C.state:match("^selected:[01]$") and "1" or "0"
  end
  C.commands[#C.commands+1]=command
  if not C.accept then return false end
  if command=="stage:1" then C.state="selected:1" elseif command=="stage:0" then C.state="selected:0" else error(command) end
  return "1"
 end
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
        self.c.mixed = getattr(self, 'mixed', False)
        self.lua.execute(Path(__file__).with_name('neo_lobby.lua').read_text(encoding='utf-8'),
                         self.c.G, self.c.Panel, self.lua.eval('function(c)return function()return c.State end end')(self.c), self.c.mixed)
        self.c.Panel.create()

    def test_host_can_enable_and_disable_through_same_visible_action(self):
        self.assertTrue(self.c.enabled)
        self.assertFalse(self.c.selected)
        self.assertEqual(len(self.c.commands), 0)  # programmatic refresh is not a new host action
        self.assertTrue(self.c.Panel.weplaySelectNEO(True))
        self.assertTrue(self.c.selected)
        self.assertTrue(self.c.Panel.weplaySelectNEO(False))
        self.assertFalse(self.c.selected)
        prefix = 'stage:' if self.c.mixed else 'setup:'
        self.assertEqual([self.c.commands[i] for i in (1, 2)], [prefix+'1', prefix+'0'])

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

    def test_checkbox_fits_small_and_normal_original_render_sizes(self):
        for width in (480, 1024, 1920):
            self.c.Panel.destroy()
            self.c.width = width
            self.c.Panel.create()
            self.assertGreaterEqual(self.c.position.x, 0)
            self.assertLessEqual(self.c.position.x+self.c.optionWidth, width)


class MixedNeoLobbyTests(NeoLobbyTests):
    mixed = True

    def test_room_host_uses_network_checkbox_without_explicit_flag(self):
        self.c.roomMode = 'room-host'
        panel = self.lua.eval('{create=function() end, update=function() end, destroy=function() end}')
        self.lua.execute(Path(__file__).with_name('neo_lobby.lua').read_text(encoding='utf-8'),
                         self.c.G, panel, self.lua.eval('function(c)return function()return c.State end end')(self.c))
        panel.create()
        self.assertTrue(panel.weplaySelectNEO(True))
        self.assertTrue(self.c.selected)
        self.assertEqual(self.c.commands[1], 'stage:1')

    def test_only_selectable_native_host_initializes_default_once(self):
        self.c.Panel.destroy()
        self.c.state, self.c.selectable = False, True
        self.c.Panel.create()
        self.assertEqual(self.c.commands[1], 'stage:0')
        self.assertFalse(self.c.selected)
        self.assertTrue(self.c.enabled)
        self.c.Panel.update()
        self.assertEqual(len(self.c.commands), 1)

    def test_native_default_rejection_is_not_retried_during_updates(self):
        self.c.Panel.destroy()
        self.c.state, self.c.selectable, self.c.accept = False, True, False
        self.c.Panel.create()
        self.c.Panel.update()
        self.assertFalse(self.c.enabled)
        self.assertEqual(len(self.c.commands), 1)

    def test_unavailable_native_selection_cannot_change_a_locked_or_guest_match(self):
        for host in (False, True):
            self.c.Panel.destroy()
            self.c.state, self.c.selectable, self.c.host = False, False, host
            self.c.Panel.create()
            self.assertFalse(self.c.enabled)
            self.assertFalse(self.c.Panel.weplaySelectNEO(True))
        self.assertEqual(len(self.c.commands), 0)


if __name__ == '__main__':
    unittest.main()
