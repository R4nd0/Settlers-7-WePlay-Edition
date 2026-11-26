"""Authored minimal bytecode only; no original resources or scene geometry."""
from contextlib import contextmanager
import math,struct,unittest
from unittest.mock import patch
from tools.material_shaders import bytecode,leaf_hemi,recipes,shaderlib
from tools.material_shaders.test_bytecode import packed
from tools.material_shaders.test_leaf_lobe import fixture as lobe_fixture

def vertex():
    # Retain an authored legacy backlight writer so old/new recipes can be
    # generated from the same fixture resource without borrowing game code.
    return lobe_fixture()[:-4]+packed(0x0200001f,0x80010005,0xe0070003,
        0x05000051,0xa00f0005,0x40000000,0xbf800000,0,0x3f800000,
        0x02000001,0x80070004,0x90e40004,0x02000001,0xe0070003,0x80e40004,
        0x02000001,0x80070004,0xa0000005,0xffff)
def pixel(extra=False):
    raw=packed(0xffff0300,0x0200001f,0x80010005,0x90040002,
        0x05000051,0xa00f0000,0xbf000000,0x3f000000,0x420c0000,0x3f800000,
        0x04000004,0x80180000,0x90aa0002,0xa0550000,0xa0550000)
    if extra:raw+=packed(0x02000001,0x80010001,0x90000002)
    return raw+packed(0x02000001,0x80070800,0x80ff0000,0xffff)
@contextmanager
def authored():
    with patch.multiple(recipes,ORIGINAL_VERTEX=shaderlib.digest(vertex()),ORIGINAL_PIXEL=shaderlib.digest(pixel())):yield

class LeafHemisphereTests(unittest.TestCase):
    def test_original_bytes_unchanged_only_output_z_mad_inserted(self):
        with authored():
            original=vertex();changed,meta=leaf_hemi.transform(original,pixel());offset=meta['insert_byte_offset']
            self.assertEqual(changed[:offset],original[:offset]);self.assertEqual(changed[offset+20:],original[offset:])
            ins=struct.unpack('<5I',changed[offset:offset+20]);self.assertEqual(ins[0],0x04000004)
            self.assertEqual(bytecode.register_type(ins[1]),6);self.assertEqual((ins[1]>>16)&15,4)
            self.assertEqual(ins[1]&2047,meta['vertex_output_register'])
            self.assertEqual(ins[2]&2047,meta['worldnormal_temporary']);self.assertEqual(bytecode.register_type(ins[2]),0)
            self.assertEqual((ins[2]>>16)&255,0xaa);self.assertEqual(ins[3:],(0xa0000005,0xa0550005))
            self.assertEqual(meta['pixel_use_proof']['consumer_count'],1)
            self.assertFalse(meta['actual_normal_distribution_measured']);self.assertFalse(meta['visible_gain_qualified'])
    def test_hardware_saturation_floor_top_and_directional_ratio(self):
        for i in range(2001):
            z=-1+i/1000;old=leaf_hemi.original_hemisphere(z);new=leaf_hemi.hemisphere(z)
            self.assertAlmostEqual(new,.5+.5*min(1,max(0,.5*(2*z-1)+.5)))
            self.assertGreaterEqual(new,.5);self.assertLessEqual(new,1)
            self.assertGreaterEqual(new/old,2/3-1e-12);self.assertLessEqual(new/old,1+1e-12)
        self.assertEqual(leaf_hemi.hemisphere(-1),.5);self.assertEqual(leaf_hemi.hemisphere(1),1)
        self.assertEqual(leaf_hemi.original_hemisphere(0),.75);self.assertEqual(leaf_hemi.hemisphere(0),.5)
        self.assertEqual(leaf_hemi.original_hemisphere(.5),.875);self.assertEqual(leaf_hemi.hemisphere(.5),.75)
    def test_other_pixel_consumers_and_wrong_components_refused(self):
        with self.assertRaisesRegex(ValueError,'other pixel consumers'):leaf_hemi._pixel_proof(pixel(True))
        wrong=pixel().replace(packed(0x90aa0002),packed(0x90550002))
        with self.assertRaises(ValueError):leaf_hemi._pixel_proof(wrong)
    def test_exact_pair_and_original_constants_required(self):
        with authored():
            changed,_=leaf_hemi.transform(vertex(),pixel())
            with self.assertRaises(ValueError):leaf_hemi.transform(changed,pixel())
        bad=vertex().replace(packed(0x40000000),packed(0x40400000))
        with patch.multiple(recipes,ORIGINAL_VERTEX=shaderlib.digest(bad),ORIGINAL_PIXEL=shaderlib.digest(pixel())):
            with self.assertRaisesRegex(ValueError,'local2/-1'):leaf_hemi.transform(bad,pixel())
        with self.assertRaises(ValueError):leaf_hemi.generate('arbitrary',vertex())
    def test_nonfinite_model_refused_no_scene_distribution_claim(self):
        for x in(math.nan,math.inf,True):
            with self.assertRaises(ValueError):leaf_hemi.hemisphere(x)
        values=[leaf_hemi.hemisphere(z)/leaf_hemi.original_hemisphere(z)for z in(-1,-.5,0,.5,1)]
        self.assertGreater(len(set(values)),2)

if __name__=='__main__':unittest.main()
