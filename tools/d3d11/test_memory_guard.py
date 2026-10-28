import json
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import Mock, patch
import memory_guard as guard
import settings


class MemoryTests(unittest.TestCase):
    def setUp(self):
        root = Path(__file__).resolve().parents[2]/'work/d3d11-tests'
        root.mkdir(exist_ok=True,parents=True)
        self.temp = tempfile.TemporaryDirectory(dir=root)
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)/'memory.json'

    def sample(self, private=1000, free=2000, largest=500):
        return {k:v*guard.MIB for k,v in [('private',private),('free',free),('largest',largest)]}

    def test_fragmentation_and_reservations_trigger_below_private_byte_limit(self):
        self.assertEqual(guard.pressure(self.sample()),0)
        self.assertEqual(guard.pressure(self.sample(free=700)),1)
        self.assertEqual(guard.pressure(self.sample(largest=12)),2)
        self.assertEqual(guard.pressure(self.sample(private=3400)),2)

    def test_persistent_pressure_is_recorded_before_one_warning_without_killing(self):
        child = Mock()
        notified = threading.Event()
        def warn(level):
            self.assertTrue(guard.recovery_required(self.path))
            notified.set()
        sentinel = guard.Sentinel(child,self.path,None,warn=warn)
        sentinel.observe(self.sample(free=700))
        sentinel.observe(self.sample())  # A transient does not latch recovery.
        self.assertFalse(self.path.exists())
        for _ in range(4):
            sentinel.observe(self.sample(largest=12))
        self.assertTrue(notified.wait(2))
        self.assertTrue(guard.recovery_required(self.path))
        child.terminate.assert_not_called()
        child.kill.assert_not_called()
        child.wait.assert_not_called()
        self.assertEqual(json.loads(self.path.read_text())['max_pressure'],2)

    def test_profile_files_and_automated_choices_cannot_bypass_caps(self):
        high = dict(settings.DEFAULTS,**dict.fromkeys(settings.LEVELS,2),
                    AA='msaa8',SSAO=1,FPS=240,Filtering='16')
        capped = settings.conservative(high)
        self.assertEqual(max(capped[k] for k in settings.LEVELS),1)
        self.assertEqual(capped['SSAO'],0)
        self.assertEqual(capped['AA'],'off')
        recovery = settings.conservative(high,True)
        self.assertTrue(all(recovery[k]==0 for k in settings.LEVELS+settings.EFFECTS))
        self.assertEqual(recovery['AA'],'off')
        self.assertEqual(high['TextureQuality'],2)  # Caller preferences not mutated.

    def test_corrupt_summary_does_not_silently_disable_recovery(self):
        self.path.write_text('{"format":"foreign","recovery_required":false}')
        with self.assertRaises(ValueError):
            guard.recovery_required(self.path)

    def test_sustained_pressure_does_not_fsync_on_every_poll(self):
        sentinel = guard.Sentinel(Mock(),self.path,None,warn=lambda level: None)
        with patch.object(guard,'save') as write:
            for _ in range(30):
                sentinel.observe(self.sample(private=3400))
            write.assert_called_once()

    def test_full_address_scan_is_cached_but_private_bytes_are_fresh(self):
        probe = guard.Probe.__new__(guard.Probe)
        probe.address_sample = None; probe.last_scan = float('-inf')
        probe.handle = Mock(); probe.psapi = Mock()
        private = [1000*guard.MIB]
        def counters(handle,pointer,size):
            pointer._obj.private = private[0]
            return True
        probe.psapi.GetProcessMemoryInfo.side_effect = counters
        probe.address_space = Mock(return_value={'free':2000*guard.MIB,'largest':500*guard.MIB})
        now = [0]
        with patch.object(guard.time,'monotonic',side_effect=lambda:now[0]):
            self.assertEqual(probe.sample()['private'],1000*guard.MIB)
            for second in (2,4,6,8):
                now[0] = second; private[0] += guard.MIB
                self.assertEqual(probe.sample()['private'],private[0])
            probe.address_space.assert_called_once()
            now[0] = 10
            probe.sample()
            self.assertEqual(probe.address_space.call_count,2)

    def test_address_scan_exits_when_its_time_budget_is_exceeded(self):
        probe = guard.Probe.__new__(guard.Probe)
        with patch.object(guard.time,'monotonic',side_effect=[0,0.06]):
            with self.assertRaises(OSError):
                probe.address_space()

    def test_monitor_failure_latches_recovery_without_stopping_existing_game(self):
        child, probe = Mock(), Mock()
        child.poll.return_value = None
        probe.sample.side_effect = OSError('scan budget')
        notified = threading.Event()
        sentinel = guard.Sentinel(child,self.path,probe,warn=lambda level:notified.set())
        sentinel.stop = Mock()
        sentinel.stop.wait.return_value = False
        sentinel.run()
        self.assertTrue(notified.wait(2))
        summary = json.loads(self.path.read_text())
        self.assertTrue(summary['monitor_failed'])
        self.assertTrue(summary['recovery_required'])
        child.terminate.assert_not_called()
        child.kill.assert_not_called()


if __name__ == '__main__':
    unittest.main()
