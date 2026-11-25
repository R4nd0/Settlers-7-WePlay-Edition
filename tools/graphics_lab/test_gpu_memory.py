import unittest
from .gpu_memory import normalize


class GpuMemoryTests(unittest.TestCase):
    def row(self,pid=123):
        return dict(Name=f'pid_{pid}_luid_0x00000000_0x00000123_phys_0',
            DedicatedUsage=100,SharedUsage=20,TotalCommitted=150,LocalUsage=90,NonLocalUsage=30)

    def test_memory_kinds_remain_distinct_and_absence_is_unavailable(self):
        second={**self.row(),'Name':self.row()['Name'].replace('_phys_0','_phys_1')}
        value=normalize([self.row(),second],123)
        self.assertEqual(value['totals']['dedicated_bytes'],200)
        self.assertEqual(value['totals']['shared_bytes'],40)
        self.assertEqual(value['totals']['committed_bytes'],300)
        self.assertFalse(normalize([],123)['available'])
        self.assertIsNone(normalize([],123)['totals'])

    def test_wrong_process_or_schema_does_not_become_a_measurement(self):
        for value in ([self.row(1234)],[{**self.row(),'DedicatedUsage':True}],
                      [{**self.row(),'SharedUsage':-1}],[{**self.row(),'extra':'untrusted'}]):
            with self.assertRaises(ValueError):normalize(value,123)
        with self.assertRaises(ValueError):normalize([self.row(),self.row()],123)
