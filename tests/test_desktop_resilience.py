"""Operation-scoped verification and interrupted settings groups; invented files."""
import io
import json
import os
import shutil
from pathlib import Path
import sys
import unittest
from types import SimpleNamespace
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

    def test_gold_settings_rollback_uses_no_history_compatibility_or_renderer_files(self):
        import gold_graphics
        (self.root/graphics.COMPAT).unlink()
        selected=dict(format=choice.FORMAT,choice=None,simple_menu=True)
        before=self.before()
        verified={'files':{}}
        with patch.object(self.desktop,'is_gold',return_value=True),patch.object(gold_graphics,'is_gold',return_value=True), \
                patch.object(gold_graphics,'checked_root',return_value=self.root), \
                patch.object(choice,'visual_supported',return_value=True),patch.object(self.desktop,'verify_instance',return_value=verified), \
                patch.object(choice,'_renderer',return_value=('dxvk',{})), \
                patch.object(self.desktop,'probe',return_value=dict(self.base.probe,devices=['Fixture GPU'],failure=None)), \
                patch.object(self.desktop,'renderer',return_value='dxvk'), \
                patch.object(backend.player_settings,'save',side_effect=OSError('synthetic Gold player failure')):
            with self.assertRaisesRegex(OSError,'Gold player failure'):
                self.desktop.save_settings(dict(renderer_policy='dxvk',choice=selected,
                    quality=dict(quality.DEFAULTS),player_name='Changed'))
        self.assertEqual(self.before(),before)
        self.assertFalse((self.root/graphics.COMPAT).exists())
        self.assertFalse((self.root/graphics.STATE).exists())

    def test_gold_save_commits_owned_files_without_a_history_manifest(self):
        import gold_graphics
        (self.root/graphics.COMPAT).unlink()
        with patch.object(self.desktop,'is_gold',return_value=True),patch.object(gold_graphics,'is_gold',return_value=True), \
                patch.object(gold_graphics,'checked_root',return_value=self.root),patch.object(choice,'visual_supported',return_value=True), \
                patch.object(self.desktop,'verify_instance',return_value={'files':{'Launcher/gold.fixture':'a'*64}}), \
                patch.object(choice,'_renderer',return_value=('dxvk',{})),patch.object(self.desktop,'renderer',return_value='dxvk'), \
                patch.object(self.desktop,'probe',return_value=dict(self.base.probe,devices=['Fixture GPU'],failure=None)):
            result=self.desktop.save_settings(dict(renderer_policy='dxvk',choice=dict(format=choice.FORMAT,choice=None,simple_menu=True),
                quality=dict(quality.DEFAULTS,WaterQuality=2),player_name='Changed'))
        self.assertTrue(result['settings_committed'])
        self.assertEqual(quality.load(self.root)['WaterQuality'],2)
        self.assertIn('Launcher/gold.fixture',self.desktop.read(self.root/'desktop-files.json')['files'])
        self.assertFalse((self.root/graphics.COMPAT).exists())

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
            result=self.desktop.save_settings(self.request())
        self.assertTrue(result['settings_committed']);self.assertTrue(result['refresh_failed'])
        self.assertEqual(result['committed_settings'],dict(self.request(),renderer='native'))
        self.assertFalse((self.desktop.state/settings.NAME).exists())
        self.assertFalse(choice._load_choice(self.root)['simple_menu'])
        self.assertEqual(quality.load(self.root)['TextureQuality'],2)
        self.assertEqual(self.desktop.installed()['renderer_policy'],'native')

    def interrupted(self):
        original=self.desktop.write
        def fail(path,value):
            if path==self.root/quality.FILE:raise OSError('synthetic disk failure')
            return original(path,value)
        with patch.object(self.desktop,'write',side_effect=fail),patch.object(settings.Transaction,'rollback',side_effect=SystemExit):
            with self.assertRaises(SystemExit):self.desktop.save_settings(self.request())

    def dispatch(self,action,data=None):
        return backend.dispatch(self.desktop,dict(format=backend.REQUEST,action=action,data=data or {}))

    def test_conflict_status_diagnostics_restore_and_next_save(self):
        before=self.before();self.interrupted()
        foreign=b'{"foreign":"preserve even invalid settings"}'
        (self.root/quality.FILE).write_bytes(foreign)
        journal=(self.desktop.state/settings.NAME).read_bytes()
        with patch.object(self.desktop,'recover_settings',side_effect=AssertionError('read-only recovery')):
            status=self.dispatch('recovery-status');report=self.dispatch('diagnostics')
        self.assertTrue(status['recoverable']);self.assertIn('quality',status['conflicts'])
        self.assertNotIn(str(self.root),json.dumps(report))
        result=self.dispatch('recovery-resolve',dict(token=status['token'],action='restore_previous'))
        backup=json.loads((self.desktop.state/result['backup']).read_bytes())['inputs']
        self.assertEqual(settings.decode(backup['current']['quality']),foreign)
        self.assertEqual(settings.decode(backup['journal']),journal)
        self.assertEqual(self.before(),before)
        self.assertTrue(self.dispatch('snapshot')['installed'])
        self.assertTrue(self.dispatch('save',self.request())['settings_committed'])

    def test_resolution_rejects_changed_confirmation_and_backup_failure(self):
        self.interrupted();status=self.dispatch('recovery-status')
        (self.root/quality.FILE).write_bytes(b'foreign')
        before=self.before()
        with self.assertRaisesRegex(ValueError,'recovery_changed'):
            self.dispatch('recovery-resolve',dict(token=status['token'],action='restore_previous'))
        status=self.dispatch('recovery-status')
        with patch.object(graphics,'atomic_create',side_effect=OSError('backup full')):
            with self.assertRaises(OSError):self.dispatch('recovery-resolve',dict(token=status['token'],action='restore_previous'))
        self.assertEqual(self.before(),before)

    def test_backup_tampering_is_detected_before_any_restore(self):
        self.interrupted();(self.root/quality.FILE).write_bytes(b'foreign');before=self.before()
        status=self.dispatch('recovery-status');original=graphics.atomic_create
        def tamper(path,value,staging):
            original(path,value,staging)
            if path.name.startswith('settings-recovery-'):path.write_bytes(b'tampered')
        with patch.object(graphics,'atomic_create',tamper):
            with self.assertRaisesRegex(ValueError,'preferences_invalid'):
                self.dispatch('recovery-resolve',dict(token=status['token'],action='restore_previous'))
        self.assertEqual(self.before(),before)

    def test_explicit_restore_retains_full_package_verification(self):
        self.interrupted();status=self.dispatch('recovery-status');before=self.before()
        (self.desktop.package/'Payload/uplay_r1_loader.dll').write_bytes(b'tampered package')
        with self.assertRaises(ValueError):self.dispatch('recovery-resolve',dict(token=status['token'],action='restore_previous'))
        self.assertEqual(self.before(),before)
        self.assertTrue((self.desktop.state/settings.NAME).exists())

    def test_interrupted_explicit_restore_can_be_resumed_without_losing_foreign_backup(self):
        before=self.before();self.interrupted();foreign=b'foreign-invalid-quality'
        (self.root/quality.FILE).write_bytes(foreign);status=self.dispatch('recovery-status')
        original=graphics.atomic_write
        def fail(path,value,staging):
            if path==self.root/quality.FILE:raise OSError('disk full during explicit restore')
            original(path,value,staging)
        with patch.object(graphics,'atomic_write',fail):
            with self.assertRaises(OSError):self.dispatch('recovery-resolve',dict(token=status['token'],action='restore_previous'))
        self.assertTrue((self.desktop.state/settings.NAME).exists())
        backups=list(self.desktop.state.glob('settings-recovery-*.json'))
        self.assertTrue(any(settings.decode(json.loads(path.read_bytes())['inputs']['current']['quality'])==foreign for path in backups))
        status=self.dispatch('recovery-status')
        self.dispatch('recovery-resolve',dict(token=status['token'],action='restore_previous'))
        self.assertEqual(self.before(),before);self.assertTrue(self.dispatch('snapshot')['installed'])

    def test_invalid_journal_detach_preserves_instances_and_returns_to_setup(self):
        raw=b'not-json journal';(self.desktop.state/settings.NAME).write_bytes(raw)
        before=self.before()
        # Start where the GUI starts: automatic recovery during snapshot, then
        # the same public error-code mapping used by desktop_backend.main.
        with self.assertRaises(ValueError) as failure:self.dispatch('snapshot')
        self.assertEqual(backend.error_code(failure.exception),'preferences_invalid')
        self.assertEqual((self.desktop.state/settings.NAME).read_bytes(),raw)
        self.assertEqual(self.before(),before)
        status=self.dispatch('recovery-status')
        self.assertFalse(status['recoverable']);self.assertTrue(status['can_detach'])
        result=self.dispatch('recovery-resolve',dict(token=status['token'],action='detach'))
        self.assertFalse(self.dispatch('snapshot')['installed'])
        for key,path in settings.paths(self.desktop,self.root).items():
            if key!='installation':self.assertEqual(settings.read(path),before[key])
        saved=json.loads((self.desktop.state/result['backup']).read_bytes())['inputs']
        self.assertEqual(settings.decode(saved['journal']),raw)
        self.assertEqual(settings.decode(saved['installation']),before['installation'])

    def test_corrupt_journal_startup_emits_recovery_error_through_real_main_ipc(self):
        before=self.before();journal=self.desktop.state/settings.NAME
        request=graphics.encoded(dict(format=backend.REQUEST,action='snapshot',data={}))
        for raw in (b'not-json journal',b'{"format":"broken"}',b'null'):
            with self.subTest(journal=raw):
                journal.write_bytes(raw);output=io.StringIO()
                with patch.object(sys,'argv',['desktop_backend.py','--app',str(self.desktop.app),'--operation','a'*32]), \
                     patch.object(sys,'stdin',SimpleNamespace(buffer=io.BytesIO(request))), \
                     patch.object(sys,'stdout',output), \
                     patch.object(backend,'__file__',str(self.desktop.app/'Package/Tools/installer/desktop_backend.py')), \
                     patch.object(backend,'Desktop',return_value=self.desktop):
                    result=backend.main()
                self.assertEqual(result,2)
                frame=json.loads(output.getvalue().splitlines()[-1])
                self.assertEqual(frame['kind'],'error');self.assertEqual(frame['code'],'preferences_invalid')
                self.assertEqual(journal.read_bytes(),raw);self.assertEqual(self.before(),before)
                status=self.dispatch('recovery-status')
                self.assertTrue(status['pending']);self.assertTrue(status['can_detach'])
                self.assertFalse(status['recoverable'])

    def test_detach_restart_preserves_registration_changed_after_interruption(self):
        self.interrupted();status=self.dispatch('recovery-status');original=Path.unlink
        def fail(path,*args,**kwargs):
            if path==self.desktop.state/'installation.json':raise OSError('synthetic unlink failure')
            return original(path,*args,**kwargs)
        with patch.object(Path,'unlink',fail):
            with self.assertRaises(OSError):self.dispatch('recovery-resolve',dict(token=status['token'],action='detach'))
        self.assertTrue((self.desktop.state/settings.DETACH).exists())
        with self.assertRaisesRegex(ValueError,'preferences_invalid'):self.dispatch('snapshot')
        registration=self.desktop.state/'installation.json';old=registration.read_bytes();registration.write_bytes(b'foreign')
        status=self.dispatch('recovery-status')
        with self.assertRaisesRegex(ValueError,'recovery_changed'):
            self.dispatch('recovery-resolve',dict(token=status['token'],action='detach'))
        self.assertEqual(registration.read_bytes(),b'foreign')
        registration.write_bytes(old);status=self.dispatch('recovery-status')
        self.dispatch('recovery-resolve',dict(token=status['token'],action='detach'))
        self.assertFalse(self.dispatch('snapshot')['installed'])

    def test_corrupt_detach_backup_is_read_only_manual_recovery_not_false_resume(self):
        self.interrupted();status=self.dispatch('recovery-status');original=Path.unlink
        def fail(path,*args,**kwargs):
            if path==self.desktop.state/'installation.json':raise OSError('synthetic interruption')
            return original(path,*args,**kwargs)
        with patch.object(Path,'unlink',fail):
            with self.assertRaises(OSError):self.dispatch('recovery-resolve',dict(token=status['token'],action='detach'))
        marker=json.loads((self.desktop.state/settings.DETACH).read_bytes())
        (self.desktop.state/marker['backup']).write_bytes(b'corrupt backup')
        registration=(self.desktop.state/'installation.json').read_bytes()
        status=self.dispatch('recovery-status')
        self.assertTrue(status['pending']);self.assertFalse(status['can_detach']);self.assertFalse(status['recoverable'])
        self.assertEqual(status['conflicts'],['unreadable']);self.assertIsNone(status['token'])
        self.assertEqual(self.dispatch('diagnostics')['recovery'],status)
        self.assertEqual((self.desktop.state/'installation.json').read_bytes(),registration)

    def test_unreadable_or_unowned_state_remains_diagnosable_but_not_resolvable(self):
        (self.desktop.state/settings.NAME).write_bytes(b'invalid')
        (self.desktop.state/'installation.json').write_bytes(b'invalid')
        status=self.dispatch('recovery-status')
        self.assertFalse(status['can_detach']);self.assertFalse(status['recoverable'])
        self.assertTrue(self.dispatch('diagnostics')['recovery']['pending'])
        with self.assertRaises(ValueError):self.dispatch('recovery-resolve',dict(token=status['token'],action='detach'))

    def test_save_reuses_one_display_snapshot(self):
        with patch.object(self.desktop,'displays',return_value=self.base.screens) as displays:
            result=self.desktop.save_settings(self.request())
        self.assertEqual(displays.call_count,1);self.assertEqual(result['displays'],self.base.screens)

    def test_failed_refresh_returns_normalized_native_generation_not_requested_effects(self):
        request=self.request();request['choice'].update(enhanced_aa=True,maximum_fx=True)
        with patch.object(choice,'_require_visual_support'),patch.object(self.desktop,'snapshot',side_effect=OSError('refresh failure')):
            result=self.desktop.save_settings(request)
        self.assertTrue(result['refresh_failed'])
        self.assertFalse(result['committed_settings']['choice']['enhanced_aa'])
        self.assertFalse(result['committed_settings']['choice']['maximum_fx'])
        self.assertEqual(result['committed_settings']['choice'],choice._load_choice(self.root))

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
