"""Invented archive guards plus opt-in execution of the pinned original Lua.

Set S7_CARTOGRAPHER_ARCHIVE to an RC3.5 archive and provide lupa.lua51 on
PYTHONPATH. Integration runs Lua in invented globals, without a game process.
"""
import copy
import json
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/installer'),str(ROOT/'tools/lan'),str(Path(__file__).parent)]
import cartographer_context as context
from bba import Archive
from prepare import sha256
from test_lan_tools import synthetic_archive


class ArchiveGuardTests(unittest.TestCase):
    def setUp(self):
        self.original=synthetic_archive();self.target='Script/example.lua'
        self.changed=Archive(self.original).replace({self.target:b'return true\n'})
        self.recipe={'format':'s7-cartographer-context-v1','source_archive_sha256':sha256(self.original),
            'patched_archive_sha256':sha256(self.changed),'files':[{'path':self.target,
            'source_sha256':sha256(b'return false\n'),'patched_sha256':sha256(b'return true\n'),
            'edits':[{'offset':7,'before_hex':b'false'.hex(),'after_utf8':'true'}]}]}
        for name,value in (('OLD',sha256(self.original)),('NEW',sha256(self.changed)),('TARGET',self.target)):
            item=patch.object(context,name,value);item.start();self.addCleanup(item.stop)

    def test_only_pinned_member_changes(self):
        result=context.build_archive(self.original,self.recipe)
        self.assertEqual(result,self.changed)
        self.assertEqual(Archive(result).read('Script/unchanged.lua'),b'return 123\n')

    def test_wrong_archive_recipe_and_target_fail_closed(self):
        with self.assertRaises(ValueError):context.build_archive(self.original+b'changed',self.recipe)
        for changes in ({'format':'other'},{'source_archive_sha256':'0'*64},{'patched_archive_sha256':'0'*64},
                        {'files':[]},{'files':[{'path':'../other'}]},{'files':self.recipe['files']*2}):
            with self.subTest(changes=changes),self.assertRaises(ValueError):
                context.build_archive(self.original,dict(self.recipe,**changes))

    def test_wrong_delta_bytes_or_output_hash_is_rejected(self):
        for field,value in (('source_sha256','0'*64),('patched_sha256','f'*64)):
            changed=copy.deepcopy(self.recipe);changed['files'][0][field]=value
            with self.assertRaises(ValueError):context.build_archive(self.original,changed)
        changed=copy.deepcopy(self.recipe);changed['files'][0]['edits'][0]['before_hex']=b'wrong'.hex()
        with self.assertRaises(ValueError):context.build_archive(self.original,changed)


@unittest.skipUnless(os.environ.get('S7_CARTOGRAPHER_ARCHIVE'),'Set S7_CARTOGRAPHER_ARCHIVE for original Lua integration')
class ActualMenuTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from lupa.lua51 import LuaRuntime
        cls.runtime=staticmethod(LuaRuntime)
        cls.original=Path(os.environ['S7_CARTOGRAPHER_ARCHIVE']).read_bytes()
        cls.recipe=json.loads((ROOT/'tools/offline-ui/cartographer-context.json').read_text())
        cls.patched=context.build_archive(cls.original,cls.recipe)

    def fixture(self,*,original=False):
        source=Archive(self.original if original else self.patched).read(context.TARGET).decode('utf-8-sig')
        # Execute the actual module's constant decoder and action dispatcher.
        # The rest of the menu is replaced by bounded invented engine objects.
        prefix=source[:source.index('a(1)(...,')]
        start=source.index('local y=function(c)')
        route=source[start:source.index('local K=function',start)]
        deferred='' if original else self.recipe['files'][0]['edits'][1]['after_utf8'].split('l[n(132)]')[0]
        lua=self.runtime(unpack_returned_tuples=True,max_memory=32*1024*1024)
        factory=lua.execute(prefix+'''
          return function()
            local Test={ready=false,loading=false,loads=0,opens=0,closes=0}
            e.Game={InMapMode=function()return Test.ready end,CacheValue=function()end}
            e.loadingScreen={isCreated=function()return Test.loading end,creationMode={backgroundMap=7},
              create=function(kind)assert(kind==7);Test.loading=true;Test.loads=Test.loads+1 end}
            e.Network={SessionIsInSession=function()return false end,SessionLeave=function()error('unexpected leave')end}
            e.misc={getKey=function()return 'presence' end};e.network={setPresenceString=function()end}
            local M,main={},{};local J={[9]=M,[4]=main}
            local t={actionTypes={toMapForge=9,toMainMenu=4}}
            local b={mainState={lastAction='test'}};local f,p,o={},{},{}
            local l={currentPageModule=main,actionManager={createCallbacks=function()return {}end},moduleManager={
              registerClose=function()Test.closes=Test.closes+1 end,
              registerOpen=function(module)
                if module==M and not Test.ready then error('mapforge opened without menu world')end
                Test.opens=Test.opens+1
              end}}
        '''+route+'''
          return Test,l,y,function()
        '''+deferred+'''end
          end
        ''')
        return lua,factory()

    def test_real_archive_changes_only_main_state_and_all_lua_compiles(self):
        before,after=Archive(self.original),Archive(self.patched)
        changed={name for name,entry in before.entries.items() if entry.values[4]!=256 and before.read(name)!=after.read(name)}
        self.assertEqual(changed,{context.TARGET})
        lua=self.runtime(max_memory=32*1024*1024)
        for name,entry in after.entries.items():
            if entry.values[4]!=256 and name.endswith('.lua'):lua.compile(after.read(name).decode('utf-8-sig'))

    def test_original_dispatcher_opens_editor_before_context_negative_control(self):
        lua,(test,state,route,pump)=self.fixture(original=True)
        with self.assertRaisesRegex(Exception,'without menu world'):route(lua.table(actionType=9))

    def test_editor_waits_for_world_and_repeated_clicks_do_not_reload(self):
        lua,(test,state,route,pump)=self.fixture()
        action=lua.table(actionType=9,noPrevious=False,data=lua.table(marker=42))
        for _ in range(4):route(action);pump()
        self.assertEqual((test.loads,test.opens,test.closes),(1,0,0))
        self.assertEqual(state.weplayPendingMapForge.data.marker,42)
        test.ready=True;pump();pump()
        self.assertEqual((test.loads,test.opens,test.closes),(1,1,1))
        self.assertIsNone(state.weplayPendingMapForge)

    def test_normal_menu_does_not_load_world_and_leaving_cancels_pending_editor(self):
        lua,(test,state,route,pump)=self.fixture()
        route(lua.table(actionType=4));pump()
        self.assertEqual((test.loads,test.opens),(0,0))
        route(lua.table(actionType=9));route(lua.table(actionType=4))
        test.ready=True;pump()
        self.assertEqual((test.loads,test.opens),(1,0))
        self.assertIsNone(state.weplayPendingMapForge)

    def test_loaded_or_loading_world_does_not_start_another_load(self):
        lua,(test,state,route,pump)=self.fixture()
        test.loading=True;route(lua.table(actionType=9))
        self.assertEqual((test.loads,test.opens),(0,0))
        test.ready=True;pump();route(lua.table(actionType=9))
        self.assertEqual((test.loads,test.opens),(0,1))


if __name__=='__main__':unittest.main()
