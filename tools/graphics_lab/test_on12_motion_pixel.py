"""CPU motion-output composition and coordinate tests; no GPU starts."""
import math
import os
import unittest

from . import on12_motion_pixel as pixel,on12_previous_source as compiler,on12_motion_program as vertex


class ReferenceTests(unittest.TestCase):
    def test_unjittered_pixel_units_and_previous_view_depth(self):
        self.assertEqual(pixel.reference_motion((.2,-.4,.9,2),(.2,-.4,.9,2),100,100), (0,0,2,1))
        result=pixel.reference_motion((.2,-.4,.9,2),(.21,-.385,.9,2),100,100,(.25,-.375),.7)
        self.assertAlmostEqual(result[0],0);self.assertAlmostEqual(result[1],0);self.assertEqual(result[2:],(2,.7))
        self.assertEqual(pixel.reference_motion((.4,-.4,.2,2),(.2,-.4,.9,2),100,100),(5,0,2,1))

    def test_invalid_previous_position_and_missing_history_are_zeroed(self):
        for position in ((0,0,0,0),(0,0,0,-1),(math.nan,0,0,1),(0,math.inf,0,1)):
            self.assertEqual(pixel.reference_motion(position,(0,0,0,1),100,100),(0,0,0,0))
        self.assertEqual(pixel.reference_motion((0,0,0,1),(0,0,0,1),100,100,confidence=0),(0,0,0,0))


@unittest.skipUnless(os.name=='nt','Windows CPU shader compiler')
class CompositionTests(unittest.TestCase):
    def test_original_color_and_alpha_discard_are_preserved(self):
        source='sampler2D albedo:register(s0);float threshold:register(c39);float4 PS(float2 uv:TEXCOORD0):COLOR0{float a=tex2D(albedo,uv).a;clip(a-threshold);return float4(uv,0,a);}'
        original=compiler.compile_shader(source,'PS','ps_3_0');merged=pixel.merge_pixel(original,1,2)
        before=[i.words for i in pixel.checked(original)if i.opcode not in vertex.SETUP]
        after=[i.words for i in pixel.checked(merged.code)if i.opcode not in vertex.SETUP]
        self.assertEqual(after[:len(before)],before)
        self.assertTrue(any((i[0]&0xffff)==65 for i in before))
        self.assertEqual(merged.target,1);self.assertNotEqual(merged.raster_register,39)

    def test_existing_mrt_is_not_overwritten(self):
        source='struct O{float4 a:COLOR0;float4 b:COLOR1;};O PS(float2 uv:TEXCOORD0){O o;o.a=float4(uv,0,1);o.b=1;return o;}'
        merged=pixel.merge_pixel(compiler.compile_shader(source,'PS','ps_3_0'),1,2)
        self.assertEqual(merged.target,2)

    def test_all_mrts_or_semantic_collision_are_rejected(self):
        source='struct O{float4 a:COLOR0;float4 b:COLOR1;float4 c:COLOR2;float4 d:COLOR3;};O PS(float2 uv:TEXCOORD0){O o;o.a=float4(uv,0,1);o.b=1;o.c=2;o.d=3;return o;}'
        with self.assertRaises(ValueError):pixel.merge_pixel(compiler.compile_shader(source,'PS','ps_3_0'),1,2)
        source='float4 PS(float4 p:TEXCOORD1):COLOR0{return p;}'
        with self.assertRaises(ValueError):pixel.merge_pixel(compiler.compile_shader(source,'PS','ps_3_0'),1,2)


if __name__=='__main__':unittest.main()
