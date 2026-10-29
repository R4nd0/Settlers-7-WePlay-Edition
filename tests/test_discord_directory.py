import base64
import ctypes
import json
import os
from pathlib import Path
import sys
import unittest
import uuid
from unittest.mock import Mock,patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/installer'))
import discord_credentials as credentials
import discord_directory as directory
from lobby_directory import LanInterface
import desktop_backend
import internet_private_channel

ORIGIN='https://weplay-multiplayer.testing.workers.dev'
KEY='s'*43
CONTENT='a'*64


def connect_code(value):return 'weplay-connect1.'+base64.urlsafe_b64encode(json.dumps(value).encode()).decode().rstrip('=')
def game(**changes):return dict(id='00000001',title='Community',state='open',players=1,capacity=4,free_slots=3,**changes)
def status(rows=None):return dict(format='s7-lobby-games-v1',content_id=CONTENT,games=[game()] if rows is None else rows)


class CredentialTests(unittest.TestCase):
    def test_connection_code_binds_only_publisher_key_to_exact_tls_origin(self):
        self.assertEqual(credentials.parse(connect_code(dict(v=1,origin=ORIGIN,key=KEY))),(ORIGIN,KEY))
        for url in ('http://weplay-multiplayer.testing.workers.dev',ORIGIN+'/',ORIGIN+'?secret=x',
                    'https://attacker.invalid','https://weplay-multiplayer.testing.workers.dev.attacker.invalid',
                    'https://user@weplay-multiplayer.testing.workers.dev'):
            with self.subTest(url=url),self.assertRaises(credentials.DiscordError):
                credentials.parse(connect_code(dict(v=1,origin=url,key=KEY)))

    def test_wrong_protocol_secret_fields_and_duplicate_keys_are_rejected(self):
        for value in (dict(v=True,origin=ORIGIN,key=KEY),dict(v=1,origin=ORIGIN,key=KEY,bot_token='no'),
                      dict(v=1,origin=ORIGIN,key='short')):
            with self.assertRaises(credentials.DiscordError):credentials.parse(connect_code(value))
        raw='{"v":1,"v":1,"origin":"'+ORIGIN+'","key":"'+KEY+'"}'
        code='weplay-connect1.'+base64.urlsafe_b64encode(raw.encode()).decode().rstrip('=')
        with self.assertRaises(credentials.DiscordError):credentials.parse(code)
        with self.assertRaises(credentials.DiscordError):credentials.parse('weplay1.private-room-secret')

    def test_credential_target_and_native_layout_are_scoped(self):
        self.assertEqual(ctypes.sizeof(credentials.Credential),80 if ctypes.sizeof(ctypes.c_void_p)==8 else 52)
        target=credentials.target(ORIGIN)
        self.assertTrue(target.startswith('Settlers7WePlay/DiscordPublisher/'))
        self.assertNotIn(KEY,target)
        self.assertNotEqual(target,credentials.target('https://weplay-multiplayer.other.workers.dev'))

    @unittest.skipUnless(os.environ.get('S7_TEST_CREDMAN')=='1','explicit native temporary-credential test')
    def test_real_windows_vault_roundtrip_for_one_own_temporary_credential(self):
        url='https://weplay-multiplayer.'+uuid.uuid4().hex+'.workers.dev'
        key=base64.urlsafe_b64encode(os.urandom(32)).decode().rstrip('=')
        try:
            self.assertIsNone(credentials.get(url))
            credentials.store(url,key)
            self.assertEqual(credentials.get(url),key)
            self.assertEqual(credentials.get(url),key,'reading/zeroing a copy must not erase the stored credential')
        finally:credentials.remove(url)
        self.assertIsNone(credentials.get(url))


class ProjectionTests(unittest.TestCase):
    def test_only_content_bound_whitelisted_game_fields_leave_local_service(self):
        self.assertEqual(directory.native_games(status(),CONTENT),[game()])
        for changed in (dict(status(),content_id='b'*64),dict(status(),peers=['192.168.1.2']),
                        status([dict(game(),network_secret='no')]),status([dict(game(),players=True)]),
                        status([dict(game(),title='weplay1.private')]),status([game(),game()])):
            with self.assertRaises(credentials.DiscordError):directory.native_games(changed,CONTENT)

    def test_full_and_started_are_distinct_and_consistent_with_reported_slots(self):
        for state in ('full','started'):
            value=dict(game(),state=state,free_slots=0)
            self.assertEqual(directory.native_games(status([value]),CONTENT),[value])
        for state in ('open','running','started'):
            bad=dict(game(),state=state,free_slots=0 if state=='open' else 1)
            with self.assertRaises(credentials.DiscordError):directory.native_games(status([bad]),CONTENT)


class ReporterTests(unittest.TestCase):
    def setUp(self):
        self.clock=0.;self.sent=[];self.row=game()
        self.interface=LanInterface('10.203.77.2',24,'WePlay-test')
        self.transport=Mock(side_effect=lambda *args,**kwargs:status([self.row]))
        def connection(host,timeout):
            self.assertEqual(host,'weplay-multiplayer.testing.workers.dev');self.assertEqual(timeout,5)
            result=Mock();result.request.side_effect=lambda *args,**kwargs:self.sent.append((args,kwargs))
            result.getresponse.return_value=Mock(status=200,read=Mock(return_value=b'{"ok":true}'))
            return result
        self.connection=connection
        with patch.object(directory,'load',return_value=(ORIGIN,KEY)):
            self.reporter=directory.Reporter(ROOT/'work/fictional-state',self.interface,'10.203.77.1',CONTENT,
                transport=self.transport,connection_factory=connection,clock=lambda:self.clock)

    def test_poll_source_binding_changed_only_updates_and_sixty_second_heartbeat(self):
        self.reporter.poll();self.assertEqual([x[0][0] for x in self.sent],['POST','PUT'])
        self.transport.assert_called_with('10.203.77.1','10.203.77.2','games',expected=CONTENT,timeout=2)
        self.clock=5;self.reporter.poll();self.assertEqual(len(self.sent),2)
        self.clock=61;self.reporter.poll();self.assertEqual(len(self.sent),3)
        self.clock=66;self.row=dict(game(),state='started',free_slots=0);self.reporter.poll()
        self.assertEqual(len(self.sent),4)
        self.reporter.close();self.assertEqual(self.sent[-1][0][0],'DELETE')
        for args,kwargs in self.sent:
            self.assertEqual(args[1],'/v1/report')
            body=kwargs['body'].decode();self.assertNotIn(KEY,body);self.assertNotIn(self.interface.address,body)
            self.assertEqual(kwargs['headers']['Authorization'],'Bearer '+KEY)

    def test_invalid_service_data_never_opens_remote_connection(self):
        self.row=dict(game(),network_secret='must not leave')
        with self.assertRaises(credentials.DiscordError):self.reporter.poll()
        self.assertEqual(self.sent,[])

    def test_redirects_are_not_followed_with_publisher_credentials(self):
        response=Mock(status=302,read=Mock(return_value=b'{}'))
        connection=Mock(getresponse=Mock(return_value=response))
        with self.assertRaises(credentials.DiscordError):
            directory.check_connection(ORIGIN,KEY,connection_factory=Mock(return_value=connection))
        connection.request.assert_called_once_with('GET','/v1/publisher',headers={'Authorization':'Bearer '+KEY})
        connection.close.assert_called_once()


class BackendPairingTests(unittest.TestCase):
    def test_private_code_is_checked_then_stored_without_entering_public_state_or_events(self):
        from types import SimpleNamespace
        state=ROOT/'work/fictional-state'
        desktop=SimpleNamespace(package=ROOT/'work/fictional-package',state=state,write=Mock())
        channel=Mock();channel.read.return_value=connect_code(dict(v=1,origin=ORIGIN,key=KEY))
        manager=Mock(__enter__=Mock(return_value=channel),__exit__=Mock(return_value=False))
        with patch.object(desktop_backend.install,'package_manifest',return_value={'discord_directory':True}), \
                patch.object(internet_private_channel,'PrivateChannel',return_value=manager), \
                patch.object(directory,'check_connection') as check,patch.object(credentials,'store') as store:
            self.assertEqual(desktop_backend.Desktop.connect_discord(desktop,{'channel':'weplay-internet-'+'c'*32}),{'connected':True})
        check.assert_called_once_with(ORIGIN,KEY);store.assert_called_once_with(ORIGIN,KEY)
        self.assertNotIn(KEY,repr(desktop.write.call_args))
        self.assertEqual(desktop.write.call_args.args[1],dict(format=credentials.FORMAT,origin=ORIGIN,publish_enabled=True))

    def test_failed_validation_never_saves_grant_and_legacy_packages_never_gain_capability(self):
        from types import SimpleNamespace
        desktop=SimpleNamespace(package=ROOT/'work/fictional-package',state=ROOT/'work/fictional-state',write=Mock())
        channel=Mock();channel.read.return_value=connect_code(dict(v=1,origin=ORIGIN,key=KEY))
        manager=Mock(__enter__=Mock(return_value=channel),__exit__=Mock(return_value=False))
        with patch.object(desktop_backend.install,'package_manifest',return_value={'discord_directory':True}), \
                patch.object(internet_private_channel,'PrivateChannel',return_value=manager), \
                patch.object(directory,'check_connection',side_effect=credentials.DiscordError('discord_connection_failed')), \
                patch.object(credentials,'store') as store:
            with self.assertRaises(credentials.DiscordError):
                desktop_backend.Desktop.connect_discord(desktop,{'channel':'weplay-internet-'+'c'*32})
        store.assert_not_called();desktop.write.assert_not_called()
        with patch.object(desktop_backend.install,'package_manifest',return_value={}), \
                patch.object(internet_private_channel,'PrivateChannel') as pipe:
            with self.assertRaisesRegex(ValueError,'discord_unavailable'):
                desktop_backend.Desktop.connect_discord(desktop,{'channel':'weplay-internet-'+'c'*32})
        pipe.assert_not_called()


if __name__=='__main__':unittest.main()
