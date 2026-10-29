"""Guard checks and optional real Lua 5.1 popup/caller lifecycle integration.

Set S7_ACHIEVEMENT_ARCHIVE to the current public-creation Interface.bba and add
the private Lua runtime directory to PYTHONPATH. No installed file is modified.
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
import achievement_notifications as notifications
from bba import Archive
from prepare import sha256
from test_lan_tools import synthetic_archive


class ArchiveGuardTests(unittest.TestCase):
    def setUp(self):
        self.original=synthetic_archive();self.target='Script/example.lua'
        self.changed=Archive(self.original).replace({self.target:b'return true\n'})
        self.recipe={'format':'s7-achievement-notifications-v1','source_archive_sha256':sha256(self.original),
            'patched_archive_sha256':sha256(self.changed),'files':[{'path':self.target,
            'source_sha256':sha256(b'return false\n'),'patched_sha256':sha256(b'return true\n'),
            'edits':[{'offset':7,'before_hex':b'false'.hex(),'after_utf8':'true'}]}]}
        for name,value in (('OLD',sha256(self.original)),('NEW',sha256(self.changed)),('TARGET',self.target)):
            item=patch.object(notifications,name,value);item.start();self.addCleanup(item.stop)

    def test_only_pinned_member_changes_and_all_members_roundtrip(self):
        result=notifications.build_archive(self.original,self.recipe)
        self.assertEqual(result,self.changed)
        self.assertEqual(Archive(result).read('Script/unchanged.lua'),b'return 123\n')

    def test_modified_archive_and_unexpected_or_duplicate_targets_fail_closed(self):
        with self.assertRaises(ValueError):notifications.build_archive(self.original+b'changed',self.recipe)
        for changes in ({'format':'other'},{'source_archive_sha256':'0'*64},{'patched_archive_sha256':'0'*64},
                        {'files':[]},{'files':[{'path':'../other'}]},
                        {'files':self.recipe['files']*2}):
            with self.subTest(changes=changes),self.assertRaises(ValueError):
                notifications.build_archive(self.original,dict(self.recipe,**changes))

    def test_wrong_delta_source_or_output_hash_is_rejected(self):
        changed=copy.deepcopy(self.recipe);changed['files'][0]['edits'][0]['before_hex']=b'wrong'.hex()
        with self.assertRaises(ValueError):notifications.build_archive(self.original,changed)
        changed=copy.deepcopy(self.recipe);changed['files'][0]['patched_sha256']='f'*64
        with self.assertRaises(ValueError):notifications.build_archive(self.original,changed)
        with patch.object(notifications,'NEW','e'*64):
            changed=copy.deepcopy(self.recipe);changed['patched_archive_sha256']='e'*64
            with self.assertRaises(ValueError):notifications.build_archive(self.original,changed)


@unittest.skipUnless(os.environ.get('S7_ACHIEVEMENT_ARCHIVE'),'Set S7_ACHIEVEMENT_ARCHIVE for real Lua integration')
class ActualPopupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from lupa.lua51 import LuaRuntime
        cls.runtime=staticmethod(LuaRuntime)
        cls.original=Path(os.environ['S7_ACHIEVEMENT_ARCHIVE']).read_bytes()
        recipe=json.loads((ROOT/'tools/offline-ui/achievement-notifications.json').read_text(encoding='utf-8'))
        cls.patched=notifications.build_archive(cls.original,recipe)

    def lua(self,*,original=False):
        lua=self.runtime(unpack_returned_tuples=True,max_memory=32*1024*1024)
        lua.execute((ROOT/'tests/fixtures/achievement_ui.lua').read_text(encoding='utf-8'))
        source=Archive(self.original if original else self.patched).read(notifications.TARGET).decode('utf-8-sig')
        # The real game loader supplies a module name to this variadic Lua chunk.
        factory=lua.execute(source,'weplay.achievement.fixture');lua.globals().Test.factory=factory
        return lua,factory

    def test_only_popup_factory_member_changes_and_other_scripts_compile(self):
        before,after=Archive(self.original),Archive(self.patched)
        changed={name for name,entry in before.entries.items() if entry.values[4]!=256 and before.read(name)!=after.read(name)}
        self.assertEqual(changed,{notifications.TARGET})
        lua,_=self.lua()
        for name,entry in after.entries.items():
            if entry.values[4]!=256 and name.endswith('.lua'):lua.compile(after.read(name).decode('utf-8-sig'))

    def test_inert_popup_interface_is_closed_idempotent_and_keeps_catalogue(self):
        lua,factory=self.lua()
        for identifier in ('AchieveCampaign0','AchieveMultiWin1','AchieveCastleIngame'):
            self.assertEqual(factory.getAchievementData(identifier).title,identifier)
            card=factory.create(lua.table(),lua.table(),identifier)
            self.assertFalse(card.isCreated())
            for _ in range(3):card.update();card.destroy();self.assertFalse(card.isCreated())
        self.assertEqual(lua.globals().Test.popupCalls,0)

    def test_original_factory_negative_control_attempts_a_real_modal(self):
        lua,factory=self.lua(original=True)
        with self.assertRaisesRegex(Exception,'popup_created'):
            factory.create(lua.table(),lua.table(), 'AchieveCampaign0')
        self.assertEqual(lua.globals().Test.popupCalls,1)

    def test_actual_game_caller_consumes_repeated_and_burst_cards_without_starvation(self):
        lua,_=self.lua()
        lua.execute("local fallback=require;require=function(name)if name=='local.menu.panel.shared.achievement'then return Test.factory end;return fallback(name)end")
        code=Archive(self.patched).read('Script/local/menu/game.lua').decode('utf-8-sig')
        lua.execute(code,'weplay.game.fixture')
        lua.execute('''
          game.lifeCycleManager={update=function()Test.ticks=Test.ticks+1 end}
          Game.IsPaused=function()return false end
          for i=1,100 do
            game.showAchievement('AchieveCampaign0')
            game.updateDefaultSystems()
            assert(#game.achievementList==0,'Closed notification was retried or retained')
          end
          assert(Test.ticks==100 and Test.popupCalls==0,'UI loop was blocked')
          for i=1,32 do game.showAchievement('AchieveMultiWin1')end
          for i=1,6 do game.updateDefaultSystems()end
          assert(#game.achievementList==0,'Burst notifications starved cleanup')
          assert(Test.ticks==106 and Test.popupCalls==0)
        ''')


if __name__=='__main__':unittest.main()
