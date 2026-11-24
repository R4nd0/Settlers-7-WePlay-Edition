"""Automatic coordination, not a second UI lobby or a simulation fixture."""
from dataclasses import replace
from pathlib import Path
import sys
import threading
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/installer'))
import lan_autoconnect as auto
from lan_discovery import LanDiscovery, answer_message, query_message
from lobby_directory import (LanInterface, LobbyRoom, LobbyEndpoint, CONTROL_FORMAT,
                             MAX_MEMBERS, DISCOVERY_PORT, LEASE_SECONDS)

A, B = 'a' * 64, 'b' * 64


def room(address, *, expected=A, local='192.168.1.10', default=True, key=1):
    return LobbyRoom(f'{key:032x}', 'WePlay LAN', expected, LobbyEndpoint('lan-direct', address),
                     LanInterface(local, 24, 'Heimnetz', default), expires_at=100)


def status(expected=A, members=1, active=0):
    return dict(format=CONTROL_FORMAT, content_id=expected, members=members,
                active_members=active, max_members=MAX_MEMBERS)


class DiscoveryPolicyTests(unittest.TestCase):
    def directory(self, rooms):
        return Mock(list_rooms=Mock(return_value=rooms))

    def discover(self, rooms, *, statuses=None, **kwargs):
        def response(host, local, path, **options):
            if path == 'peers':
                raise ValueError('No existing admission')
            self.assertEqual(path, 'status')
            self.assertLessEqual(options['timeout'], 1)
            if statuses is None:
                return status(next(r.content_id for r in rooms if r.endpoint.host == host))
            result = statuses[host]
            if isinstance(result, Exception):
                raise result
            return result
        with patch.object(auto, 'request', side_effect=response):
            return auto.discover_preferred(self.directory(rooms), A, **kwargs)

    def test_empty_lan_does_not_probe_arbitrary_addresses(self):
        with patch.object(auto, 'request') as request:
            self.assertIsNone(auto.discover_preferred(self.directory([]), A))
            request.assert_not_called()

    def test_numeric_order_does_not_depend_on_reply_order_name_or_local_adapter_name(self):
        low, high = room('192.168.1.9'), room('192.168.1.100', key=2)
        for rows in ([low, high], [high, low]):
            self.assertEqual(self.discover(rows), low)
        other_view = [replace(r, local_interface=LanInterface('192.168.1.20', 24, 'Andere Karte')) for r in [high, low]]
        self.assertEqual(self.discover(other_view).endpoint.host, low.endpoint.host)

    def test_existing_active_group_wins_over_lower_address_idle_hub(self):
        low, high = room('192.168.1.9'), room('192.168.1.100', key=2)
        result = self.discover([low, high], statuses={low.endpoint.host: status(), high.endpoint.host: status(members=2, active=1)})
        self.assertEqual(result, high)

    def test_two_active_groups_are_reported_instead_of_hiding_other_matches(self):
        rows = [room('192.168.1.9'), room('192.168.1.100', key=2)]
        with self.assertRaises(auto.LanConnectionError) as error:
            self.discover(rows, statuses={r.endpoint.host: status(active=1) for r in rows})
        self.assertEqual(error.exception.code, 'lan_multiple')

    def test_two_active_rooms_with_reused_ip_on_different_adapters_are_distinct(self):
        first = room('192.168.1.20', local='192.168.1.10', key=1)
        second = room('192.168.1.20', local='192.168.1.11', key=2, default=False)
        with self.assertRaises(auto.LanConnectionError) as error:
            self.discover([first, second], statuses={first.endpoint.host: status(active=1)})
        self.assertEqual(error.exception.code, 'lan_multiple')

    def test_known_renderer_alternative_is_live_and_selected_before_alignment(self):
        candidate = room('192.168.1.20', expected=B)
        self.assertEqual(self.discover([candidate], acceptable_content_ids=(B,)), candidate)
        with self.assertRaises(auto.LanConnectionError) as error:
            self.discover([candidate])
        self.assertEqual(error.exception.code, 'lan_version')

    def test_unknown_active_group_cannot_be_hidden_by_a_compatible_idle_hub(self):
        idle, active = room('192.168.1.9'), room('192.168.1.20', expected=B, key=2)
        with self.assertRaises(auto.LanConnectionError) as error:
            self.discover([idle, active], statuses={idle.endpoint.host: status(), active.endpoint.host: status(B, active=1)})
        self.assertEqual(error.exception.code, 'lan_version')

    def test_stale_same_build_beacon_cannot_hide_restarted_different_active_build(self):
        idle, restarted = room('192.168.1.9'), room('192.168.1.20', key=2)
        statuses = {idle.endpoint.host: status(), restarted.endpoint.host: status(B, active=1)}
        with self.assertRaises(auto.LanConnectionError) as error:
            self.discover([idle, restarted], statuses=statuses)
        self.assertEqual(error.exception.code, 'lan_version')
        selected = self.discover([idle, restarted], statuses=statuses, acceptable_content_ids=(B,))
        self.assertEqual(selected.endpoint.host, restarted.endpoint.host)
        self.assertEqual(selected.content_id, B)

    def test_different_idle_profiles_cannot_each_elect_themselves(self):
        low, high = room('192.168.1.9', expected=B), room('192.168.1.20', key=2)
        with self.assertRaises(auto.LanConnectionError) as error:
            self.discover([high, low])
        self.assertEqual(error.exception.code, 'lan_version')
        self.assertEqual(self.discover([high, low], acceptable_content_ids=(B,)), low)
        with patch.object(auto, 'request', side_effect=lambda host, *_, **__: status(B if host == low.endpoint.host else A)):
            chosen = auto.discover_preferred(self.directory([high, low]), B)
        self.assertEqual(chosen.endpoint.host, low.endpoint.host)

    def test_restarted_beacon_generations_probe_one_endpoint_and_use_newer_record(self):
        old = room('192.168.1.20')
        new = replace(old, room_id='f' * 32, expires_at=105)
        with patch.object(auto, 'request', return_value=status(active=1)) as request:
            self.assertEqual(auto.discover_preferred(self.directory([old, new]), A), new)
        request.assert_called_once()

    def test_new_content_for_same_route_gets_one_authoritative_status_probe(self):
        old = room('192.168.1.20')
        current = replace(old, room_id='f' * 32, content_id=B, expires_at=105)
        with patch.object(auto, 'request', return_value=status(B, active=1)) as request:
            selected = auto.discover_preferred(self.directory([old, current]), B)
        self.assertEqual(selected, current)
        request.assert_called_once()

    def discover_on_interfaces(self, directory, sockets, clock):
        pending = list(sockets)
        def ready(_read, _write, _error, wait):
            if pending:
                available = pending[:]
                pending.clear()
                return available, [], []
            clock[0] += wait
            return [], [], []
        with patch('select.select', side_effect=ready), patch('lan_discovery.secrets.token_hex', return_value='c' * 32):
            return auto.discover_preferred(directory, A)

    def test_both_live_adapter_routes_are_checked_for_same_room(self):
        primary = room('192.168.1.20')
        secondary = LanInterface('192.168.1.30', 24, 'Second adapter')
        sockets = [Mock(), Mock()]
        reply = answer_message(query_message('c' * 32), primary)
        for sock in sockets:
            sock.recvfrom.return_value = reply, (primary.endpoint.host, DISCOVERY_PORT)
        clock = [1.]
        directory = LanDiscovery([primary.local_interface, secondary],
                                 socket_factory=Mock(side_effect=sockets), route_socket=Mock(),
                                 clock=lambda: clock[0])
        def inspect(_host, local, *_args, **_options):
            if local == secondary.address:
                raise ConnectionRefusedError()
            return status()
        with patch.object(auto, 'request', side_effect=inspect) as request:
            chosen = self.discover_on_interfaces(directory, sockets, clock)
        self.assertEqual(chosen.local_interface, primary.local_interface)
        self.assertEqual({call.args[1] for call in request.call_args_list}, {primary.local_interface.address, secondary.address})

    def test_expired_connection_uses_fresh_scan_instead_of_stale_room_cache(self):
        old = room('192.168.1.20')
        clock = [1.]
        directory = LanDiscovery([old.local_interface], socket_factory=Mock(), route_socket=Mock(),
                                 clock=lambda: clock[0])
        directory.cache.add(old)
        with patch.object(auto, 'request', side_effect=ConnectionRefusedError) as request:
            self.assertIsNone(self.discover_on_interfaces(directory, [], clock))
        request.assert_called_once_with(old.endpoint.host, old.local_interface.address, 'status', timeout=.65)
        self.assertEqual(directory.resolve(old.room_id), old)

    def test_cached_active_room_survives_a_missed_beacon(self):
        existing = room('192.168.1.20')
        clock = [1.]
        directory = LanDiscovery([existing.local_interface], socket_factory=Mock(), route_socket=Mock(),
                                 clock=lambda: clock[0])
        directory.cache.add(existing)
        with patch.object(auto, 'request', return_value=status(active=1)):
            self.assertEqual(self.discover_on_interfaces(directory, [], clock), existing)

    def test_cached_alternate_adapter_can_reach_a_room_after_a_missed_beacon(self):
        primary = room('192.168.1.20')
        alternate = replace(primary, local_interface=LanInterface('192.168.1.30', 24, 'Second adapter'))
        clock = [1.]
        directory = LanDiscovery([primary.local_interface, alternate.local_interface], socket_factory=Mock(),
                                 route_socket=Mock(), clock=lambda: clock[0])
        directory.cache.add(primary)
        directory.cache.add(alternate)

        def inspect(_host, local, *_args, **_options):
            if local == alternate.local_interface.address:
                raise ConnectionRefusedError()
            return status(active=1)

        with patch.object(auto, 'request', side_effect=inspect) as request:
            self.assertEqual(self.discover_on_interfaces(directory, [], clock), primary)
        self.assertEqual({call.args[1] for call in request.call_args_list},
                         {primary.local_interface.address, alternate.local_interface.address})
        self.assertEqual(len(directory.cache.list_rooms()), 1)
        self.assertEqual(directory.resolve(primary.room_id), primary)

    def test_fresh_room_does_not_conflict_with_cached_generation_on_another_route(self):
        current = room('192.168.1.20', key=2)
        previous = room('192.168.1.20', local='192.168.1.30', key=1, default=False)
        sockets = [Mock(), Mock()]
        reply = answer_message(query_message('c' * 32), current)
        sockets[0].recvfrom.return_value = reply, (current.endpoint.host, DISCOVERY_PORT)
        clock = [1.]
        directory = LanDiscovery([current.local_interface, previous.local_interface],
                                 socket_factory=Mock(side_effect=sockets), route_socket=Mock(),
                                 clock=lambda: clock[0])
        directory.cache.add(previous)
        with patch.object(auto, 'request', return_value=status(active=1)) as status_request:
            selected = self.discover_on_interfaces(directory, [sockets[0]], clock)
        self.assertEqual(selected.room_id, current.room_id)
        status_request.assert_called_once()

    def test_live_group_on_second_adapter_retains_the_bound_source(self):
        other = room('10.20.1.20', local='10.20.1.10', default=False)
        with patch.object(auto, 'request', return_value=status(active=1)) as request:
            self.assertEqual(auto.discover_preferred(self.directory([other]), A).local_interface, other.local_interface)
        self.assertEqual(request.call_args.args[1], '10.20.1.10')

    def test_stale_beacon_cannot_hide_live_hub_or_create_another_group(self):
        stale, live = room('192.168.1.9'), room('192.168.1.20', key=2)
        results = {stale.endpoint.host: OSError(), live.endpoint.host: status()}
        self.assertEqual(self.discover([stale, live], statuses=results), live)
        with self.assertRaises(auto.LanConnectionError) as error:
            self.discover([stale], statuses=results)
        self.assertEqual(error.exception.code, 'lan_unreachable')

    def test_full_group_does_not_turn_into_empty_lan(self):
        full = room('192.168.1.20')
        with self.assertRaises(auto.LanConnectionError) as error:
            self.discover([full], statuses={full.endpoint.host: status(members=8, active=8)})
        self.assertEqual(error.exception.code, 'lan_full')
        own = room('192.168.1.10')
        self.assertEqual(self.discover([own], statuses={own.endpoint.host: status(members=8, active=7)}), own)

    def test_full_group_allows_an_already_admitted_source_to_reconnect(self):
        full = room('192.168.1.20')
        host, local = auto.route(full)
        peers = dict(format=CONTROL_FORMAT, content_id=A, revision=1,
                     lease_seconds=LEASE_SECONDS, peers=[host, local])
        with patch.object(auto, 'request', side_effect=[status(members=8, active=8), peers]) as request:
            self.assertEqual(auto.discover_preferred(self.directory([full]), A), full)
        request.assert_called_with(host, local, 'peers', expected=A, timeout=.65)

    def test_full_group_does_not_allow_an_unverified_lease(self):
        full = room('192.168.1.20')
        host, local = auto.route(full)
        peers = dict(format=CONTROL_FORMAT, content_id=B, revision=1,
                     lease_seconds=LEASE_SECONDS, peers=[host, local])
        for unverified in (peers, dict(peers, content_id=A, peers=[host])):
            with self.subTest(unverified=unverified), \
                    patch.object(auto, 'request', side_effect=[status(members=8, active=8), unverified]), \
                    self.assertRaisesRegex(auto.LanConnectionError, 'LAN-Gruppe ist voll'):
                auto.discover_preferred(self.directory([full]), A)

    def test_status_wrong_version_or_bad_counts_do_not_pass_beacon_check(self):
        item = room('192.168.1.20')
        for bad in (status(B), status(active=True), status(members=9), status(active=2), status(members=True)):
            with self.subTest(bad=bad), self.assertRaises(auto.LanConnectionError):
                self.discover([item], statuses={item.endpoint.host: bad})

    def test_status_is_source_bound_and_cancellation_is_not_swallowed(self):
        item = room('192.168.1.20')
        with patch.object(auto, 'request', return_value=status()) as request:
            self.assertEqual(auto.discover_preferred(self.directory([item]), A), item)
            self.assertEqual(request.call_args.args[:2], (item.endpoint.host, item.local_interface.address))
        cancel = Mock(side_effect=RuntimeError('cancelled'))
        directory = self.directory([item])
        with self.assertRaisesRegex(RuntimeError, 'cancelled'):
            auto.discover_preferred(directory, A, cancel_check=cancel)
        directory.list_rooms.assert_not_called()

    def test_status_concurrency_and_input_size_are_bounded(self):
        rows = [room(f'192.168.1.{n + 20}', key=n) for n in range(33)]
        with patch.object(auto, 'request') as request, self.assertRaises(ValueError):
            auto.discover_preferred(self.directory(rows), A)
        request.assert_not_called()

    def test_cancel_during_status_joins_owned_workers_before_return(self):
        started, release, finished = threading.Event(), threading.Event(), threading.Event()
        calls = 0
        def request(*args, **kwargs):
            started.set()
            release.wait(.65)
            finished.set()
            return status()
        def cancel():
            nonlocal calls
            calls += 1
            if calls > 1:
                self.assertTrue(started.wait(1))
                self.assertFalse(finished.is_set())
                release.set()
                raise RuntimeError('cancelled')
        with patch.object(auto, 'request', side_effect=request), self.assertRaisesRegex(RuntimeError, 'cancelled'):
            auto.discover_preferred(self.directory([room('192.168.1.20')]), A, cancel_check=cancel)
        self.assertTrue(finished.is_set())


class StartupElectionTests(unittest.TestCase):
    def connect(self, discoveries, start=None):
        with patch.object(auto, 'discover_preferred', side_effect=discoveries) as discover:
            result = auto.connect(Mock(), A, start or Mock())
            self.assertLessEqual(discover.call_count, 5)
            return result

    def test_simultaneous_start_settles_on_same_lower_address_from_both_views(self):
        low, high = room('192.168.1.9'), room('192.168.1.20', key=2)
        for views in ([None, high, low, low], [None, low, low], [low, low, low]):
            with self.subTest(views=views):
                self.assertEqual(self.connect(views).endpoint.host, low.endpoint.host)

    def test_late_start_uses_active_hub_without_starting_own(self):
        active = room('192.168.1.20')
        start = Mock()
        self.assertEqual(self.connect([active] * 3, start), active)
        start.assert_not_called()

    def test_local_port_race_only_recovers_after_live_exact_content_discovery(self):
        active = room('192.168.1.20')
        self.assertEqual(self.connect([None, active, active, active], Mock(side_effect=RuntimeError())), active)
        with self.assertRaises(RuntimeError):
            self.connect([None, None], Mock(side_effect=RuntimeError()))

    def test_no_unbounded_retry_on_changing_or_vanished_group(self):
        one, two = room('192.168.1.9'), room('192.168.1.20', key=2)
        with self.assertRaises(auto.LanConnectionError) as error:
            self.connect([one, one, two, one, two])
        self.assertEqual(error.exception.code, 'lan_changing')
        with self.assertRaises(auto.LanConnectionError):
            self.connect([one, None, None, None, None])

    def test_transient_empty_search_does_not_abort_election(self):
        selected = room('192.168.1.20')
        start = Mock()
        self.assertEqual(self.connect([selected, None, selected, selected], start), selected)
        start.assert_not_called()

    def test_cancel_before_start_has_no_side_effects(self):
        start = Mock()
        with self.assertRaisesRegex(RuntimeError, 'cancelled'):
            auto.connect(Mock(), A, start, cancel_check=Mock(side_effect=RuntimeError('cancelled')))
        start.assert_not_called()


if __name__ == '__main__':
    unittest.main()
