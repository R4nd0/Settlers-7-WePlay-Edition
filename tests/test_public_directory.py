import http.client
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock,patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/installer'))
import discord_directory
import public_directory
import desktop_backend
import game_directory

ORIGIN='https://weplay-multiplayer.testing.workers.dev'
CONTENT='a'*64


class PublicHostingTests(unittest.TestCase):
    def test_central_default_migrates_legacy_reader_but_retains_new_explicit_alternative(self):
        with tempfile.TemporaryDirectory(prefix='public-directory-test-',dir=ROOT/'work') as temporary:
            root=Path(temporary);state=root/'state';state.mkdir()
            previous='https://weplay-multiplayer.previous.workers.dev'
            custom='https://weplay-multiplayer.other.workers.dev'
            profile=state/'game-directory.json';default=root/'directory-service.json'
            profile.write_text(json.dumps(dict(format=game_directory.FORMAT,origin=previous)))
            default.write_text(json.dumps(dict(format='weplay-directory-service-v1',origin=ORIGIN)))
            with patch.object(game_directory,'__file__',str(root/'game_directory.py')):
                self.assertEqual(game_directory.configured(state),ORIGIN)
                profile.write_text(json.dumps(dict(format=game_directory.FORMAT,origin=custom,explicit_override=True)))
                self.assertEqual(game_directory.configured(state),custom)
                profile.write_text(json.dumps(dict(format=game_directory.FORMAT,origin=previous)))
                default.write_text(json.dumps(dict(format='weplay-directory-service-v1',origin=None)))
                self.assertEqual(game_directory.configured(state),previous)
                profile.write_text(json.dumps(dict(format=game_directory.FORMAT,origin=custom,explicit_override='yes')))
                with self.assertRaisesRegex(ValueError,'directory_invalid'):game_directory.configured(state)

    def test_public_reporter_never_reads_discord_credentials_and_uses_public_routes(self):
        connection=Mock();connection.getresponse.return_value=Mock(status=200,read=Mock(return_value=b'{"ok":true}'))
        factory=Mock(return_value=connection)
        transport=Mock(return_value=dict(format='s7-lobby-games-v1',content_id=CONTENT,games=[
            dict(id='00000001',title='Public game',state='open',players=1,capacity=4,free_slots=3)]))
        clock=[0]
        with patch.object(public_directory,'configured',return_value=ORIGIN),patch.object(discord_directory,'load',side_effect=AssertionError('Discord credentials must not be read')):
            reporter=public_directory.PublicReporter(ROOT/'work/public-host-fixture',Mock(address='10.203.7.1'),'10.203.7.1',CONTENT,
                transport=transport,connection_factory=factory,clock=lambda:clock[0])
            key=reporter.key;reporter.poll();clock[0]=61;reporter.poll();reporter.close()
        paths=[call.args[1] for call in connection.request.call_args_list]
        self.assertEqual(paths.count('/v1/directory/register'),2)
        self.assertNotIn('/v1/report',paths)
        self.assertTrue(all(path in ('/v1/directory/register','/v1/directory/report') for path in paths))
        registrations=[json.loads(call.kwargs['body']) for call in connection.request.call_args_list if call.args[1].endswith('/register')]
        self.assertEqual(registrations,[dict(format='weplay-public-host-v1',token=key)]*2)
        self.assertEqual(reporter.key,'')

    def test_public_registration_refuses_absent_service_and_invalid_response(self):
        with patch.object(public_directory,'configured',return_value=None),patch.object(public_directory,'request') as request:
            with self.assertRaisesRegex(ValueError,'directory_not_connected'):
                public_directory.PublicReporter(ROOT/'work/public-host-fixture',Mock(),'127.0.0.1',CONTENT)
            request.assert_not_called()
        with patch.object(public_directory,'configured',return_value=ORIGIN),patch.object(public_directory,'request',return_value={'ok':True,'token':'unexpected'}):
            with self.assertRaisesRegex(ValueError,'directory_invalid'):
                public_directory.PublicReporter(ROOT/'work/public-host-fixture',Mock(),'127.0.0.1',CONTENT)

    def test_public_host_request_retains_consent_and_does_not_require_discord(self):
        backend=object.__new__(desktop_backend.Desktop)
        backend.package=ROOT/'work/public-package-fixture';backend.state=ROOT/'work/public-state-fixture'
        backend.validate_lan=Mock();backend.play=Mock(return_value={'exit_code':0});backend.progress=Mock()
        channel=Mock();channel.__enter__=Mock(return_value=channel);channel.__exit__=Mock(return_value=False);channel.read.return_value=None
        request=dict(player_name='Tester',channel='private-fixture',mode='assisted',direct_endpoint=None,
            publish_discord=False,public_lobby=True,internet_consent=1)
        import internet_private_channel,discord_credentials
        with patch.object(desktop_backend.install,'package_manifest',return_value={'internet_easytier':True,'game_directory':True,'public_game_directory':True}),\
             patch.object(internet_private_channel,'PrivateChannel',return_value=channel),\
             patch.object(discord_credentials,'connected',side_effect=AssertionError('Discord must be optional')):
            self.assertEqual(backend.internet_play(request,host=True),{'exit_code':0})
            self.assertTrue(backend.play.call_args.kwargs['internet']['public_lobby'])
            self.assertFalse(backend.play.call_args.kwargs['internet']['publish_discord'])
            for value in (None,False,True,0,'1'):
                with self.assertRaisesRegex(ValueError,'internet_consent_required'):
                    backend.internet_play(dict(request,internet_consent=value),host=True)
            with self.assertRaisesRegex(ValueError,'request_invalid'):
                backend.internet_play(dict(request,public_lobby='yes'),host=True)


if __name__=='__main__':unittest.main()
