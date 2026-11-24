import unittest
from .pacing import interval_identity


class PacingTests(unittest.TestCase):
    def test_readback_or_toggle_changes_invalidate_interval(self):
        value=dict(capture_requests=1,capture_completed=1,capture_failures=0,
                   effects_revision=2,effects_active=7,effects_pending=0)
        baseline=interval_identity(value)
        for changed in ({**value,'capture_requests':2,'capture_completed':2},
                        {**value,'effects_revision':3},{**value,'effects_active':0}):
            self.assertNotEqual(interval_identity(changed),baseline)

    def test_pending_work_and_missing_status_refuse_measurement(self):
        value=dict(capture_requests=1,capture_completed=1,capture_failures=0,
                   effects_revision=2,effects_active=7,effects_pending=0)
        for changed in ({**value,'effects_pending':1},{**value,'capture_requests':2},{},
                        {**value,'effects_active':True}):
            with self.assertRaises(ValueError):interval_identity(changed)
