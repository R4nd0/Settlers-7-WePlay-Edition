"""Managed setup, recovery, probe and ownership contracts using invented files."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/installer'),str(Path(__file__).parent)]
import desktop_backend as backend
import game_detection
import graphics_choice as choice
import graphics_profile as graphics
import compatibility as content
import install
import quality_settings as quality
import memory_guard
import test_installer as fixture


class DesktopTests(unittest.TestCase):
    def setUp(self):
        self.fixture=fixture.InstallerTests();self.fixture.setUp();self.addCleanup(self.fixture.doCleanups)
        self.app=self.fixture.root/'app';self.app.mkdir()
        shutil.copytree(self.fixture.package,self.app/'Package')
        self.events=[]
        self.desktop=backend.Desktop(self.app,lambda kind,**fields:self.events.append((kind,fields)))
        self.screens=[dict(device=r'\\.\DISPLAY1',adapter='Fixture GPU',primary=True,x=0,y=0,width=1920,height=1080,
                           resolutions=[[1280,720],[1920,1080]])]
        self.probe={'format':'s7-desktop-probe-v1','probe_sha256':'a'*64,'adapters':['Fixture GPU'],'devices':[],'failure':'unsupported'}
        def save(root,value):
            value=choice.validate_choice(value);(root/'LocalData').mkdir(exist_ok=True)
            (root/choice.CHOICE_FILE).write_bytes(graphics.encoded(value));return value
        for target,options in [((graphics,'ensure_closed'),{}),((choice,'save_choice'),{'side_effect':save}),
                               ((choice,'_renderer'),{'return_value':('native',{})}),
                               ((self.desktop,'probe'),{'return_value':self.probe}),
                               ((self.desktop,'displays'),{'return_value':self.screens}),
                               ((self.desktop,'renderer'),{'return_value':'native'})]:
            item=patch.object(*target,**options);item.start();self.addCleanup(item.stop)

    def prepare(self):return self.desktop.install_game(self.fixture.source)

    def test_detect_supported_originals_once_and_reject_changed_or_linked_sources(self):
        found=game_detection.detect([self.fixture.source,self.fixture.source,self.fixture.root/'missing'])
        self.assertEqual([x['path'] for x in found],[str(self.fixture.source.resolve())])
        (self.fixture.source/'Base/Bin/Settlers7R.exe').write_bytes(b'changed')
        self.assertEqual(game_detection.detect([self.fixture.source]),[])

    def test_setup_is_one_transaction_without_cmd_starters_and_records_ownership(self):
        original={p.relative_to(self.fixture.source):p.read_bytes() for p in self.fixture.source.rglob('*') if p.is_file()}
        result=self.prepare();root=self.desktop.game()
        self.assertTrue(result['installed']);self.assertFalse(result['needs_repair'])
        self.assertEqual(list(root.glob('*.cmd')),[])
        self.assertEqual(content.verify(root)['content_id'],content.load(root/'compatibility.json')['content_id'])
        self.assertEqual({p.relative_to(self.fixture.source):p.read_bytes() for p in self.fixture.source.rglob('*') if p.is_file()},original)
        self.assertTrue((root/'desktop-files.json').is_file())
        self.assertEqual(choice._load_choice(root)['display']['mode'],'borderless')
        self.assertIn('copy',[fields['phase'] for kind,fields in self.events if kind=='progress'])

    def test_repair_preserves_same_pc_saves_and_previous_copy(self):
        self.prepare();old=self.desktop.game();old_manifest=(old/'compatibility.json').read_bytes()
        (old/'LocalData/saves').mkdir();(old/'LocalData/saves/1.s7local').write_bytes(b'opaque save fixture')
        (old/'LocalData/local-profile-id.txt').write_bytes(b'opaque identity fixture')
        self.prepare();new=self.desktop.game()
        self.assertNotEqual(old,new)
        self.assertEqual((new/'LocalData/saves/1.s7local').read_bytes(),b'opaque save fixture')
        self.assertEqual((new/'LocalData/local-profile-id.txt').read_bytes(),b'opaque identity fixture')
        self.assertEqual((old/'compatibility.json').read_bytes(),old_manifest)
        self.assertEqual(self.desktop.installed()['previous'],[{'container':str(old.parent),'instance':old.name}])

    def test_failed_or_cancelled_preparation_never_switches_active_version(self):
        self.prepare();before=(self.desktop.state/'installation.json').read_bytes();old=self.desktop.game()
        with patch.object(install,'prepare',side_effect=OSError('invented disk failure')):
            with self.assertRaises(OSError):self.prepare()
        self.assertEqual((self.desktop.state/'installation.json').read_bytes(),before)
        self.desktop.cancel.write_text('cancel')
        with self.assertRaises(backend.Cancelled):self.prepare()
        self.assertEqual((self.desktop.state/'installation.json').read_bytes(),before)
        self.assertTrue((old/'Base/Bin/Settlers7R.exe').is_file())

    def test_destination_change_keeps_previous_locations_and_cleanup_preserves_personal_data(self):
        self.prepare();old=self.desktop.game();(old/'LocalData/saves').mkdir()
        (old/'LocalData/saves/keep.txt').write_bytes(b'personal');(old/'unknown.txt').write_bytes(b'unknown')
        target=self.fixture.root/'other-drive'
        self.desktop.install_game(self.fixture.source,target);new=self.desktop.game()
        self.assertEqual(new.parent,target)
        self.assertFalse(self.desktop.snapshot()['needs_repair'])
        self.assertEqual(self.desktop.installed()['previous'],[{'container':str(old.parent),'instance':old.name}])
        self.assertEqual((new/'LocalData/saves/keep.txt').read_bytes(),b'personal')
        with patch.object(subprocess,'run',return_value=SimpleNamespace(returncode=0,stdout=b'[]')):
            self.assertEqual(self.desktop.clean_old(),{'cleaned':1})
        self.assertFalse((old/'Base/Bin/Settlers7R.exe').exists())
        self.assertTrue((new/'Base/Bin/Settlers7R.exe').is_file())
        self.assertEqual((old/'unknown.txt').read_bytes(),b'unknown')
        self.assertEqual((old/'LocalData/saves/keep.txt').read_bytes(),b'personal')

    def test_legacy_cross_container_record_recovers_only_exact_owned_default_instances(self):
        self.prepare();old=self.desktop.game()
        self.desktop.install_game(self.fixture.source,self.fixture.root/'moved')
        value=self.desktop.installed();value.update(format=backend.FORMAT,previous=[old.name])
        self.desktop.write(self.desktop.state/'installation.json',value)
        before=(self.desktop.state/'installation.json').read_bytes()
        self.assertFalse(self.desktop.snapshot()['needs_repair'])
        self.assertEqual((self.desktop.state/'installation.json').read_bytes(),before)
        result=self.desktop.migrate_record();self.assertTrue(result['installed'])
        self.assertEqual(self.desktop.read(self.desktop.state/'installation.json')['format'],backend.INSTALL_FORMAT)
        backups=list(self.desktop.state.glob('installation-v1-backup-*.json'));self.assertEqual(len(backups),1)
        self.assertEqual(json.loads(backups[0].read_bytes()),value)
        self.assertEqual(backups[0].read_bytes(),before)
        value['previous']=['a'*32];self.desktop.write(self.desktop.state/'installation.json',value)
        with self.assertRaisesRegex(ValueError,'installation_invalid'):self.desktop.installed()

    def test_uninstall_follows_cross_container_references_and_keeps_all_saves(self):
        self.prepare();old=self.desktop.game();(old/'LocalData/saves').mkdir()
        (old/'LocalData/saves/keep.txt').write_bytes(b'personal')
        self.desktop.install_game(self.fixture.source,self.fixture.root/'moved');new=self.desktop.game()
        with patch.object(subprocess,'run',return_value=SimpleNamespace(returncode=0,stdout=b'[]')):
            self.desktop.uninstall()
        for root in (old,new):
            self.assertFalse((root/'Base/Bin/Settlers7R.exe').exists())
            self.assertEqual((root/'LocalData/saves/keep.txt').read_bytes(),b'personal')

    def test_corrupt_preferences_are_left_in_old_copy_and_reset_in_repair(self):
        self.prepare();old=self.desktop.game();(old/quality.FILE).write_bytes(b'not json')
        self.prepare();new=self.desktop.game()
        self.assertEqual((old/quality.FILE).read_bytes(),b'not json')
        self.assertEqual(quality.load(new),quality.DEFAULTS)
        self.assertIn(('warning',{'code':'preferences_reset'}),self.events)

    def test_cleanup_retains_personal_unknown_and_modified_files(self):
        self.prepare();root=self.desktop.game()
        (root/'LocalData/saves').mkdir();(root/'LocalData/saves/1.s7local').write_bytes(b'keep save')
        modified=root/'Base/BBA/map.bba';modified.write_bytes(b'keep modification')
        (root/'my-document.txt').write_bytes(b'keep unknown')
        with patch.object(subprocess,'run',return_value=SimpleNamespace(returncode=0,stdout=b'[]')):
            result=self.desktop.cleanup_instance(root.parent,root.name)
        self.assertGreater(result['kept_modified'],0)
        self.assertEqual((root/'LocalData/saves/1.s7local').read_bytes(),b'keep save')
        self.assertEqual(modified.read_bytes(),b'keep modification')
        self.assertEqual((root/'my-document.txt').read_bytes(),b'keep unknown')
        self.assertFalse((root/'Base/Bin/Settlers7R.exe').exists())
        self.assertTrue((root/'desktop-owner.json').exists())

    def test_cleanup_rejects_path_escape_and_personal_alias_before_any_process_or_file_mutation(self):
        self.prepare();root=self.desktop.game();path=root/'desktop-files.json'
        before=(root/'Base/Bin/Settlers7R.exe').read_bytes()
        for name in ('../outside','/absolute','Base/../../outside','./LocalData/secret','LocalData/secret','base/x:stream'):
            self.desktop.write(path,{'format':backend.FORMAT,'owner':self.desktop.owner,'files':{name:'0'*64}})
            with patch.object(subprocess,'run') as child:
                with self.assertRaises(ValueError):self.desktop.cleanup_instance(root.parent,root.name)
                child.assert_not_called()
            self.assertEqual((root/'Base/Bin/Settlers7R.exe').read_bytes(),before)

    def test_active_hub_prevents_removing_that_managed_copy(self):
        self.prepare();root=self.desktop.game()
        owned=str(root/'Runtime/python/python.exe')
        with patch.object(subprocess,'run',return_value=SimpleNamespace(returncode=0,stdout=json.dumps([owned]).encode())):
            with self.assertRaisesRegex(ValueError,'managed_copy_busy'):self.desktop.cleanup_instance(root.parent,root.name)
        self.assertTrue((root/'Base/Bin/Settlers7R.exe').exists())

    def test_invalid_installation_marker_and_request_schemas_are_not_adopted(self):
        self.prepare();root=self.desktop.game();(root/'desktop-owner.json').write_bytes(b'{}')
        with self.assertRaises(ValueError):self.desktop.installed()
        for value in ({},{'format':backend.REQUEST,'action':'anything','data':{},'extra':True},
                      {'format':backend.REQUEST,'action':'install','data':{'source':True}}):
            with self.assertRaises(ValueError):backend.dispatch(self.desktop,value)

    def test_diagnostics_include_only_aggregate_memory_fields(self):
        self.prepare();root=self.desktop.game()
        self.desktop.write(self.desktop.state/'graphics-probe.json',dict(self.probe,devices=['Fixture GPU'],failure=None))
        self.desktop.write(root/'LocalData/desktop-memory-summary.json',{'samples':12,'peak_private_mib':400,
                           'private_path':'DO NOT EXPORT','payload':'DO NOT EXPORT','monitor_failed':False})
        value=self.desktop.diagnostics();encoded=json.dumps(value)
        self.assertNotIn('DO NOT EXPORT',encoded);self.assertNotIn(str(self.fixture.root),encoded)
        self.assertEqual(value['memory_summary']['samples'],12)

    def test_probe_json_rejects_non_names_and_invalid_cache_records(self):
        for changes in ({'devices':[None]},{'devices':[True]},{'devices':[],'failure':None},
                        {'probe_sha256':'wrong'},{'adapters':['bad\nname']}):
            with self.assertRaises(ValueError):backend.Desktop.validate_probe(dict(self.probe,**changes))

    def test_backend_only_updates_keep_game_contract_but_copied_helpers_require_update(self):
        self.prepare();before=self.desktop.package_id()
        package=self.desktop.package
        def write_manifest():
            value=json.loads((package/'package.json').read_bytes())
            value['files']=content.inventory(package,('Payload','Runtime','Tools'))
            (package/'package.json').write_bytes(graphics.encoded(value))
        (package/'Tools/installer/desktop_backend.py').write_bytes(b'new desktop-only backend')
        write_manifest()
        self.assertEqual(self.desktop.package_id(),before)
        self.assertFalse(self.desktop.snapshot()['update_available'])
        (package/'Tools/offline-ui/launch_local.py').write_bytes(b'new copied game launcher')
        write_manifest()
        self.assertNotEqual(self.desktop.package_id(),before)
        self.assertTrue(self.desktop.snapshot()['update_available'])

    def test_explicit_metadata_recovery_backs_up_unknown_record_without_touching_games(self):
        self.prepare();root=self.desktop.game();before=(root/'Base/Bin/Settlers7R.exe').read_bytes()
        record=self.desktop.state/'installation.json';record.write_bytes(b'foreign or damaged record')
        result=self.desktop.reset_installation()
        self.assertFalse(result['installed'])
        backups=list(self.desktop.state.glob('installation-recovery-*.json'))
        self.assertEqual(len(backups),1);self.assertEqual(backups[0].read_bytes(),b'foreign or damaged record')
        self.assertEqual((root/'Base/Bin/Settlers7R.exe').read_bytes(),before)

    def test_bad_lan_selection_is_rejected_before_probe_or_game_start(self):
        for request in ({},{'action':'join','network':'10.1.2.3','room':'not-a-room','player_name':'Player'},
                        {'action':'create','network':True,'room':None,'player_name':'Player'}):
            with patch.object(self.desktop,'game') as game:
                with self.assertRaises(ValueError):self.desktop.play(request)
                game.assert_not_called()

    def test_copy_cancel_reclaims_only_unchanged_files_created_by_this_operation(self):
        destination=self.fixture.destination
        def cancel(phase,current,total):
            if phase=='copy':
                (destination/'keep-personal.txt').write_bytes(b'unknown personal file')
                raise backend.Cancelled('cancelled')
        with self.assertRaises(backend.Cancelled):
            install.prepare(self.desktop.package,self.fixture.source,destination,emit=lambda _:None,
                            progress=cancel,publish_starters=False)
        self.assertEqual((destination/'keep-personal.txt').read_bytes(),b'unknown personal file')
        self.assertEqual([p for p in (destination/'Base').rglob('*') if p.is_file()],[])
        self.assertTrue((self.fixture.source/'Base/Bin/Settlers7R.exe').is_file())

    def test_duplicate_active_version_cannot_be_cleaned_as_an_older_copy(self):
        self.prepare();value=self.desktop.installed();value['previous']=[{'container':value['container'],'instance':value['current']}]
        self.desktop.write(self.desktop.state/'installation.json',value)
        with self.assertRaisesRegex(ValueError,'installation_invalid'):self.desktop.installed()

    def test_pending_graphics_transaction_is_recovered_by_profile_api(self):
        self.prepare();root=self.desktop.game();folder=root/graphics.STATE;folder.mkdir(parents=True)
        (folder/'transaction.json').write_bytes(b'pending fixture')
        with patch.object(graphics,'restore') as restore:
            self.assertEqual(backend.Desktop.renderer(self.desktop,root,'native',self.probe),'native')
            restore.assert_called_once_with(root)

    def test_lan_desktop_passes_only_name_and_saved_settings_to_automatic_launcher(self):
        self.prepare()
        with patch.object(self.desktop,'align_lan') as align,patch.object(backend.launch_auto_lan,'run',return_value=0) as run,\
                patch.object(content,'verify') as verify:
            self.assertEqual(self.desktop.play({'player_name':'Tester'}),{'exit_code':0})
            align.assert_called_once_with({'player_name':'Tester'});verify.assert_not_called()
            args=run.call_args.args[0]
            self.assertEqual(args.player_name,'Tester');self.assertTrue(args.saved_graphics)
            self.assertTrue(callable(args.progress));self.assertTrue(callable(args.cancel_check))
            self.assertEqual(run.call_args.kwargs,{})

    def test_lan_renderer_alignment_uses_only_known_contracts_and_error_codes_are_localizable(self):
        import lan_autoconnect
        self.prepare();root=self.desktop.game();other='d'*64
        with patch.object(backend,'enumerate_interfaces',return_value=[]),patch.object(backend,'LanDiscovery'),\
                patch.object(self.desktop,'alternatives',return_value={'native':other}),\
                patch.object(lan_autoconnect,'discover_preferred',return_value=SimpleNamespace(content_id=other)) as discover:
            self.desktop.align_lan({'player_name':'Tester'})
            self.assertEqual(discover.call_args.kwargs['acceptable_content_ids'],(other,))
            self.assertEqual(self.desktop.renderer.call_args.args[1],'native')
        for code in ('lan_unreachable','lan_version','lan_multiple','lan_full','lan_changing'):
            self.assertEqual(backend.error_code(lan_autoconnect.LanConnectionError(code,'private details')),code)

    def test_firewall_cleanup_is_owned_hash_guarded_and_aborts_before_game_deletion(self):
        self.prepare();root=self.desktop.game();helper=root/'Launcher/Configure-LAN-Firewall.ps1'
        helper.write_bytes(b'fixture helper');self.desktop.record_ownership(root,content.create(root))
        key=hashlib.sha256(str(root).casefold().encode()).hexdigest()[:16].upper()
        self.desktop.write(root/'LocalData/desktop-firewall.json',{'format':'s7-private-firewall-v1','key':key})
        with patch.object(subprocess,'run',side_effect=[SimpleNamespace(returncode=0,stdout=b'[]'),
                SimpleNamespace(returncode=1,stdout=b'')]) as child:
            with self.assertRaisesRegex(ValueError,'firewall_remove_failed'):self.desktop.cleanup_instance(root.parent,root.name)
            self.assertEqual(child.call_count,2)
            self.assertIn('-Remove',child.call_args.args[0])
        self.assertTrue((root/'Base/Bin/Settlers7R.exe').exists())
        helper.write_bytes(b'unknown modification')
        with patch.object(subprocess,'run',return_value=SimpleNamespace(returncode=0,stdout=b'[]')) as child:
            with self.assertRaisesRegex(ValueError,'ownership_invalid'):self.desktop.cleanup_instance(root.parent,root.name)
            self.assertEqual(child.call_count,1)


class ProbeTests(unittest.TestCase):
    def test_probe_timeout_falls_back_and_transient_failures_are_retried(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as folder:
            app=Path(folder);desktop=backend.Desktop(app)
            package=app/'Package'
            (package/'Payload/Graphics').mkdir(parents=True)
            dll=package/'Payload/Graphics/d3d9.dll';dll.write_bytes(b'pinned DLL fixture')
            probe=package/'Payload/Graphics/S7GraphicsProbe.exe';probe.write_bytes(b'probe fixture')
            expected={'files':{'payload/graphics/s7graphicsprobe.exe':content.digest(probe)}}
            with patch.object(install,'package_manifest',return_value=expected), \
                    patch.object(graphics,'DXVK_SHA256',content.digest(dll)),patch.object(graphics,'ensure_closed'), \
                    patch.object(desktop,'displays',return_value=[{'adapter':'Fixture GPU'}]), \
                    patch.object(memory_guard,'require_host_headroom'), \
                    patch.object(subprocess,'run',side_effect=[subprocess.TimeoutExpired('owned probe',25),
                         SimpleNamespace(returncode=0,stdout=b'{"format":"s7-render-probe-v1","devices":["Fixture GPU"]}')]) as child:
                first=desktop.probe();self.assertEqual(first['devices'],[]);self.assertEqual(first['failure'],'probe_failed')
                second=desktop.probe();self.assertEqual(second['devices'],['Fixture GPU']);self.assertIsNone(second['failure'])
                self.assertEqual(child.call_count,2)
                self.assertEqual(child.call_args.kwargs['timeout'],25)
                third=desktop.probe();self.assertEqual(third,second);self.assertEqual(child.call_count,2)


class QualityTests(unittest.TestCase):
    def test_allowlist_preserves_bom_endian_comments_and_unrelated_values(self):
        text='; header\r\n[quality]\r\nTextureQuality = 0 ; note\r\nFullScreen = 0\r\n[Effects]\r\nShadows = 0\r\n[Other]\r\nValue = keep\r\n'
        for bom,encoding in ((b'','utf-8'),(b'\xef\xbb\xbf','utf-8'),(b'\xff\xfe','utf-16-le'),(b'\xfe\xff','utf-16-be')):
            after=quality.replace(bom+text.encode(encoding),quality.DEFAULTS)
            self.assertTrue(after.startswith(bom));decoded=after[len(bom):].decode(encoding)
            self.assertIn('TextureQuality = 1 ; note',decoded);self.assertIn('FullScreen = 0',decoded)
            self.assertIn('[Other]\r\nValue = keep',decoded)
            self.assertEqual(quality.replace(after,quality.DEFAULTS),after)

    def test_ambiguous_or_untrusted_ini_and_preferences_are_rejected(self):
        for raw in (b'[quality]\n[Quality]\n',b'[Quality]\nTextureQuality=0\ntexturequality=2\n',
                    b'[Effects]\nShadows=2\n',b'[Quality]\nTextureQuality=no\n',b'x'*65537):
            with self.assertRaises(ValueError):quality.replace(raw,quality.DEFAULTS)
        for changes in ({'TextureQuality':True},{'SSAO':2},{'Antialiasing':'off'},{'FPS':60}):
            with self.assertRaises(ValueError):quality.validate(dict(quality.DEFAULTS,**changes))


if __name__=='__main__':unittest.main()
