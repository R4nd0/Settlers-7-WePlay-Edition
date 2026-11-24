import struct
import unittest

from tools.material_shaders import leaf_lobe
from tools.material_shaders.test_bytecode import evaluate,packed


def fixture(dot_squared=.25):
    return (packed(0xfffe0300,0x0200001f,0x80070005,0xe00f0002,
                   0x0200001f,0x80000000,0xe00f0000)
            +struct.pack("<2I4f",0x05000051,0xa00f0000,dot_squared,.6,.2,.1)
            +packed(0x02000001,0x800f0000,0xa0e40000,
                    0x02000001,0xe00d0002,0xa0e40000,
                    0x03000005,0xe0020002,0x80000000,0x80000000,
                    0x02000001,0xe00f0000,0xa0e40000,0xffff))


class LeafLobeTests(unittest.TestCase):
    def test_broadens_angle_without_touching_other_outputs(self):
        original=fixture();changed,metadata=leaf_lobe._transform(original)
        before=evaluate(original,stage="vertex");after=evaluate(changed,stage="vertex")
        self.assertEqual(before[(6,2)][1],.0625)
        self.assertEqual(after[(6,2)][1],.25)
        self.assertEqual(before[(6,0)],after[(6,0)])
        self.assertEqual(before[(6,2)][::2],after[(6,2)][::2])
        self.assertEqual(before[(6,2)][3],after[(6,2)][3])
        self.assertEqual(len(changed),len(original)-4)
        self.assertFalse(metadata["replacement_ready"])

    def test_zero_stays_zero_and_candidate_is_clamped(self):
        for value,expected in ((0,0),(1,1),(2,1),(-1,0)):
            changed,_=leaf_lobe._transform(fixture(value))
            self.assertEqual(evaluate(changed,stage="vertex")[(6,2)][1],expected)

    def test_ambiguous_or_modified_writer_refused(self):
        original=fixture()
        cases=(original.replace(packed(0xe0020002),packed(0xe0030002)),
               original.replace(packed(0x80000000,0x80000000),packed(0x80000000,0x80550000)),
               original[:-4]+packed(0x02000001,0xe0020002,0x80000000,0xffff),
               original.replace(packed(0x03000005),packed(0x03000002)))
        for program in cases:
            with self.subTest(program=program),self.assertRaises(ValueError):leaf_lobe._transform(program)

    def test_public_entry_point_never_accepts_unpinned_program(self):
        with self.assertRaisesRegex(ValueError,"resource identity"):
            leaf_lobe.generate("Graphics/Shaders/SpeedTree_ATI_High.ShaderLib",fixture())


if __name__=="__main__":unittest.main()
