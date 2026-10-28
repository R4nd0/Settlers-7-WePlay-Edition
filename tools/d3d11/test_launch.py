from pathlib import Path
import unittest
from unittest.mock import Mock, patch
import launch


class LaunchTests(unittest.TestCase):
    def setUp(self):
        allowed = patch.object(launch,'GAMEPLAY_QUALIFIED',True)
        allowed.start()
        self.addCleanup(allowed.stop)

    def test_unqualified_start_is_blocked_before_any_file_or_process_work(self):
        with patch.object(launch,'GAMEPLAY_QUALIFIED',False), \
             patch.object(launch.prepare,'package') as package, \
             patch.object(launch.subprocess,'Popen') as spawn:
            with self.assertRaises(RuntimeError):
                launch.run(Mock(preflight=False),Path('missing-copy'))
            launch.require_qualified(True)
            package.assert_not_called()
            spawn.assert_not_called()

    def test_service_is_not_instrumented_and_only_exact_game_child_gets_probe(self):
        root = Path('work/fixture').resolve()
        proxy = launch.GuardedSubprocess(root,root/'memory.json',True)
        child, sentinel = Mock(), Mock()
        with patch.object(launch.subprocess,'Popen',return_value=child) as spawn, \
             patch.object(launch.memory_guard,'Probe') as probe, \
             patch.object(launch.memory_guard,'Sentinel',return_value=sentinel) as watch:
            proxy.Popen(['service.exe'])
            probe.assert_not_called()
            proxy.Popen([str(root/'Base/Bin/Settlers7R.exe')],cwd=root/'Base/Bin')
            probe.assert_called_once_with(child,root/'Base/Bin/Settlers7R.exe')
            self.assertTrue(watch.call_args.kwargs['sticky'])
            sentinel.start.assert_called_once()
            proxy.close()
            sentinel.close.assert_called_once()

    def test_monitor_initialization_failure_stops_only_the_new_owned_child(self):
        root = Path('work/fixture').resolve()
        proxy = launch.GuardedSubprocess(root,root/'memory.json',False)
        child = Mock(); child.poll.return_value = None
        with patch.object(launch.subprocess,'Popen',return_value=child), \
             patch.object(launch.memory_guard,'Probe',side_effect=OSError('unavailable')):
            with self.assertRaises(RuntimeError):
                proxy.Popen([str(root/'Base/Bin/Settlers7R.exe')],cwd=root/'Base/Bin')
        child.terminate.assert_called_once()
        child.wait.assert_called_once_with(timeout=10)

    def test_wrong_game_working_directory_is_rejected_before_spawn(self):
        root = Path('work/fixture').resolve()
        proxy = launch.GuardedSubprocess(root,root/'memory.json',False)
        with patch.object(launch.subprocess,'Popen') as spawn:
            with self.assertRaises(ValueError):
                proxy.Popen([str(root/'Base/Bin/Settlers7R.exe')],cwd=root)
            spawn.assert_not_called()


if __name__ == '__main__':
    unittest.main()
