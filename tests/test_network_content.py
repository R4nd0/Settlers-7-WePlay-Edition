"""Pure filesystem/protocol tests; no installed game, sockets or child processes."""
import copy
from contextlib import redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/installer'))
import compatibility as content
import desktop_backend as backend
import graphics_profile as graphics
import game_directory
import lan_autoconnect
import lan_hub
import launch_auto_lan
from lobby_directory import CONTROL_FORMAT, MAX_MEMBERS, LanInterface, LobbyEndpoint, LobbyRoom


def manifest(files):
    return {'format':content.FORMAT, 'files':files, 'content_id':content.fingerprint(files)}


class NetworkContentTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.directory=Path(self.temp.name)
        self.files={name:hashlib.sha256(name.encode()).hexdigest() for name in (
            'base/bin/settlers7r.exe','base/bin/uplay_r1_loader.dll','base/bba/interface.bba',
            'base/bba/map.bba','server/s7patcher.dll','launcher/start.py')}

    def test_only_two_exact_auxiliary_paths_do_not_change_network_identity(self):
        bare=manifest(dict(self.files))
        aux=dict(self.files)
        for name in content.NETWORK_AUXILIARIES:aux[name]='a'*64
        supplied=manifest(aux)
        self.assertNotEqual(bare['content_id'],supplied['content_id'])
        self.assertEqual(content.network_id(bare),content.network_id(supplied))
        for name in content.NETWORK_AUXILIARIES:aux[name]='b'*64
        self.assertEqual(content.network_id(bare),content.network_id(manifest(aux)))

    def test_every_game_asset_changes_identity_but_host_tools_are_platform_specific(self):
        expected=content.network_id(manifest(self.files))
        for name in self.files:
            changed=dict(self.files);changed[name]='f'*64
            with self.subTest(file=name):
                if name.startswith('base/'):
                    self.assertNotEqual(expected,content.network_id(manifest(changed)))
                else:
                    self.assertEqual(expected,content.network_id(manifest(changed)))
        for name in ('base/support/custom.dll','base/bin/vc_redist.x64.exe',
                     'base/support/vc_redist.x64.exe.bak'):
            with self.subTest(extra=name):
                self.assertNotEqual(expected,content.network_id(manifest(dict(self.files,**{name:'c'*64}))))

    def test_network_contract_is_domain_separated_and_does_not_mutate_manifest(self):
        value=manifest(dict(self.files));before=copy.deepcopy(value)
        expected=hashlib.sha256(json.dumps({'format':'s7-play-content-v3','protocol':'prudp7-neo3',
            'files':{name:digest for name,digest in self.files.items() if name.startswith('base/')}},
            sort_keys=True,separators=(',',':'),ensure_ascii=True).encode('ascii')).hexdigest()
        self.assertEqual(content.network_id(value),expected)
        self.assertNotEqual(value['content_id'],expected)
        self.assertEqual(value,before)

    def test_invalid_full_manifest_and_empty_gameplay_are_rejected(self):
        value=manifest(dict(self.files));value['content_id']='0'*64
        with self.assertRaises(ValueError):content.network_id(value)
        value=manifest({next(iter(content.NETWORK_AUXILIARIES)):'a'*64})
        with self.assertRaises(ValueError):content.network_id(value)

    def test_explicit_full_file_comparison_reports_auxiliary_differences_and_keeps_exit_code(self):
        bare,left=self.root('compare-native');root,right=self.root('compare-aux',aux=True)
        output=io.StringIO()
        with patch.object(sys,'argv',['compatibility.py','--bundle',str(bare),'--compare',str(root/'compatibility.json')]), \
                redirect_stdout(output):
            result=content.main()
        self.assertEqual(result,2)
        self.assertIn('base/support/vc_redist.x64.exe',output.getvalue())
        self.assertIn('Netzwerkkennung stimmt ueberein',output.getvalue())

    def root(self,name,aux=False):
        root=self.directory/name
        data={'Base/Bin/Settlers7R.exe':b'invented game','Base/Bin/uplay_r1_loader.dll':b'invented adapter',
              'Base/BBA/Interface.bba':b'invented UI','Server/S7Patcher.dll':b'invented service',
              'Launcher/start.py':b'invented launcher'}
        if aux:data['Base/Support/vc_redist.x64.exe']=b'invented auxiliary installer'
        for relative,raw in data.items():
            target=root/relative;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(raw)
        value=content.create(root)
        (root/'compatibility.json').write_text(json.dumps(value))
        return root,value

    def test_full_local_verification_still_checks_changed_or_missing_auxiliary_file(self):
        root,value=self.root('full',aux=True);expected=content.network_id(value)
        path=root/'Base/Support/vc_redist.x64.exe';path.write_bytes(b'changed')
        self.assertEqual(content.network_id(content.create(root)),expected)
        with self.assertRaises(ValueError):content.verify(root)
        path.unlink()
        with self.assertRaises(ValueError):content.verify(root)

    def test_failed_local_auxiliary_check_prevents_discovery_and_game_start(self):
        root,value=self.root('rejected',aux=True)
        (root/'Base/Support/vc_redist.x64.exe').write_bytes(b'changed')
        network=Mock()
        with patch.object(launch_auto_lan,'enumerate_interfaces') as interfaces, \
                patch.object(launch_auto_lan.subprocess,'Popen') as child:
            with self.assertRaises(ValueError):
                launch_auto_lan.run(SimpleNamespace(bundle=root,check_only=False),network=network)
        interfaces.assert_not_called();network.assert_not_called();child.assert_not_called()

    def test_launcher_election_and_membership_share_canonical_identity(self):
        identities=[]
        for enabled in (False,True):
            root,value=self.root('launch-'+str(enabled),aux=enabled)
            expected=content.network_id(value);identities.append(expected)
            interface=LanInterface('192.168.1.10',24)
            room=LobbyRoom('a'*32,'Fixture',expected,LobbyEndpoint('lan-direct','192.168.1.20'),interface)
            runtime=root/'LocalData/fixture-runtime.ini'
            def configure(*args,**kwargs):runtime.write_text('fixture');return runtime
            with patch.object(launch_auto_lan.launch_local,'verify_bundle'), \
                    patch.object(launch_auto_lan.launch_local,'choose_graphics_environment',return_value={}), \
                    patch.object(launch_auto_lan.launch_local,'game_launch_environment',return_value={}), \
                    patch.object(launch_auto_lan.launch_local,'graphics_game_arguments',return_value=()), \
                    patch.object(launch_auto_lan,'enumerate_interfaces',return_value=[interface]), \
                    patch.object(launch_auto_lan.lan_autoconnect,'connect',return_value=room) as election, \
                    patch.object(launch_auto_lan,'Membership') as membership, \
                    patch.object(launch_auto_lan.launch_lan,'remote_configuration',return_value={'content_id':expected}), \
                    patch.object(launch_auto_lan.launch_local,'write_configuration',side_effect=configure), \
                    patch.object(launch_auto_lan.launch_local,'wait_for_graphics_game',return_value=0), \
                    patch.object(launch_auto_lan.subprocess,'Popen'):
                self.assertEqual(launch_auto_lan.run(SimpleNamespace(bundle=root,check_only=False)),0)
            self.assertEqual(election.call_args.args[1],expected)
            self.assertEqual(membership.call_args.args[-1],expected)
        self.assertEqual(*identities)

    def test_hub_accepts_network_identity_and_retains_service_file_guard(self):
        bare,left=self.root('bare');root,right=self.root('aux',aux=True)
        expected=content.network_id(left)
        lan_hub.verify_service(root,expected)
        with self.assertRaises(ValueError):lan_hub.verify_service(root,right['content_id'])
        (root/'Server/S7Patcher.dll').write_bytes(b'modified service')
        with self.assertRaises(ValueError):lan_hub.verify_service(root,expected)

    def test_renderer_alternatives_are_consistent_for_auxiliary_variants(self):
        bare,left=self.root('native');extra,right=self.root('native-aux',aux=True)
        desktop=backend.Desktop.__new__(backend.Desktop)
        a=desktop.alternatives(bare);b=desktop.alternatives(extra)
        self.assertEqual(a,b)
        self.assertEqual(a['native'],content.network_id(left))
        self.assertEqual(a['native'],a['dxvk'])

    def modern(self,files=None):
        value=manifest(dict(self.files if files is None else files))
        return json.loads(graphics._expected(graphics.encoded(value)))

    def test_pinned_modern_renderer_and_native_share_gameplay_identity_only(self):
        native=manifest(dict(self.files));modern=self.modern()
        self.assertNotEqual(native['content_id'],modern['content_id'])
        self.assertEqual(content.network_id(native),content.network_id(modern))
        self.assertEqual(content.compare(native,modern),[graphics.DLL.lower(),graphics.CONF.lower()])
        self.assertEqual(content.LOCAL_GRAPHICS_PROFILE,graphics.PROFILE)
        self.assertEqual(content.LOCAL_GRAPHICS_FILES,{graphics.DLL.lower():graphics.DXVK_SHA256,
            graphics.CONF.lower():hashlib.sha256(graphics.CONFIG).hexdigest()})

    def test_previous_renderer_coupled_network_domain_is_not_accepted(self):
        files={name:value for name,value in self.files.items() if name.startswith('base/')}
        old=content.fingerprint({'format':'s7-play-content-v2','protocol':content.NETWORK_PROTOCOL,'files':files})
        self.assertNotEqual(content.network_id(manifest(dict(self.files))),old)

    def test_unknown_or_incomplete_renderer_pair_is_never_normalized(self):
        expected=content.network_id(manifest(dict(self.files)))
        for name in (graphics.DLL.lower(),graphics.CONF.lower()):
            for operation in ('change','remove'):
                value=self.modern()
                if operation=='change':value['files'][name]='c'*64
                else:value['files'].pop(name)
                value['content_id']=content.fingerprint(value['files'])
                with self.subTest(path=name,operation=operation):
                    self.assertNotEqual(content.network_id(value),expected)
        for profile in (None,'unknown-profile'):
            value=self.modern();value['graphics_profile']=profile
            self.assertNotEqual(content.network_id(value),expected)

    def test_modern_normalization_keeps_every_game_asset_and_nearby_proxy_path(self):
        expected=content.network_id(self.modern())
        for name in (name for name in self.files if name.startswith('base/')):
            files=dict(self.files);files[name]='c'*64
            with self.subTest(path=name):self.assertNotEqual(content.network_id(self.modern(files)),expected)
        for name in ('base/bin/dxgi.dll','base/bin/d3d9.dll.bak','base/other/d3d9.dll'):
            files=dict(self.files);files[name]='c'*64
            with self.subTest(path=name):self.assertNotEqual(content.network_id(self.modern(files)),expected)

    def test_full_verification_still_rejects_tampered_normalized_renderer(self):
        root,value=self.root('renderer-tamper')
        dll=root/graphics.DLL;conf=root/graphics.CONF
        dll.write_bytes(b'invented renderer');conf.write_bytes(graphics.CONFIG)
        expected=content.create(root);expected['graphics_profile']=graphics.PROFILE
        (root/'compatibility.json').write_bytes(graphics.encoded(expected))
        pins={graphics.DLL.lower():content.digest(dll),graphics.CONF.lower():content.digest(conf)}
        with patch.object(content,'LOCAL_GRAPHICS_FILES',pins):
            self.assertEqual(content.network_id(expected),content.network_id(value))
            content.verify(root)
            dll.write_bytes(b'modified renderer')
            with self.assertRaises(ValueError):content.verify(root)
            conf.unlink()
            with self.assertRaises(ValueError):content.verify(root)

    def test_directory_uses_network_identity_without_accepting_legacy_full_id(self):
        root,value=self.root('directory',aux=True)
        desktop=backend.Desktop.__new__(backend.Desktop)
        desktop.package=self.directory/'package';desktop.state=self.directory/'state'
        desktop.game=Mock(return_value=root)
        rows=[{'id':'new','content_id':content.network_id(value)},
              {'id':'modern','content_id':content.network_id(self.modern(value['files']))},
              {'id':'legacy','content_id':value['content_id']}]
        with patch.object(backend.install,'package_metadata',return_value={'game_directory':True}), \
                patch.object(game_directory,'configured',return_value='https://fixture.invalid'), \
                patch.object(game_directory,'listing',return_value=rows):
            result=desktop.directory_list()
        self.assertEqual([row['compatible'] for row in result['games']],[True,True,False])

    def test_gui_preconnect_uses_the_same_contract_as_game_launcher(self):
        root,value=self.root('preconnect',aux=True)
        desktop=backend.Desktop.__new__(backend.Desktop)
        desktop.game=Mock(return_value=root);desktop.progress=Mock();desktop.renderer=Mock()
        desktop.installed=Mock(return_value={'renderer_policy':'auto'});desktop.probe=Mock()
        with patch.object(backend,'enumerate_interfaces',return_value=[]), \
                patch.object(backend,'LanDiscovery'), \
                patch.object(lan_autoconnect,'discover_preferred',return_value=None) as discover:
            desktop.align_lan({'player_name':'Fixture'})
        self.assertEqual(discover.call_args.args[1],content.network_id(value))
        self.assertNotIn('acceptable_content_ids',discover.call_args.kwargs)

    def test_legacy_group_is_rejected_instead_of_silently_elected(self):
        expected=content.network_id(manifest(self.files))
        interface=LanInterface('192.168.1.10',24)
        room=LobbyRoom('a'*32,'Fixture',manifest(self.files)['content_id'],
                       LobbyEndpoint('lan-direct','192.168.1.20'),interface)
        directory=Mock();directory.list_rooms.return_value=[room]
        response={'format':CONTROL_FORMAT,'content_id':room.content_id,
                  'members':2,'active_members':1,'max_members':MAX_MEMBERS}
        with patch.object(lan_autoconnect,'request',return_value=response):
            with self.assertRaises(lan_autoconnect.LanConnectionError) as caught:
                lan_autoconnect.discover_preferred(directory,expected)
        self.assertEqual(caught.exception.code,'lan_version')


if __name__=='__main__':unittest.main()
