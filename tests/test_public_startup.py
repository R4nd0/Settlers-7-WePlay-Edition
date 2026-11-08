"""Exercise shipped host orchestrators and their actual callback order together.

Only machine boundaries (files, renderer, network admission and game process)
are replaced here. Neither launch_internet nor launch_auto_lan is simulated.
Live directory/native-client qualification is recorded separately.
"""
from contextlib import ExitStack, nullcontext
from pathlib import Path
from types import SimpleNamespace
import sys
import os
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT=Path(__file__).resolve().parents[1]
TOOLS=Path(os.environ.get('S7_TEST_PACKAGE_TOOLS',ROOT/'tools'))
sys.path[:0]=[str(TOOLS/'installer'),str(TOOLS/'offline-ui'),str(TOOLS/'linux')]
if sys.platform=='win32':
    import desktop_backend
else:
    desktop_backend=None
import discord_directory
import game_directory
import launch_auto_lan as lan
import launch_internet as internet
import linux_network
import public_directory
from internet_room import Room
from lobby_directory import LanInterface

CONTENT='a'*64
ROOM=Room('a'*32,'b'*64,'10.203.77.0/24')


class PublicStartupTests(unittest.TestCase):
    platforms=('windows','linux') if desktop_backend is not None else ('linux',)
    def exercise(self,kind,*,configuration_error=False,preflight_error=False,publish_error=False,
                 directory_connection=None,native_actions=None):
        with tempfile.TemporaryDirectory(dir=ROOT/'work') as temporary,ExitStack() as stack:
            root=Path(temporary);(root/'LocalData').mkdir()
            interface=LanInterface(ROOM.coordinator,24,'owned-fixture')
            overlay=Mock();overlay.start.return_value=interface;overlay.snapshot.return_value=(interface,{})
            directory=Mock();directory.list_rooms.return_value=[Mock(endpoint=Mock(host=ROOM.coordinator),compatible=Mock(return_value=True))]
            events=[];reporters=[];native_rows=[]
            def admitted():events.append('admitted')
            membership=Mock();membership.start.side_effect=admitted
            def invite(value):
                self.assertIn('admitted',events);self.assertEqual(value,ROOM.token());events.append('invitation_ready')
            connection=Mock();connection.getresponse.return_value=Mock(status=200,read=Mock(return_value=b'{"ok":true}'))
            real_reporter=public_directory.PublicReporter
            def reporter(*args,**kwargs):
                self.assertIn('admitted',events)
                self.assertNotIn('invitation_ready',events,'internal broker setup must precede externally exposing readiness')
                if publish_error:raise ValueError('registration failed')
                value=real_reporter(*args,**kwargs,connection_factory=directory_connection or Mock(return_value=connection),
                    transport=lambda *a,**kw:dict(format='s7-lobby-games-v1',content_id=CONTENT,games=list(native_rows)))
                reporters.append(value);return value
            real_popen=lan.subprocess.Popen
            def native_start(*args,**kwargs):
                if args[0][0]!='fixture-native-game':return real_popen(*args,**kwargs)
                self.assertEqual(len(reporters),1)
                self.assertIsNotNone(reporters[0].broker,'public game starts without any directory host broker')
                self.assertEqual(reporters[0].broker.room,ROOM)
                self.assertIn('invitation_ready',events);events.append('native_start')
                if native_actions:native_actions(reporters[0],native_rows)
                return Mock(poll=Mock(return_value=0))
            def patcher(target,name,**kwargs):return stack.enter_context(patch.object(target,name,**kwargs))
            patcher(internet,'enumerate_interfaces',return_value=[LanInterface('192.168.1.7',24)])
            patcher(internet,'create',return_value=ROOM)
            patcher(internet,'LanDiscovery',return_value=directory)
            patcher(internet,'request',return_value={'active_members':0})
            real_run=internet.run
            patcher(internet,'run',side_effect=lambda args,**kwargs:real_run(args,overlay_factory=lambda *a,**kw:overlay,**kwargs))
            patcher(lan,'session_lock',side_effect=lambda *a:nullcontext(root/'LocalData'))
            patcher(lan.compatibility,'verify',return_value={})
            patcher(lan.compatibility,'network_id',return_value=CONTENT)
            patcher(lan.launch_local,'verify_bundle')
            patcher(lan.launch_local,'saved_graphics_environment',return_value={})
            patcher(linux_network.linux_runtime,'graphics_environment',return_value={})
            patcher(lan.launch_local,'game_launch_environment',return_value={})
            patcher(lan.launch_local,'graphics_game_arguments',return_value=[])
            patcher(lan.player_settings,'save')
            patcher(lan,'Membership',return_value=membership)
            patcher(lan.launch_lan,'remote_configuration',**({'side_effect':OSError('coordinator unreachable')} if configuration_error else {'return_value':{'content_id':CONTENT}}))
            patcher(lan.launch_local,'write_configuration',return_value=None)
            patcher(lan.host_runtime,'game_command',return_value=(['fixture-native-game'],{}))
            popen=patcher(lan.subprocess,'Popen',side_effect=native_start)
            patcher(lan.launch_local,'wait_for_graphics_game',return_value=0)
            stack.enter_context(patch.object(discord_directory.Reporter,'start',lambda self:self))
            if kind=='windows':
                patcher(public_directory,'PublicReporter',side_effect=reporter)
                desktop=object.__new__(desktop_backend.Desktop)
                desktop.app=root;desktop.package=root/'Package';desktop.state=root/'State'
                # This orchestrator fixture intentionally has no real package;
                # integrity/operation-scope tests exercise constructed Desktops.
                desktop.verification=lambda:nullcontext()
                desktop.package_manifest=lambda:desktop_backend.install.package_manifest(desktop.package)
                for name,value in {'game':root,'installed':{'package_id':'fixture','renderer_policy':'native'},
                    'package_id':'fixture','probe':{},'renderer':'native','default_choice':{}}.items():setattr(desktop,name,Mock(return_value=value))
                for name in ('progress','emit','validate_lan','align_internet'):setattr(desktop,name,Mock())
                desktop.read=Mock(return_value={'version':'2.0.0-rc3.19'})
                desktop.check_start_memory=Mock(side_effect=OSError('preflight failed') if preflight_error else None)
                patcher(desktop_backend.install,'package_manifest',return_value={'game_directory':True})
                patcher(desktop_backend.graphics,'ensure_closed')
                patcher(desktop_backend.graphics,'session_lock',return_value=nullcontext())
                patcher(desktop_backend.choice,'_load_choice',return_value=None)
                patcher(desktop_backend.choice,'save_choice')
                run=lambda:desktop.play({'player_name':'Fixture'},internet=dict(invitation=None,mode='assisted',direct_endpoint=None,
                    public_lobby=True,publish_discord=False,internet_consent=1),invitation_ready=invite)
            else:
                patcher(linux_network,'PublicReporter',side_effect=reporter)
                run=lambda:linux_network.run(root,'public-host',player_name='Fixture',consent=True,invitation_ready=invite)
            if configuration_error or preflight_error or publish_error:
                with self.assertRaises((OSError,ValueError)):run()
                popen.assert_not_called();self.assertNotIn('invitation_ready',events)
            else:
                self.assertEqual(run(),{'exit_code':0} if kind=='windows' else 0)
                self.assertIn('native_start',events)
            membership.close.assert_called_once()
            overlay.close.assert_called_once()
            return reporters,events

    @unittest.skipIf(desktop_backend is None,'Windows desktop backend is tested by the Windows job')
    def test_windows_public_host_has_broker_before_native_start(self):self.exercise('windows')
    def test_linux_public_host_has_broker_before_native_start(self):self.exercise('linux')
    def test_unreachable_coordinator_exposes_no_room_or_game(self):
        for kind in self.platforms:
            with self.subTest(kind=kind):self.exercise(kind,configuration_error=True)
    def test_failed_public_registration_exposes_no_room_or_game(self):
        for kind in self.platforms:
            with self.subTest(kind=kind):self.exercise(kind,publish_error=True)
    @unittest.skipIf(desktop_backend is None,'Windows preflight is tested by the Windows job')
    def test_failed_preflight_exposes_no_room_or_game(self):self.exercise('windows',preflight_error=True)


if __name__=='__main__':unittest.main()
