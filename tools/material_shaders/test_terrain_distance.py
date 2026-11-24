import math
import struct
import unittest

from tools.material_shaders import bytecode,terrain_distance
from tools.material_shaders.test_bytecode import packed


def fixture(role):
    c2=(.000244140625,.99951171875,19200,3200) if role=="BumpLayerShader" else (.000244140625,.99951171875,3200,1/1200)
    c3=(1/4200,1/1200,.5,-.5) if role=="BumpLayerShader" else (.5,-.5,10,-20)
    return packed(0xfffe0300)+struct.pack("<2I4f2I4f",0x05000051,0xa00f0002,*c2,0x05000051,0xa00f0003,*c3)+packed(0xffff)


class TerrainDistanceTests(unittest.TestCase):
    def test_only_proven_near_literal_components_change_far_fade_survives(self):
        for role in terrain_distance.LAYOUTS:
            original=fixture(role);changed,metadata=terrain_distance._transform(original,role)
            before=bytecode.decode(original,stage="vertex");after=bytecode.decode(changed,stage="vertex")
            points=set(terrain_distance.LAYOUTS[role])
            for a,b in zip(before,after):
                register=a.words[1]&2047
                for index in range(4):
                    if (register,index) not in points:self.assertEqual(a.words[2+index],b.words[2+index])
            self.assertEqual(len(original),len(changed));self.assertTrue(metadata["far_layer_visibility_preserved"])
            self.assertFalse(metadata["replacement_ready"])

    def test_original_interval_is_bit_identical(self):
        for role in terrain_distance.LAYOUTS:
            original=fixture(role)
            self.assertEqual(terrain_distance._transform(original,role,full=2000,zero=3200)[0],original)

    def test_bounded_candidate_changes_distance_weight_not_colour_gain(self):
        self.assertEqual(terrain_distance.weight(2000),1)
        self.assertEqual(terrain_distance.weight(3200),1)
        self.assertAlmostEqual(terrain_distance.weight(8500),4300/4800)
        self.assertEqual(terrain_distance.weight(12800),0)
        self.assertEqual(terrain_distance.weight(15000),0)
        self.assertEqual(terrain_distance.weight(3200,full=2000,zero=3200),0)
        for full,zero in ((True,12800),(math.nan,12800),(2000,math.inf),(1999,3200),(8001,12800),(8000,12801),(8000,8500)):
            with self.subTest(full=full,zero=zero),self.assertRaises(ValueError):terrain_distance.weight(8500,full=full,zero=zero)

    def test_wrong_role_or_literal_and_unpinned_resource_refused(self):
        with self.assertRaises(ValueError):terrain_distance._transform(fixture("BumpLayerShader"),"Other")
        changed=fixture("BumpLayerShader").replace(struct.pack("<f",3200),struct.pack("<f",3201))
        with self.assertRaisesRegex(ValueError,"literal mismatch"):terrain_distance._transform(changed,"BumpLayerShader")
        with self.assertRaisesRegex(ValueError,"resource identity"):
            terrain_distance.generate("Graphics/Shaders/Terrain_ATI_High.ShaderLib",fixture("BumpLayerShader"),"BumpLayerShader")


if __name__=="__main__":unittest.main()
