"""Do not mistake low physical availability for an owned game's allocation limit."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/d3d11'))
import memory_guard as guard


class AllocationPolicyTests(unittest.TestCase):
    def sample(self):
        return dict(private=1800*guard.MIB,free=1200*guard.MIB,largest=1000*guard.MIB,
                    host_physical_free=500*guard.MIB,host_commit_free=12000*guard.MIB)

    def test_windows_can_supply_pages_with_low_physical_availability(self):
        sample=self.sample()
        self.assertEqual(guard.allocation_pressure(sample),0)
        self.assertEqual(guard.require_game_headroom(sample),sample)

    def test_commit_and_32_bit_constraints_remain_effective(self):
        sample=self.sample();sample['host_commit_free']=512*guard.MIB
        self.assertEqual(guard.allocation_pressure(sample),2)
        sample=self.sample();sample['largest']=8*guard.MIB
        self.assertEqual(guard.allocation_pressure(sample),2)
        sample=self.sample();sample['free']=200*guard.MIB
        self.assertEqual(guard.allocation_pressure(sample),2)

    def test_selected_policy_keeps_actual_physical_telemetry(self):
        watcher=guard.Sentinel(object(),Path('unused.json'),object(),pressure_policy=guard.allocation_pressure)
        watcher.observe(self.sample())
        self.assertEqual(watcher.summary['max_pressure'],0)
        self.assertEqual(watcher.summary['min_host_physical_free_mib'],500)
        ordinary=guard.Sentinel(object(),Path('unused.json'),object())
        ordinary.observe(self.sample())
        self.assertEqual(ordinary.summary['max_pressure'],2)


if __name__=='__main__':unittest.main()
