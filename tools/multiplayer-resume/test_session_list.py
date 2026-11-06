"""Exercise the real checkpoint browser callback, including map variants."""
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
local C={creates=0,loads=0,leaves=0,messages={},templates={"Mesa 15"},
    createOK=true,loadOK=true,rosterOK=true,prepared=0,transitions=0,closed=0}
C.record={map="Maps/Skirmish/s02_m06_Grassland_Mesa",template="Mesa 15",coop=0,
    players={{role=1},{role=1},{role=2},{role=2}}}
C.Resume={lookup=function(slot) assert(slot==1002);return C.record end,
    applyRoster=function(record) assert(record==C.record);C.applied=true;return C.rosterOK end}
C.dialog={setPosition=function() end,destroy=function() C.closed=C.closed+1 end,
    fetchActions=function() return {{actionType=1,data={itemData={data={slot=1002}}}}} end}
C.Host={weplayPrepareResume=function(callback,record)
    C.prepared=C.prepared+1;C.deferred=callback;assert(record==C.record) end}
C.Panel={create=function() end,update=function() end,destroy=function() end}
C.State={browserFrame={},sessionsFilterList={getSelectedItemData=function() return {lan=true} end},
    callbacks={toHostLobby=function(options) C.transitions=C.transitions+1;C.options=options end}}
C.G={require=function(name)
    if name=="local.tools.settings" then return {weplayMPResume=C.Resume}
    elseif name=="local.menu.panel.shared.tools.loadSaveDialogFactory" then
        return {createLoadMultiplayerGameDialog=function() return C.dialog end}
    elseif name=="local.menu.panel.shared.loadSaveListDialog" then return {actionTypes={confirm=1,cancel=2}}
    elseif name=="local.menu.panel.menu.play.multiplayer.hostLobby" then return C.Host end
    error(name)
end,system={getScreenSize=function() return 1280,720 end},
button={class={optionsButton=1},create=function(frame,position,label,class,callback) C.open=callback;return {} end},
UI={Anchor={Top=1,Left=2,Center=3}},
ui={setSize=function() end,messageBox=function(title,text) C.messages[#C.messages+1]=text end},
Game={GetSaveGameFileName=function(slot) assert(slot==1002);return C.savePath or "owned/s1002.s7s" end},
MapForge={GetTemplateNames=function(map) assert(map==C.record.map);C.templateReads=true;return C.templates end},
map={loadSetupMapConfig=function(map,template) C.setupMap=map;C.setupTemplate=template end},
MapSetup={SetCoopMode=function(mode) C.coop=mode end},
Network={SetIsQuickMatch=function(value) assert(value==false) end,
    SetIsPrivate=function(value) assert(value==false) end,
    SessionCreate=function(...) C.creates=C.creates+1;C.args={...};return C.createOK end,
    ChangeSavegame=function(path) C.loads=C.loads+1;C.path=path;return C.loadOK and path or "" end,
    SessionLeave=function() C.leaves=C.leaves+1 end}}
return C
'''


@unittest.skipIf(LuaRuntime is None, 'Lua5.1 runtime required')
class SessionListTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.c = self.lua.execute(BACKEND)
        self.panel = self.lua.execute((HERE / 'session_list.lua').read_text(encoding='utf8'),
                                      self.c.G, self.c.Panel, self.c.State)

    def select_save(self):
        self.panel.create(self.lua.table())
        self.c.open()
        self.panel.update()

    def test_variant_reaches_native_create_and_deferred_load_with_saved_template(self):
        self.select_save()
        c = self.c
        self.assertEqual(c.setupMap, c.record.map)
        self.assertEqual(c.setupTemplate, 'Mesa 15')
        self.assertEqual([c.args[i] for i in range(1, 9)],
                         [c.record.map, False, True, 2, True, True, 'Mesa 15', False])
        self.assertEqual(c.prepared, 1)
        self.assertEqual(c.loads, 0)
        self.assertEqual(c.transitions, 1)
        self.assertTrue(c.options.fixedTeams and c.options.fixedPositions)
        self.assertTrue(c.deferred())
        self.assertEqual(c.loads, 1)
        self.assertTrue(c.applied)
        self.assertEqual(c.leaves, 0)

    def test_standard_map_keeps_nil_setup_template_and_empty_native_name(self):
        self.c.record.template = ''
        self.select_save()
        self.assertIsNone(self.c.templateReads)
        self.assertIsNone(self.c.setupTemplate)
        self.assertEqual(self.c.args[7], '')
        self.assertTrue(self.c.deferred())

    def test_missing_or_other_map_template_cannot_fall_back_to_standard(self):
        for names in (None, self.lua.table(), self.lua.table_from(['Mesa 16'])):
            with self.subTest(names=names):
                self.c.templates = names
                self.select_save()
                self.assertEqual(self.c.creates, 0)
                self.assertIsNone(self.c.setupMap)
                self.assertEqual(self.c.prepared, 0)
                self.assertEqual(self.c.transitions, 0)
        self.assertEqual(len(self.c.messages), 3)

    def test_failed_create_or_native_load_never_starts_a_substitute_world(self):
        self.c.createOK = False
        self.select_save()
        self.assertEqual(self.c.prepared, 0)
        self.assertEqual(self.c.transitions, 0)
        self.c.createOK = True
        self.select_save()
        self.c.loadOK = False
        self.assertFalse(self.c.deferred())
        self.assertEqual(self.c.leaves, 1)
        self.assertIsNone(self.c.applied)

    def test_roster_failure_after_native_load_leaves_lobby(self):
        self.select_save()
        self.c.rosterOK = False
        self.assertFalse(self.c.deferred())
        self.assertEqual(self.c.loads, 1)
        self.assertEqual(self.c.leaves, 1)

    def test_unknown_save_does_not_read_template_or_create_lobby(self):
        self.c.record = None
        self.select_save()
        self.assertIsNone(self.c.templateReads)
        self.assertEqual(self.c.creates, 0)
        self.assertEqual(self.c.prepared, 0)


if __name__ == '__main__':
    unittest.main()
