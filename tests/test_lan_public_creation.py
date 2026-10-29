"""Guard regressions plus opt-in execution of hash-locked Lua in Lua 5.1.

Set S7_MENU_ARCHIVE to the RC2 Interface.bba and add lupa to PYTHONPATH for the
read-only integration. Extracted game scripts and generated archives stay local.
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
import lan_public_game as public
from bba import Archive
from prepare import sha256
from test_lan_tools import synthetic_archive


class PublicMenuGuardTests(unittest.TestCase):
    def setUp(self):
        self.original=synthetic_archive();self.path='Script/example.lua'
        self.changed=Archive(self.original).replace({self.path:b'return true\n'})
        self.recipe={'format':'s7-lan-public-creation-v1','source_archive_sha256':sha256(self.original),
            'patched_archive_sha256':sha256(self.changed),'files':[{'path':self.path,
            'source_sha256':sha256(b'return false\n'),'patched_sha256':sha256(b'return true\n'),
            'edits':[{'offset':7,'before_hex':b'false'.hex(),'after_utf8':'true'}]}]}
        for name,value in (('OLD',sha256(self.original)),('NEW',sha256(self.changed)),('TARGETS',frozenset([self.path]))):
            item=patch.object(public,name,value);item.start();self.addCleanup(item.stop)

    def test_archive_and_unchanged_members_are_verified(self):
        result=public.build_archive(self.original,self.recipe)
        self.assertEqual(result,self.changed)
        self.assertEqual(Archive(result).read('Script/unchanged.lua'),b'return 123\n')

    def test_modified_source_recipe_and_unexpected_target_fail_closed(self):
        with self.assertRaises(ValueError):public.build_archive(self.original+b'changed',self.recipe)
        for change in ({'format':'other'},{'source_archive_sha256':'0'*64},{'patched_archive_sha256':'0'*64},
                       {'files':[]},{'files':[{'path':'../unexpected'}]}):
            with self.subTest(change=change),self.assertRaises(ValueError):
                public.build_archive(self.original,dict(self.recipe,**change))

    def test_wrong_before_bytes_and_output_hash_are_rejected(self):
        recipe=copy.deepcopy(self.recipe);recipe['files'][0]['edits'][0]['before_hex']=b'wrong'.hex()
        with self.assertRaises(ValueError):public.build_archive(self.original,recipe)
        with patch.object(public,'NEW','f'*64):
            recipe=copy.deepcopy(self.recipe);recipe['patched_archive_sha256']='f'*64
            with self.assertRaises(ValueError):public.build_archive(self.original,recipe)


@unittest.skipUnless(os.environ.get('S7_MENU_ARCHIVE'),'Set S7_MENU_ARCHIVE for read-only real-menu integration')
class ActualMenuTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from lupa.lua51 import LuaRuntime
        cls.runtime=staticmethod(LuaRuntime)
        cls.original=Path(os.environ['S7_MENU_ARCHIVE']).read_bytes()
        cls.recipe=json.loads((ROOT/'tools/offline-ui/lan-public-creation.json').read_text(encoding='utf-8'))
        cls.patched=public.build_archive(cls.original,cls.recipe)

    def lua(self):
        lua=self.runtime(unpack_returned_tuples=True,max_memory=16*1024*1024)
        lua.execute((ROOT/'tests/fixtures/lan_creation_ui.lua').read_text(encoding='utf-8'))
        return lua

    def test_real_archive_only_changes_the_two_allowed_members_and_compiles(self):
        before,after=Archive(self.original),Archive(self.patched)
        changed={name for name,entry in before.entries.items() if entry.values[4]!=256 and before.read(name)!=after.read(name)}
        self.assertEqual(changed,public.TARGETS)
        lua=self.lua()
        for name in public.TARGETS:lua.compile(after.read(name).decode('utf-8-sig'))

    def test_creation_checkbox_and_submitted_flag_stay_off_for_both_engine_modes(self):
        code=Archive(self.patched).read(next(n for n in public.TARGETS if n.endswith('/sessionCreation.lua'))).decode('utf-8-sig')
        for legacy in (False,True,None):
            with self.subTest(legacy_lan=legacy):
                lua=self.lua();module=lua.execute(code);g=lua.globals()
                module.create(g.Test.callbacks,False,lua.table_from({'lanMode':legacy}))
                lua.execute('Test.assertOff();Test.tamper()')
                module.update();lua.execute('Test.assertOff();Test.tamper();Test.pressCreate();Test.assertOff()')
                self.assertIs(g.Test.created['private'],False)
                self.assertEqual(g.Test.created.legacyLan,legacy)
                self.assertIs(g.Test.host.privateSession,False)
                self.assertTrue(all(value is False for _,value in g.Test.nativePrivate.items()))

    def test_old_menu_negative_control_exposes_the_selected_enabled_checkbox(self):
        code=Archive(self.original).read(next(n for n in public.TARGETS if n.endswith('/sessionCreation.lua'))).decode('utf-8-sig')
        lua=self.lua();module=lua.execute(code);g=lua.globals()
        module.create(g.Test.callbacks,False,lua.table_from({'lanMode':False}))
        selected,enabled=lua.eval('(function()local id=Test.privateId();return Test.selected[id],Test.widgets[id].enabled end)')()
        self.assertIs(selected,True);self.assertIs(enabled,True)

    def test_host_boundary_cannot_restore_a_stale_private_option(self):
        code=Archive(self.patched).read(next(n for n in public.TARGETS if n.endswith('/hostLobby.lua'))).decode('utf-8-sig')
        lua=self.lua();module=lua.execute(code);lua.globals().Test.hostModule=module
        lua.execute('''
        local state,constructor
        for index=1,20 do
          local name,value=debug.getupvalue(Test.hostModule.create,index)
          if not name then break end
          if name=='e' then state=value end
          if name=='C' then constructor=index end
        end
        assert(state and constructor,'Host creation boundary changed')
        debug.setupvalue(Test.hostModule.create,constructor,function()end)
        Test.hostModule.create({},false,{privateSession=true,fixedTeams=true})
        assert(state.privateSession==false,'Stale private host setting restored')
        ''')


if __name__=='__main__':unittest.main()
