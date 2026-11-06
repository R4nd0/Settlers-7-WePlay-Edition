import importlib.util
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/installer'),str(ROOT/'work/lua-test-runtime')]
import ingame_directory as bridge
try:
    from lupa.lua51 import LuaRuntime
except ImportError:LuaRuntime=None

CONTENT='a'*64
def row(**change):
    return dict(dict(id='b'*32,title='Test {title}',state='open',players=1,capacity=4,free_slots=3,
        content_id=CONTENT,network_revision=7,version='2.0.0-rc3.12',expires_at=2000000000),**change)

class BridgeTests(unittest.TestCase):
    def test_snapshot_is_data_only_bounded_and_marks_incompatible_games(self):
        raw=bridge.snapshot('c'*32,1,[row(title='";os.execute()')],{CONTENT})
        self.assertNotIn(b'os.execute',raw);self.assertNotIn(b'weplay1.',raw)
        self.assertIn(b'|o|1|4|',raw)
        self.assertIn(b'|i|1|4|',bridge.snapshot('c'*32,2,[row()],set()))
        self.assertLessEqual(len(bridge.snapshot('c'*32,2,[row(title='x'*48)]*128,{CONTENT})),32768)

    def test_only_current_run_displayed_open_game_can_request_switch(self):
        base=Path(os.environ.get('S7_TEST_TEMP_ROOT',ROOT/'work/test-temp'));base.mkdir(exist_ok=True,parents=True)
        for token,state in [('current','open'),('wrong','open'),('current','started')]:
            with tempfile.TemporaryDirectory(dir=base) as temporary:
                root=Path(temporary);(root/'LocalData').mkdir();instance=bridge.Bridge(root,'unused',{CONTENT})
                instance.write([row(state=state,free_slots=3 if state=='open' else 0)])
                (root/'LocalData'/bridge.REQUEST).write_text((instance.nonce if token=='current' else 'd'*32)+':'+'b'*32)
                instance.close()
                self.assertEqual(instance.pending,'b'*32 if token=='current' and state=='open' else None)
                self.assertFalse((root/'LocalData'/bridge.SNAPSHOT).exists())

class InterfaceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec=importlib.util.spec_from_file_location('weplay_global_interface_test',ROOT/'tools/game-directory/interface_patch.py')
        cls.module=importlib.util.module_from_spec(spec);spec.loader.exec_module(cls.module)
    def test_unknown_archive_is_rejected_before_parsing(self):
        with self.assertRaisesRegex(ValueError,'verified multiplayer interface'):self.module.build(b'unknown')
    @unittest.skipUnless((ROOT/'work/mp-resume-20261102/fixture-backup/B-Interface.bba').is_file() and LuaRuntime is not None,
                         'Private verified interface and Lua5.1 runtime required')
    def test_exact_interface_only_changes_browser_and_matches_package_pin(self):
        import build_package
        fixture=ROOT/'work/mp-resume-20261102/fixture-backup/B-Interface.bba';source=fixture.read_bytes()
        # Build today's checkpoint stage from its pinned input. A retained
        # output from an older checkpoint implementation is not today's input.
        raw=self.module.module.build(source)
        result=self.module.build(raw)
        self.assertEqual(hashlib.sha256(result).hexdigest(),build_package.DIRECTORY_ARCHIVE_SHA256)
        before,after=self.module.Archive(raw),self.module.Archive(result)
        changed=[name for name,entry in before.entries.items() if entry.values[4]!=256 and before.read(name)!=after.read(name)]
        self.assertEqual(set(changed),{self.module.MEMBER,self.module.HOST})
        self.assertTrue(LuaRuntime().eval('function(s) local f,e=loadstring(s);assert(f,e);return true end')(after.read(self.module.HOST).decode('utf-8-sig')))
        _,host,_=self.module.decoded_view(after.read(self.module.HOST))
        self.assertNotIn('["ShowInviteOverlay"]()',host)
        self.assertNotIn('["InvitePlayerToSession"](',host)
        self.assertTrue(LuaRuntime().eval('function(s) local f,e=loadstring(s);assert(f,e);return true end')(after.read(self.module.MEMBER).decode('utf-8-sig')))
        self.assertEqual(source,fixture.read_bytes())

@unittest.skipIf(LuaRuntime is None,'Optional private Lua5.1 test runtime')
class LuaTests(unittest.TestCase):
    def setUp(self):
        self.lua=LuaRuntime(unpack_returned_tuples=True)
        self.context=self.lua.execute('''
local C={time=0,session=false,buttons={},reads=0,exits=0,messages=0,rows={}}
local list={removeAll=function()C.rows={}end,addItem=function(row)C.rows[#C.rows+1]=row end,
getSelectedItemData=function()return C.selected and C.rows[C.selected+1]end,setSelectedIndex=function(index)C.selected=index end}
local controls={templates={genericListWithBackground=1},itemTemplates={singleTextItemTemplate=1},create=function()return list end}
local confirm={types={confirmYesNo=1},create=function(...)C.confirm=select(7,...);return {destroy=function()end}end}
local G={UI={Anchor={Top=1,Left=1},WriteToConsole=function(command)
if command=="weplay-directory-read-v1" then C.reads=C.reads+1;return C.raw end C.command=command;return C.accept end},
Game={GetRunTime=function()return C.time end},Network={SessionIsInSession=function()return C.session end},
system={getScreenSize=function()return 1280,720 end,exitApi=function()C.exits=C.exits+1 end},
defaultPriorities={todoHint=1},font={small=1},frame={create=function()return 1 end,destroy=function()end},
label={create=function()return 1 end},image={create=function()return 1 end,setColor=function()end},
ui={setSize=function()end,enable=function()end,disable=function()end,messageBox=function()C.messages=C.messages+1 end},
button={class={optionsButton=1},create=function(parent,position,text,kind,callback)
local key=type(text)=="table" and text[1] or text;C.buttons[key]=callback;return 1 end}}
function G.require(name)if name=="local.ui.listControl" then return {listControl=controls} else return confirm end end
local Panel={create=function()end,update=function()end,destroy=function()end}
C.G,C.Panel=G,Panel
return C
''')
        source=(ROOT/'tools/game-directory/session_list.lua').read_text(encoding='utf-8')
        self.lua.execute(source,self.context.G,self.context.Panel,self.lua.table(browserFrame=1))
    def start(self,rows=None):
        self.context.raw=bridge.snapshot('c'*32,1,rows or [row()],{CONTENT}).decode('ascii')
        self.context.Panel.create(None);self.context.buttons['WePlay Online']()
    def test_public_list_parsing_and_confirmed_menu_only_restart(self):
        self.start();self.assertEqual(self.context.rows[1].id,'b'*32)
        self.context.selected=0;self.context.accept=True
        self.context.buttons['Beitreten / Join']();self.context.confirm(self.lua.table(userData=True))
        self.assertEqual(self.context.exits,1)
        self.assertEqual(self.context.command,'weplay-directory-join-v1:'+'c'*32+':'+'b'*32)
    def test_session_and_stale_data_guard_prevent_exit(self):
        for guard in ('session','stale'):
            self.setUp();self.start();self.context.selected=0
            if guard=='session':self.context.session=True
            else:self.context.time=36000
            self.context.buttons['Beitreten / Join']()
            self.assertEqual(self.context.exits,0);self.assertIsNone(self.context.confirm)
    def test_network_data_is_not_executed_and_unavailable_or_incompatible_cannot_join(self):
        self.start([row(content_id='d'*64)]);self.context.selected=0
        self.context.buttons['Beitreten / Join']();self.assertIsNone(self.context.confirm)
        self.context.raw='return os.execute("never")';self.context.time=1000;self.context.Panel.update()
        self.assertEqual(len(self.context.rows),0);self.assertEqual(self.context.exits,0)

if __name__=='__main__':unittest.main()
