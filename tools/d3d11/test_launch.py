from pathlib import Path
import unittest
from unittest.mock import Mock, patch
import launch


class LaunchTests(unittest.TestCase):
    def setUp(self):
        allowed = patch.object(launch,'GAMEPLAY_QUALIFIED',True)
        allowed.start()
        self.addCleanup(allowed.stop)
        headroom=patch.object(launch.memory_guard,'observe_host_memory')
        headroom.start()
        self.addCleanup(headroom.stop)

    def test_unqualified_start_is_blocked_before_any_file_or_process_work(self):
        with patch.object(launch,'GAMEPLAY_QUALIFIED',False), \
             patch.object(launch.prepare,'package') as package, \
             patch.object(launch.subprocess,'Popen') as spawn:
            with self.assertRaises(RuntimeError):
                launch.run(Mock(preflight=False),Path('missing-copy'))
            launch.require_qualified(True)
            package.assert_not_called()
            spawn.assert_not_called()

    def test_low_host_memory_does_not_refuse_diagnostic_spawn(self):
        root=Path('work/fixture').resolve()
        proxy=launch.GuardedSubprocess(root,root/'memory.json',False)
        with patch.object(launch.memory_guard,'observe_host_memory',return_value={'host_commit_free':0}), \
             patch.object(launch.memory_guard,'DiagnosticSentinel'), \
             patch.object(launch.subprocess,'Popen') as spawn:
            self.assertIs(proxy.Popen([str(root/'Base/Bin/Settlers7R.exe')],cwd=root/'Base/Bin'),spawn.return_value)
            spawn.return_value.terminate.assert_not_called()

    def test_service_is_not_instrumented_and_only_exact_game_child_gets_probe(self):
        root = Path('work/fixture').resolve()
        proxy = launch.GuardedSubprocess(root,root/'memory.json',True)
        child, sentinel = Mock(), Mock()
        with patch.object(launch.subprocess,'Popen',return_value=child) as spawn, \
             patch.object(launch.memory_guard,'Probe') as probe, \
             patch.object(launch.memory_guard,'DiagnosticSentinel',return_value=sentinel) as watch:
            proxy.Popen(['service.exe'])
            probe.assert_not_called()
            proxy.Popen([str(root/'Base/Bin/Settlers7R.exe')],cwd=root/'Base/Bin')
            probe.assert_not_called()
            self.assertEqual(watch.call_args.args[2],root/'Base/Bin/Settlers7R.exe')
            self.assertTrue(watch.call_args.kwargs['sticky'])
            sentinel.start.assert_called_once()
            proxy.close()
            sentinel.close.assert_called_once()

    def test_monitor_initialization_failure_does_not_stop_the_owned_child(self):
        root = Path('work/fixture').resolve()
        proxy = launch.GuardedSubprocess(root,root/'memory.json',False)
        child = Mock(); child.poll.return_value = None
        with patch.object(launch.subprocess,'Popen',return_value=child), \
             patch.object(launch.memory_guard,'Probe',side_effect=OSError('unavailable')):
            self.assertIs(proxy.Popen([str(root/'Base/Bin/Settlers7R.exe')],cwd=root/'Base/Bin'),child)
            self.assertTrue(proxy.sentinel.snapshot()['monitor_failed'])
        child.terminate.assert_not_called()
        child.wait.assert_not_called()

    def test_wrong_game_working_directory_is_rejected_before_spawn(self):
        root = Path('work/fixture').resolve()
        proxy = launch.GuardedSubprocess(root,root/'memory.json',False)
        with patch.object(launch.subprocess,'Popen') as spawn:
            with self.assertRaises(ValueError):
                proxy.Popen([str(root/'Base/Bin/Settlers7R.exe')],cwd=root)
            spawn.assert_not_called()


if __name__ == '__main__':
    unittest.main()
