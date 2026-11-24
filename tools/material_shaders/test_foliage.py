import math
import struct
import unittest
from unittest.mock import patch

from tools.material_shaders import foliage
from tools.material_shaders.test_bytecode import evaluate,packed


def vertex_fixture():
    return (packed(0xfffe0300,0x0200001f,0x80070005,0xe00f0002,
                   0x0200001f,0x80000000,0xe00f0000)
            +struct.pack("<2I4f",0x05000051,0xa00f0000,.7,.5,.2,.1)
            +packed(0x02000001,0xe00f0002,0xa0e40000,
                    0x02000001,0xe00f0000,0xa0e40000,0xffff))


class FoliageExtensionTests(unittest.TestCase):
    def test_only_backlight_component_changes_position_alpha_specular_selection_survive(self):
        original=vertex_fixture();changed,report=foliage.extend_leaf_backlight(original,factor=1.5)
        before=evaluate(original,stage="vertex");after=evaluate(changed,stage="vertex")
        self.assertEqual(after[(6,0)],before[(6,0)])
        self.assertEqual(after[(6,2)][0],before[(6,2)][0])
        self.assertAlmostEqual(after[(6,2)][1],.75)
        self.assertEqual(after[(6,2)][2:],before[(6,2)][2:])
        self.assertTrue(report["saturated_to_unit_interval"])
        self.assertFalse(report["runtime_verified"])

    def test_backlight_is_saturated_not_extrapolated(self):
        source=vertex_fixture().replace(struct.pack("<f",.5),struct.pack("<f",.9))
        changed,_=foliage.extend_leaf_backlight(source,factor=1.5)
        self.assertEqual(evaluate(changed,stage="vertex")[(6,2)][1],1.0)

    def test_missing_semantic_profile_and_nonfinite_factor_refused(self):
        source=vertex_fixture()
        for changed in (source.replace(packed(0x80070005),packed(0x80060005)),
                        source.replace(packed(0xfffe0300),packed(0xffff0300))):
            with self.assertRaises(ValueError):foliage.extend_leaf_backlight(changed)
        for factor in (math.nan,math.inf,.9,1.6,True):
            with self.subTest(factor=factor),self.assertRaises(ValueError):foliage.extend_leaf_backlight(source,factor=factor)

    def test_relative_constant_needs_complete_declared_array_footprint(self):
        source=vertex_fixture()
        # MOV r0, c[a0.x+120]. The additional address operand is explicit.
        relative=packed(0x03000001,0x800f0000,0xa0e42078,0xb0000000)
        source=source[:4]+relative+source[4:]
        with self.assertRaisesRegex(ValueError,"declared CTAB"):foliage.extend_leaf_backlight(source)
        binding={"register_set":"float4","register_index":120,"register_count":32,
                 "class":2,"type":3,"rows":4,"columns":4,"elements":8,"struct_members":0}
        with patch.object(foliage,"reflect",return_value=[binding]):
            changed,report=foliage.extend_leaf_backlight(source)
            self.assertTrue(report["relative_constant_ranges_verified_from_ctab"])
            self.assertEqual(report["local_constant"],255)
        binding["register_count"]=31
        with patch.object(foliage,"reflect",return_value=[binding]),self.assertRaises(ValueError):
            foliage.extend_leaf_backlight(source)


if __name__=="__main__":unittest.main()
