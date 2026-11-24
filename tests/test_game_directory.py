import json
from pathlib import Path
import sys
import unittest
from contextlib import nullcontext
from unittest.mock import Mock,patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/installer'))
import game_directory as directory
import internet_room
import join_crypto
import desktop_backend

ORIGIN='https://weplay-multiplayer.testing.workers.dev'
CONTENT='a'*64
GAME='b'*32

def row(**changes):
    return dict(dict(id=GAME,title='Community',state='open',players=1,capacity=4,free_slots=3,
        content_id=CONTENT,network_revision=7,version='2.0.0-rc3.12',expires_at=1035),**changes)

class DirectoryTests(unittest.TestCase):
    def test_reader_and_join_codes_are_exact_and_cannot_set_arbitrary_endpoints(self):
        for prefix,extra in [('weplay-directory1.',{}),('weplay-join1.',{'game_id':GAME})]:
            value=dict(v=1,origin=ORIGIN,**extra)
            self.assertEqual(directory.parse_code(prefix+join_crypto.encoded(json.dumps(value).encode())),(ORIGIN,extra.get('game_id')))
            for change in ({'origin':'http://localhost'},{'origin':ORIGIN+'/x'},{'v':True},{'secret':'never'}):
                bad=dict(value,**change)
                with self.assertRaises(directory.DirectoryError):directory.parse_code(prefix+join_crypto.encoded(json.dumps(bad).encode()))
        with self.assertRaises(directory.DirectoryError):directory.record('{"a":1,"a":2}')

    def test_join_code_requires_an_actual_game_identifier(self):
        code = 'weplay-join1.' + join_crypto.encoded(json.dumps(dict(v=1, origin=ORIGIN, game_id=None)).encode())
        with self.assertRaisesRegex(directory.DirectoryError, 'directory_code_invalid'):
            directory.parse_code(code)

    def test_metadata_rejects_private_fields_duplicates_and_bad_types(self):
        self.assertEqual(directory.validate_game(row()),row())
        for change in ({'token':'never'},{'players':True},{'title':'weplay1.secret'},{'id':'bad'},
                       {'network_revision':True},{'content_id':'bad'},{'state':'started','free_slots':1}):
            with self.subTest(change=change),self.assertRaises(ValueError):directory.validate_game(row(**change))
        transport=Mock(return_value=dict(format='weplay-games-directory-v1',games=[row(),row()]))
        with self.assertRaises(ValueError):directory.listing(ORIGIN,transport=transport)

    def test_transport_is_bounded_no_redirects_and_no_secrets_in_errors(self):
        connection=Mock();response=Mock(status=302,read=Mock(return_value=b'weplay1.private'));connection.getresponse.return_value=response
        with self.assertRaisesRegex(directory.DirectoryError,'^directory_unavailable$'):
            directory.request(ORIGIN,'/v1/directory/games',connection_factory=Mock(return_value=connection))
        self.assertEqual(connection.request.call_count,1);connection.close.assert_called_once()
        for path in ('https://attacker.invalid','/v1/directory/games?secret=x','/v1/directory/../games'):
            with self.assertRaises(ValueError):directory.request(ORIGIN,path)

    def test_join_requires_matching_data_and_live_open_game_before_key_generation(self):
        crypto=Mock()
        for change,allowed in [({},set()),({'state':'full','free_slots':0},{CONTENT}),({'state':'started','free_slots':0},{CONTENT})]:
            transport=Mock(return_value=dict(format='weplay-games-directory-v1',games=[row(**change)]))
            with self.assertRaises(ValueError):directory.join(ORIGIN,GAME,allowed,transport=transport,crypto=crypto)
        crypto.assert_not_called()

    def test_cancelled_join_does_not_contact_directory(self):
        transport = Mock(return_value=dict(format='weplay-games-directory-v1', games=[row()]))
        crypto = Mock(return_value=dict(format=join_crypto.FORMAT, public_key='public', private_key='private'))
        cancel = Mock(side_effect=RuntimeError('cancelled'))
        with self.assertRaisesRegex(RuntimeError, 'cancelled'):
            directory.join(ORIGIN, GAME, {CONTENT}, transport=transport, crypto=crypto, cancel=cancel)
        transport.assert_not_called()
        crypto.assert_not_called()

    def test_guest_validates_ticket_and_always_consumes_on_cancel_or_bad_envelope(self):
        for cancel in (True,False):
            calls=[];ticket=None
            def transport(url,path,method='GET',value=None,authorization=None):
                nonlocal ticket
                calls.append((path,method))
                if path.endswith('/games'):return dict(format='weplay-games-directory-v1',games=[row()])
                if method=='POST':
                    ticket=dict(format='weplay-join-ticket-v1',id='c'*32,game_id=GAME,challenge=value['challenge'],expires_at=1045)
                    return ticket
                if method=='DELETE':return {'ok':True}
                return dict(ticket,format='weplay-join-delivery-v1',envelope={'malformed':True})
            def crypto(action,value,**kw):
                if action=='keygen':return dict(format=join_crypto.FORMAT,public_key='public',private_key='private')
                raise ValueError('directory_join_failed')
            def cancel_after_ticket():
                if ticket is not None:
                    raise ValueError('cancelled')
            with self.assertRaises(ValueError):
                directory.join(ORIGIN,GAME,{CONTENT},transport=transport,crypto=crypto,clock=lambda:1000,
                    cancel=cancel_after_ticket if cancel else None)
            self.assertEqual(calls[-1],('/v1/directory/join/'+'c'*32,'DELETE'))

    def test_join_recovers_from_temporary_delivery_failure_on_same_ticket(self):
        calls = []
        ticket = None
        invitation = internet_room.Room('a'*32, 'b'*64, '10.203.77.0/24').token()
        def transport(_url, path, method='GET', value=None, authorization=None):
            nonlocal ticket
            calls.append((path, method, authorization))
            if path.endswith('/games'):
                return dict(format='weplay-games-directory-v1', games=[row()])
            if method == 'POST':
                ticket = dict(format='weplay-join-ticket-v1', id='c'*32, game_id=GAME,
                              challenge=value['challenge'], expires_at=1045)
                return ticket
            if method == 'DELETE':
                return {'ok': True}
            if sum(path == entry[0] and entry[1] == 'GET' for entry in calls) == 1:
                raise directory.DirectoryError('directory_unavailable')
            return dict(ticket, format='weplay-join-delivery-v1', envelope={'sealed': 'fixture'})

        def crypto(action, _value, **_options):
            if action == 'keygen':
                return dict(format=join_crypto.FORMAT, public_key='public', private_key='private')
            grant = dict(ticket, format='weplay-room-grant-v1', invitation=invitation)
            return dict(plaintext=join_crypto.encoded(json.dumps(grant).encode('ascii')))

        self.assertEqual(directory.join(ORIGIN, GAME, {CONTENT}, transport=transport, crypto=crypto,
                                        clock=lambda: 1000, sleep=lambda _: None), invitation)
        ticket_calls = [call for call in calls if call[0].endswith('/' + ticket['id'])]
        self.assertEqual([call[1] for call in ticket_calls], ['GET', 'GET', 'DELETE'])
        self.assertEqual(len({call[2] for call in ticket_calls}), 1)

    def test_delivery_that_arrives_after_ticket_expiry_is_rejected(self):
        now = [1000]
        ticket = None
        invitation = internet_room.Room('a'*32, 'b'*64, '10.203.77.0/24').token()
        def transport(_url, path, method='GET', value=None, authorization=None):
            nonlocal ticket
            if path.endswith('/games'):
                return dict(format='weplay-games-directory-v1', games=[row(expires_at=1100)])
            if method == 'POST':
                ticket = dict(format='weplay-join-ticket-v1', id='c'*32, game_id=GAME,
                              challenge=value['challenge'], expires_at=1005)
                return ticket
            if method == 'DELETE':
                return {'ok': True}
            now[0] = 1006
            return dict(ticket, format='weplay-join-delivery-v1', envelope={'sealed': 'fixture'})

        def crypto(action, _value, **_options):
            if action == 'keygen':
                return dict(format=join_crypto.FORMAT, public_key='public', private_key='private')
            grant = dict(ticket, format='weplay-room-grant-v1', invitation=invitation)
            return dict(plaintext=join_crypto.encoded(json.dumps(grant).encode('ascii')))

        with self.assertRaisesRegex(directory.DirectoryError, '^directory_join_unavailable$'):
            directory.join(ORIGIN, GAME, {CONTENT}, transport=transport, crypto=crypto,
                           clock=lambda: now[0])

    def test_clock_rollback_cannot_prolong_public_join_reservation(self):
        wall, elapsed = [1000], [10.]
        ticket = None
        polls = []
        def transport(_url, path, method='GET', value=None, authorization=None):
            nonlocal ticket
            if path.endswith('/games'):
                return dict(format='weplay-games-directory-v1', games=[row()])
            if method == 'POST':
                ticket = dict(format='weplay-join-ticket-v1', id='c'*32, game_id=GAME,
                              challenge=value['challenge'], expires_at=1001)
                return ticket
            if method == 'DELETE':
                return {'ok': True}
            polls.append(path)
            if len(polls) > 2:
                self.fail('Polling outlived the original ticket lifetime')
            return dict(ticket, format='weplay-join-delivery-v1', envelope=None)

        def delay(seconds):
            elapsed[0] += seconds

        validate = directory.validate_ticket
        def validate_before_clock_rolls_back(*args):
            accepted = validate(*args)
            wall[0] = 900
            return accepted

        crypto = Mock(return_value=dict(format=join_crypto.FORMAT, public_key='public', private_key='private'))
        with patch.object(directory, 'validate_ticket', side_effect=validate_before_clock_rolls_back), \
                patch.object(directory.time, 'monotonic', side_effect=lambda: elapsed[0]), \
                self.assertRaisesRegex(directory.DirectoryError, '^directory_join_unavailable$'):
            directory.join(ORIGIN, GAME, {CONTENT}, transport=transport, crypto=crypto,
                           clock=lambda: wall[0], sleep=delay)
        self.assertEqual(len(polls), 2)

    def make_broker(self, transport):
        invitation = internet_room.Room('a' * 32, 'b' * 64, '10.203.77.0/24').token()
        return directory.HostBroker(ORIGIN, 's' * 43, 'e' * 32, invitation, CONTENT, '2.0.0-rc4.10',
                                    transport=transport, crypto=Mock(return_value={'sealed': 'fixture'}),
                                    clock=lambda: 1000, monotonic=lambda: 0)

    def test_cancelled_guest_does_not_abort_delivery_to_next_guest(self):
        first = dict(id='c'*32, session_id='00000001', game_id=GAME,
                     public_key='k'*600, challenge='d'*32, expires_at=1045)
        second = dict(first, id='d'*32)
        delivered = []
        def transport(_url, path, _method, _value, **_options):
            if path.endswith('/host'):
                return {'ok': True, 'requests': [first, second]}
            if path.endswith(first['id']):
                raise directory.DirectoryError('directory_join_unavailable')
            delivered.append(path)
            return {'ok': True}
        broker = self.make_broker(transport)
        broker.poll([dict(id='00000001', state='open', free_slots=3)])
        self.assertEqual(delivered, ['/v1/directory/host/' + broker.run_id + '/requests/' + second['id']])
        self.assertNotIn(first['id'], broker.pending)

    def test_delivery_outages_do_not_retain_envelopes_for_replaced_ticket_batches(self):
        first = dict(id='c'*32, session_id='00000001', game_id=GAME,
                     public_key='k'*600, challenge='d'*32, expires_at=1045)
        second = dict(first, id='d'*32)
        transport = Mock(side_effect=[
            {'ok': True, 'requests': [first]}, directory.DirectoryError('directory_unavailable'),
            {'ok': True, 'requests': [second]}, directory.DirectoryError('directory_unavailable')])
        broker = self.make_broker(transport)
        rows = [dict(id='00000001', state='open', free_slots=3)]
        with self.assertRaisesRegex(directory.DirectoryError, '^directory_unavailable$'):
            broker.poll(rows)
        self.assertEqual(list(broker.pending), [first['id']])
        broker.last = -11
        with self.assertRaisesRegex(directory.DirectoryError, '^directory_unavailable$'):
            broker.poll(rows)
        self.assertEqual(list(broker.pending), [second['id']])

    def test_failed_host_removal_can_retry_on_next_empty_game_list(self):
        transport = Mock(side_effect=[{'ok': True, 'requests': []},
                                      directory.DirectoryError('directory_unavailable'), {'ok': True}])
        broker = self.make_broker(transport)
        broker.poll([dict(id='00000001', state='open', free_slots=3)])
        with self.assertRaisesRegex(directory.DirectoryError, 'directory_unavailable'):
            broker.poll([])
        self.assertTrue(broker.registered)
        broker.poll([])
        self.assertFalse(broker.registered)
        self.assertEqual([call.args[2] for call in transport.call_args_list], ['PUT', 'DELETE', 'DELETE'])

    def test_reopened_native_match_registers_without_waiting_for_old_poll_interval(self):
        transport = Mock(side_effect=[{'ok': True, 'requests': []}, {'ok': True},
                                      {'ok': True, 'requests': []}])
        broker = self.make_broker(transport)
        rows = [dict(id='00000001', state='open', free_slots=3)]
        broker.poll(rows)
        broker.poll([])
        broker.poll([dict(rows[0], id='00000002')])
        self.assertEqual([call.args[2] for call in transport.call_args_list], ['PUT', 'DELETE', 'PUT'])

    def test_host_only_seals_open_native_sessions_and_caches_retry_envelope(self):
        room=internet_room.Room('a'*32,'b'*64,'10.203.77.0/24')
        invitation=room.token()
        now=[0];requests=[];posted=[]
        item=dict(id='c'*32,session_id='00000001',game_id=GAME,public_key='k'*600,challenge='d'*32,expires_at=1045)
        def transport(url,path,method='GET',value=None,authorization=None):
            self.assertEqual(authorization,'Bearer '+'s'*43)
            requests.append(method)
            if method=='PUT' and path.endswith('/host'):return {'ok':True,'requests':[item]}
            posted.append(value);return {'ok':True}
        crypto=Mock(return_value={'encrypted':'synthetic fixture'})
        broker=directory.HostBroker(ORIGIN,'s'*43,'e'*32,invitation,CONTENT,'2.0.0-rc3.12',
            transport=transport,crypto=crypto,clock=lambda:1000,monotonic=lambda:now[0])
        native={k:v for k,v in row().items() if k in ('title','state','players','capacity','free_slots')};native['id']='00000001'
        broker.poll([native]);now[0]=11;broker.poll([native]);self.assertEqual(crypto.call_count,1)
        clear=json.loads(join_crypto.decoded(crypto.call_args.args[1]['plaintext']))
        self.assertEqual(clear['invitation'],invitation)
        now[0]=22;broker.poll([dict(native,state='started',free_slots=0)])
        self.assertIsNone(posted[-1]['envelope'])
        broker.poll([]);self.assertEqual(requests[-1],'DELETE');self.assertFalse(broker.pending)

class BackendTests(unittest.TestCase):
    def setUp(self):
        self.backend=object.__new__(desktop_backend.Desktop)
        self.backend.package=ROOT/'work/directory-package-fixture';self.backend.state=ROOT/'work/directory-state-fixture'
        self.backend.verification=lambda:nullcontext()
        self.backend.package_manifest=lambda:desktop_backend.install.package_manifest(self.backend.package)
        self.backend.game=Mock(return_value=ROOT/'work/directory-game-fixture')
        self.backend.alternatives=Mock(return_value={'dxvk':CONTENT})
        self.backend.installed=Mock(return_value={'package_id':'current'})
        self.backend.package_id=Mock(return_value='current');self.backend.progress=Mock();self.backend.write=Mock()
        self.backend.play=Mock(return_value={'exit_code':0})
        self.addCleanup(patch.stopall)
        patch.object(desktop_backend.install,'package_manifest',return_value={'game_directory':True}).start()
        patch.object(desktop_backend.install,'package_metadata',return_value={'game_directory':True}).start()
        patch.object(directory,'configured',return_value=ORIGIN).start()
        patch.object(desktop_backend.Desktop,'network_id',return_value=CONTENT).start()
        patch.object(desktop_backend.graphics,'ensure_closed').start()

    def test_list_exposes_only_public_rows_and_compatibility(self):
        with patch.object(directory,'listing',return_value=[row(),row(id='c'*32,content_id='d'*64)]):
            result=self.backend.directory_list()
        self.assertEqual([game['compatible'] for game in result['games']],[True,False])
        self.assertNotIn('origin',json.dumps(result));self.assertNotIn('invitation',json.dumps(result))

    def test_private_reader_code_uses_pipe_and_persists_no_publisher_credential(self):
        import internet_private_channel
        channel=Mock();channel.__enter__=Mock(return_value=channel);channel.__exit__=Mock(return_value=False)
        channel.read.return_value='weplay-directory1.'+join_crypto.encoded(json.dumps(dict(v=1,origin=ORIGIN)).encode())
        with patch.object(internet_private_channel,'PrivateChannel',return_value=channel), \
                patch.object(directory, 'listing', return_value=[]):
            self.assertEqual(self.backend.directory_connect({'channel':'private-fixture'}),{'connected':True,'game_id':None})
        self.assertEqual(self.backend.write.call_args.args[1],dict(format=directory.FORMAT,origin=ORIGIN,explicit_override=True))

    def test_unreachable_directory_does_not_replace_previous_working_origin(self):
        import internet_private_channel
        channel = Mock()
        channel.__enter__ = Mock(return_value=channel)
        channel.__exit__ = Mock(return_value=False)
        channel.read.return_value = 'weplay-directory1.' + join_crypto.encoded(
            json.dumps(dict(v=1, origin=ORIGIN)).encode())
        failure = directory.DirectoryError('directory_unavailable')
        with patch.object(internet_private_channel, 'PrivateChannel', return_value=channel), \
                patch.object(directory, 'listing', side_effect=failure) as verify, \
                self.assertRaisesRegex(directory.DirectoryError, 'directory_unavailable'):
            self.backend.directory_connect({'channel': 'private-fixture'})
        verify.assert_called_once_with(ORIGIN)
        self.backend.write.assert_not_called()

    def test_join_delegates_only_resolved_private_room_to_existing_internet_launcher(self):
        with patch.object(directory,'join',return_value='weplay1.private-fixture') as resolve:
            self.assertEqual(self.backend.directory_join({'game_id':GAME,'player_name':'Tester','internet_consent':1}),{'exit_code':0})
        self.assertEqual(resolve.call_args.args,(ORIGIN,GAME,{CONTENT}))
        self.assertEqual(self.backend.play.call_args.kwargs['internet']['invitation'],'weplay1.private-fixture')
        self.assertFalse(self.backend.play.call_args.kwargs['internet']['publish_discord'])

    def test_update_and_closed_game_guard_precede_remote_join(self):
        self.backend.installed.return_value={'package_id':'outdated'}
        with patch.object(directory,'join') as resolve,self.assertRaisesRegex(ValueError,'^update_required$'):
            self.backend.directory_join({'game_id':GAME,'player_name':'Tester','internet_consent':1})
        resolve.assert_not_called()
        self.assertEqual(desktop_backend.error_code(directory.DirectoryError('directory_join_failed')),'directory_join_failed')
        self.assertNotIn('private',desktop_backend.error_code(ValueError('weplay1.private')))

    def test_public_join_requires_consent_before_requesting_room_credentials(self):
        with patch.object(directory,'join') as resolve:
            for value in (None,False,True,0,'1'):
                with self.assertRaisesRegex(ValueError,'internet_consent_required'):
                    self.backend.directory_join({'game_id':GAME,'player_name':'Tester','internet_consent':value})
            resolve.assert_not_called();self.backend.play.assert_not_called()

if __name__=='__main__':unittest.main()
