"""Operation-scoped verification and interrupted settings groups; invented files."""
import json
import os
import shutil
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/installer'),str(Path(__file__).parent)]
import desktop_backend as backend
import desktop_settings as settings
import graphics_choice as choice
import graphics_profile as graphics
import install
import quality_settings as quality
import test_desktop_backend as fixtures
import test_standalone as portable_fixtures
import test_graphics_profile as profile_fixtures
import test_installer as package_fixtures


class DesktopResilienceTests(unittest.TestCase):
    def setUp(self):
        self.base=fixtures.DesktopTests();self.base.setUp();self.addCleanup(self.base.doCleanups)
        self.desktop=self.base.desktop;self.base.prepare();self.root=self.desktop.game()

    def request(self):
        return dict(renderer_policy='native',choice={'format':choice.FORMAT,'choice':None,'simple_menu':False},
                    quality=dict(quality.DEFAULTS,TextureQuality=2),player_name='Changed')

    def before(self):return {key:settings.read(path) for key,path in settings.paths(self.desktop,self.root).items()}

    def test_failure_at_each_persistent_write_restores_entire_previous_generation(self):
        targets=settings.paths(self.desktop,self.root)
        for key in ('quality','installation','ownership'):
            with self.subTest(write=key):
                before=self.before();original=self.desktop.write
                def fail(path,value):
                    if path==targets[key]:raise OSError('synthetic persistence failure')
                    return original(path,value)
                with patch.object(self.desktop,'write',side_effect=fail):
                    with self.assertRaises(OSError):self.desktop.save_settings(self.request())
                self.assertEqual(self.before(),before)
                self.assertFalse((self.desktop.state/settings.NAME).exists())

    def test_player_write_failure_rolls_back_choice_and_quality(self):
        before=self.before()
        with patch.object(backend.player_settings,'save',side_effect=OSError('synthetic player failure')):
            with self.assertRaises(OSError):self.desktop.save_settings(self.request())
        self.assertEqual(self.before(),before)

    def test_restart_recovers_interrupted_group_before_snapshot(self):
        before=self.before();original=self.desktop.write
        def fail(path,value):
            if path==self.root/quality.FILE:raise OSError('synthetic disk failure')
            return original(path,value)
        with patch.object(self.desktop,'write',side_effect=fail),patch.object(settings.Transaction,'rollback',side_effect=SystemExit('simulated abrupt exit')):
            with self.assertRaises(SystemExit):self.desktop.save_settings(self.request())
        self.assertTrue((self.desktop.state/settings.NAME).exists())
        restarted=backend.Desktop(self.desktop.app)
        with patch.object(restarted,'displays',return_value=self.base.screens):
            result=backend.dispatch(restarted,dict(format=backend.REQUEST,action='snapshot',data={}))
        self.assertTrue(result['installed']);self.assertEqual(self.before(),before)
        self.assertFalse((self.desktop.state/settings.NAME).exists())

    def test_foreign_settings_edit_is_preserved_and_blocks_recovery_before_any_rollback(self):
        original=self.desktop.write
        def fail(path,value):
            if path==self.root/quality.FILE:raise OSError('synthetic disk failure')
            return original(path,value)
        with patch.object(self.desktop,'write',side_effect=fail),patch.object(settings.Transaction,'rollback',side_effect=SystemExit):
            with self.assertRaises(SystemExit):self.desktop.save_settings(self.request())
        foreign=graphics.encoded(dict(quality.DEFAULTS,TextureQuality=0))
        (self.root/quality.FILE).write_bytes(foreign)
        current_choice=(self.root/choice.CHOICE_FILE).read_bytes()
        with self.assertRaisesRegex(ValueError,'preferences_invalid'):self.desktop.recover_settings()
        self.assertEqual((self.root/quality.FILE).read_bytes(),foreign)
        self.assertEqual((self.root/choice.CHOICE_FILE).read_bytes(),current_choice)
        self.assertTrue((self.desktop.state/settings.NAME).exists())

    def test_committed_save_survives_failed_snapshot(self):
        with patch.object(self.desktop,'snapshot',side_effect=OSError('synthetic UI refresh failure')):
            with self.assertRaisesRegex(ValueError,'settings_saved_refresh_failed'):self.desktop.save_settings(self.request())
        self.assertFalse((self.desktop.state/settings.NAME).exists())
        self.assertFalse(choice._load_choice(self.root)['simple_menu'])
        self.assertEqual(quality.load(self.root)['TextureQuality'],2)
        self.assertEqual(self.desktop.installed()['renderer_policy'],'native')

    def test_settings_save_verifies_package_once_then_discards_context(self):
        with patch.object(install,'package_manifest',wraps=install.package_manifest) as verify:
            self.desktop.save_settings(self.request());self.assertEqual(verify.call_count,1)
            self.desktop.save_settings(self.request());self.assertEqual(verify.call_count,2)
        self.assertIsNone(self.desktop._verification)

    def test_read_only_directory_and_snapshot_do_not_inventorize_package(self):
        manifest=json.loads((self.desktop.package/'package.json').read_bytes());manifest['game_directory']=True
        (self.desktop.package/'package.json').write_bytes(graphics.encoded(manifest))
        with patch.object(install,'package_manifest',side_effect=AssertionError('read-only full scan')),\
             patch('game_directory.configured',return_value='https://fixture.invalid'),\
             patch('game_directory.listing',return_value=[]),patch.object(self.desktop,'alternatives',return_value={}):
            self.assertEqual(self.desktop.directory_list()['games'],[])
            self.assertTrue(self.desktop.snapshot()['installed'])

    def test_listing_uses_only_v3_identity_without_full_verification_or_renderer_journal(self):
        import compatibility
        current=self.desktop.network_id(self.root)
        old=compatibility.load(self.root/'compatibility.json')['content_id']
        manifest=json.loads((self.desktop.package/'package.json').read_bytes());manifest['game_directory']=True
        (self.desktop.package/'package.json').write_bytes(graphics.encoded(manifest))
        rows=[dict(content_id=value) for value in (current,old,'f'*64)]
        with patch.object(install,'package_manifest',side_effect=AssertionError('read-only full scan')),\
             patch.object(self.desktop,'alternatives',side_effect=AssertionError('renderer alternatives scan')),\
             patch.object(graphics,'_load_state',side_effect=AssertionError('renderer journal scan')),\
             patch('game_directory.configured',return_value='https://fixture.invalid'),\
             patch('game_directory.listing',return_value=rows):
            self.assertEqual([row['compatible'] for row in self.desktop.directory_list()['games']],[True,False,False])

    def test_context_invalidates_after_tampering_and_does_not_cross_operations(self):
        path=self.desktop.package/'Payload/uplay_r1_loader.dll'
        before=path.read_bytes()
        with self.desktop.verification():
            self.desktop.package_manifest()
            path.write_bytes(before+b' changed')
            with self.assertRaises(ValueError):self.desktop.package_manifest()
        self.assertIsNone(self.desktop._verification)
        with patch.object(backend.launch_local,'run') as run:
            with self.assertRaises(ValueError):self.desktop.play()
            run.assert_not_called()

    def test_diagnostics_reports_actual_release_without_exporting_paths(self):
        self.desktop.write(self.desktop.state/'graphics-probe.json',self.base.probe)
        self.desktop.write(self.desktop.app/'desktop-release.json',dict(version='2.0.0-test',source_commit='a'*40,network_revision=9,neo_wire_revision=5))
        value=self.desktop.diagnostics()
        self.assertEqual(value['release'],'2.0.0-test');self.assertEqual(value['source_commit'],'a'*40)
        self.assertEqual(value['release_metadata_network_revision'],9)
        self.assertEqual(value['content_contract'],'s7-play-content-v3')
        self.assertEqual(value['directory_protocol_revision'],7);self.assertEqual(value['neo_wire_revision'],5)
        self.assertNotIn(str(self.desktop.app),json.dumps(value))


class PortableReadOnlyTests(unittest.TestCase):
    def test_already_bound_snapshot_does_not_rehash_package_but_play_still_rejects_tampering(self):
        base=portable_fixtures.StandaloneTests();base.setUp();self.addCleanup(base.doCleanups)
        base.launcher_snapshot()
        with patch.object(install,'package_manifest',side_effect=AssertionError('read-only full scan')):
            self.assertTrue(base.launcher_snapshot()['installed'])
        (base.app/'Package/Payload/uplay_r1_loader.dll').write_bytes(b'tampered fixture adapter')
        desktop=backend.Desktop(base.app)
        with patch.object(backend.launch_local,'run') as run:
            with self.assertRaises(ValueError):desktop.play()
            run.assert_not_called()


class RendererGroupTests(unittest.TestCase):
    """Real profile journal/lock implementation over invented pinned PE bytes."""
    def setUp(self):
        self.package=package_fixtures.InstallerTests();self.package.setUp();self.addCleanup(self.package.doCleanups)
        self.profile=profile_fixtures.GraphicsProfileTests();self.profile.setUp();self.addCleanup(self.profile.doCleanups)
        app=self.package.root/'app';app.mkdir();shutil.copytree(self.package.package,app/'Package')
        self.desktop=backend.Desktop(app);self.root=app/'Instances'/('a'*32)
        self.root.parent.mkdir();shutil.move(str(self.profile.root),self.root)
        payload=app/'Package/Payload/Graphics/d3d9.dll';payload.parent.mkdir();payload.write_bytes(self.profile.dll)
        manifest=json.loads((app/'Package/package.json').read_bytes())
        manifest['files']=graphics.content.inventory(app/'Package',('Payload','Runtime','Tools'))
        (app/'Package/package.json').write_bytes(graphics.encoded(manifest))
        self.desktop.write(self.root/'desktop-owner.json',dict(format=backend.FORMAT,owner=self.desktop.owner,instance=self.root.name))
        self.desktop.write(self.desktop.state/'installation.json',dict(format=backend.INSTALL_FORMAT,source='',
            container=str(self.root.parent),current=self.root.name,previous=[],package_id=self.desktop.package_id(),renderer_policy='auto'))
        self.desktop.record_ownership(self.root)
        for name,value in (('probe',{'devices':['Fixture GPU'],'failure':None}),('displays',[])):
            mocked=patch.object(self.desktop,name,return_value=value);mocked.start();self.addCleanup(mocked.stop)

    def request(self,policy):
        return dict(renderer_policy=policy,choice={'format':choice.FORMAT,'choice':None,'simple_menu':False},
                    quality=dict(quality.DEFAULTS,TextureQuality=2),player_name='Changed')

    def fail_quality(self,path,value):
        if path==self.root/quality.FILE:raise OSError('synthetic quality failure')
        return self.write(path,value)

    def test_native_to_modern_and_modern_to_native_failure_restore_renderer_and_settings(self):
        for previous,policy in (('native','auto'),('dxvk','native')):
            with self.subTest(previous=previous):
                if previous=='dxvk':graphics.install(self.root,self.desktop.package/'Payload/Graphics/d3d9.dll')
                before={key:settings.read(path) for key,path in settings.paths(self.desktop,self.root).items()}
                compatibility=(self.root/graphics.COMPAT).read_bytes();self.write=self.desktop.write
                with patch.object(self.desktop,'write',side_effect=self.fail_quality):
                    with self.assertRaises(OSError):self.desktop.save_settings(self.request(policy))
                self.assertEqual(choice._renderer(self.root,{})[0],previous)
                self.assertEqual((self.root/graphics.COMPAT).read_bytes(),compatibility)
                self.assertEqual({key:settings.read(path) for key,path in settings.paths(self.desktop,self.root).items()},before)
                graphics.content.verify(self.root)

    def test_restart_restores_renderer_after_interruption_before_settings_commit(self):
        compatibility=(self.root/graphics.COMPAT).read_bytes();self.write=self.desktop.write
        with patch.object(self.desktop,'write',side_effect=self.fail_quality),\
                patch.object(settings.Transaction,'rollback',side_effect=SystemExit('simulated abrupt exit')):
            with self.assertRaises(SystemExit):self.desktop.save_settings(self.request('auto'))
        self.assertEqual(choice._renderer(self.root,{})[0],'dxvk')
        self.desktop.recover_settings()
        self.assertEqual(choice._renderer(self.root,{})[0],'native')
        self.assertEqual((self.root/graphics.COMPAT).read_bytes(),compatibility)
        self.assertFalse((self.desktop.state/settings.NAME).exists())

    def test_unknown_renderer_edit_blocks_rollback_without_overwriting_it(self):
        self.write=self.desktop.write
        def fail(path,value):
            if path==self.root/quality.FILE:
                (self.root/graphics.DLL).write_bytes(b'foreign renderer edit')
                raise OSError('synthetic failure')
            return self.write(path,value)
        with patch.object(self.desktop,'write',side_effect=fail):
            with self.assertRaises(ValueError):self.desktop.save_settings(self.request('auto'))
        self.assertEqual((self.root/graphics.DLL).read_bytes(),b'foreign renderer edit')
        self.assertTrue((self.desktop.state/settings.NAME).exists())


if __name__=='__main__':unittest.main()
