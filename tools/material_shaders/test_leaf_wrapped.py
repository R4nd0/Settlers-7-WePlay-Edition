"""Authored pixel programs only; original game programs are never fixtures."""
import math,struct,unittest
from unittest.mock import patch
from tools.material_shaders import bytecode,leaf_wrapped,semantics
from tools.material_shaders.test_bytecode import packed
def fixture():
    return packed(0xffff0300,0x0200001f,0x80050005,0x90070006,
        0x03000008,0x80110001,0x80e40003,0x90e40006,
        0x03000005,0x80010001,0x80000001,0xa000004c,
        0x03000005,0x80070001,0x80000001,0xa0e40006,
        0x02000001,0x80070800,0x80e40001,0xffff)

class WrappedLeafTests(unittest.TestCase):
    def test_only_diffuse_saturation_and_two_insertions_change(self):
        original=fixture()
        with patch.object(semantics,'reflect',return_value=[]):changed,meta=leaf_wrapped._transform(original)
        first,dot=meta['definition_insert_offset'],meta['diffuse_dot_offset']
        # Authored fixture starts with the diffuse dot; insertion ordering must
        # handle DEF and modified instruction at the same original offset.
        restored=changed[:first]+changed[first+24:]
        mad=struct.unpack('<5I',restored[dot+16:dot+36]);self.assertEqual(mad[0],0x04000004)
        self.assertTrue(mad[1]&0x100000);self.assertEqual(mad[3]&2047,meta['local_constant'])
        restored=bytearray(restored[:dot+16]+restored[dot+36:]);dest=struct.unpack_from('<I',restored,dot+4)[0]
        self.assertFalse(dest&0x100000);struct.pack_into('<I',restored,dot+4,dest|0x100000)
        self.assertEqual(bytes(restored),original);self.assertEqual(len(changed),len(original)+44)
    def test_fixed_wrap_not_applied_to_already_clipped_dot(self):
        for value,expected in((-1,0),(-.75,0),(-.5,0),(-.25,1/6),(0,1/3),(.5,2/3),(1,1)):
            self.assertAlmostEqual(leaf_wrapped.wrapped(value),expected)
        self.assertEqual(leaf_wrapped.wrapped(-.75),0);self.assertGreater(leaf_wrapped.wrapped(max(0,-.75)),0)
        for i in range(2001):self.assertTrue(0<=leaf_wrapped.wrapped(-1+i/1000)<=1)
    def test_typed_diffuse_chain_and_constant_allocator_strict(self):
        for bad in(fixture().replace(packed(0xa000004c),packed(0xa000004d)),
                   fixture().replace(packed(0x80110001),packed(0x80010001)),
                   fixture().replace(packed(0x90e40006),packed(0x90aa0006))):
            with self.assertRaises(ValueError):leaf_wrapped._transform(bad)
        with patch.object(semantics,'reflect',return_value=[{'register_set':'float4','register_index':223,'register_count':1}]):
            _,meta=leaf_wrapped._transform(fixture());self.assertEqual(meta['local_constant'],222)
        with patch.object(semantics,'reflect',return_value=[{'register_set':'float4','register_index':0,'register_count':224}]):
            with self.assertRaises(ValueError):leaf_wrapped._transform(fixture())
    def test_unavailable_resource_and_nonfinite_model_refused(self):
        with self.assertRaises(ValueError):leaf_wrapped.generate('arbitrary',fixture())
        for x in(math.inf,math.nan,True):
            with self.assertRaises(ValueError):leaf_wrapped.wrapped(x)
if __name__=='__main__':unittest.main()
