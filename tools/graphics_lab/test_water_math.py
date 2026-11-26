"""Own result fixtures only; no GPU execution."""
import copy
import unittest
from .water_math import validate


def fixture(mode):
    value=dict(synthetic_only=True,on12_query_hr=0 if mode.endswith('on12') else -2147467262,draws_and_readbacks_passed=True,cases=[])
    for i in range(4):
        nan=mode.endswith('on12')and i==0;v=2.25 if i%2 else .25;b=0x40100000 if i%2 else 0x3e800000
        value['cases'].append(dict(guarded=i>=2,base=i%2,rgba=[None,None,None,1]if nan else[v,v,v,1],bits=[0xffc00000]*3+[0x3f800000]if nan else[b,b,b,0x3f800000]))
    return value


class Tests(unittest.TestCase):
    def test_both_renderers_require_real_bit_and_numeric_results(self):
        for mode in ('dxwrapper-d3d9','dxwrapper-d3d9on12'):
            good=fixture(mode);self.assertTrue(validate(mode,good))
            for field,replacement in (('synthetic_only',False),('on12_query_hr',1),('draws_and_readbacks_passed',False),('cases',good['cases'][:3])):
                value=copy.deepcopy(good);value[field]=replacement;self.assertFalse(validate(mode,value))
            value=copy.deepcopy(good);value['cases'][2]['rgba'][0]=None;self.assertFalse(validate(mode,value))
            value=copy.deepcopy(good);value['cases'][3]['bits'][0]=0;self.assertFalse(validate(mode,value))
        value=fixture('dxwrapper-d3d9on12');value['cases'][0]['bits'][0]=0x7f800000
        self.assertFalse(validate('dxwrapper-d3d9on12',value))
        self.assertFalse(validate('dxwrapper-d3d9on12',fixture('dxwrapper-d3d9')))


if __name__=='__main__':unittest.main()
