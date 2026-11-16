"""Owned service lifetime; no game, socket, VPN or process launch."""
from pathlib import Path
import sys
import threading
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/installer'))
from crossplay_runtime import SessionPump


class RuntimeTests(unittest.TestCase):
    def endpoint(self):
        result = Mock(); result.closed = False
        return result

    def test_owned_game_exit_closes_all_services_without_terminating_game(self):
        broker, gold, gateway = self.endpoint(), self.endpoint(), self.endpoint()
        game = Mock(); game.poll.return_value = None
        reached = threading.Event(); broker.pump.side_effect = lambda *_: reached.set()
        owner = SessionPump(broker=broker, gold=gold, gateway=gateway).attach(game)
        self.addCleanup(owner.close); owner.start()
        self.assertTrue(reached.wait(2)); self.assertFalse(owner.closed)
        game.poll.return_value = 0; owner.thread.join(timeout=2)
        self.assertTrue(owner.closed); owner.check()
        for service in (broker, gold, gateway): service.close.assert_called_once()
        game.terminate.assert_not_called(); game.kill.assert_not_called()

    def test_service_failure_closes_channel_and_reports_only_safe_error(self):
        broker, gold = self.endpoint(), self.endpoint()
        broker.pump.side_effect = RuntimeError('fixture sensitive payload')
        game = Mock(); game.poll.return_value = None
        owner = SessionPump(broker=broker, gold=gold).attach(game)
        self.addCleanup(owner.close); owner.start(); owner.thread.join(timeout=2)
        self.assertTrue(owner.closed)
        with self.assertRaisesRegex(RuntimeError, '^crossplay_service_failed$'): owner.check()
        broker.close.assert_called_once(); gold.close.assert_called_once()
        game.terminate.assert_not_called(); game.kill.assert_not_called()

    def test_explicit_cancel_closes_waiting_session_and_close_is_idempotent(self):
        broker, gold = self.endpoint(), self.endpoint()
        owner = SessionPump(broker=broker, gold=gold)
        owner.start(); owner.close(); owner.close()
        self.assertTrue(owner.closed); owner.check()
        broker.close.assert_called_once(); gold.close.assert_called_once()
        with self.assertRaises(ValueError): owner.start()
        with self.assertRaises(ValueError): owner.attach(Mock())

    def test_partial_unstarted_scope_releases_resources_and_forbids_replacement_game(self):
        broker = self.endpoint(); owner = SessionPump(broker=broker)
        owner.attach(Mock())
        with self.assertRaises(ValueError): owner.attach(Mock())
        owner.close(); broker.close.assert_called_once()
        for values in ({}, {'gold': self.endpoint()}):
            with self.assertRaises(ValueError): SessionPump(**values)

    def test_failed_thread_start_releases_prepared_resources(self):
        broker = self.endpoint(); owner = SessionPump(broker=broker)
        with patch.object(threading.Thread, 'start', side_effect=RuntimeError('fixture')):
            with self.assertRaises(RuntimeError): owner.start()
        owner.close(); self.assertTrue(owner.closed)
        broker.close.assert_called_once()


if __name__ == '__main__': unittest.main()
