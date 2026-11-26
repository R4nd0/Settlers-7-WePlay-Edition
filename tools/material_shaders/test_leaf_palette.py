import struct,unittest
from unittest.mock import patch
from tools.material_shaders import leaf_palette,leaf_wrapped,recipes,shaderlib,semantics,bytecode
from tools.material_shaders.test_leaf_wrapped import fixture

class PaletteTests(unittest.TestCase):
    def test_three_curves_change_only_authored_two_float_literals(self):
        with patch.object(semantics,'reflect',return_value=[]):base,meta=leaf_wrapped._transform(fixture())
        generated=recipes.Generated(base,dict(local_constant=meta['local_constant']))
        with patch.object(leaf_wrapped,'generate',return_value=generated):
            programs=[leaf_palette.generate('authored',b'',i).program for i in range(3)]
            self.assertEqual(len(set(programs)),3);self.assertEqual(programs[1],base)
            at=next(r.offset+8 for r in bytecode.decode(base)if r.opcode==81)
            for i,program in enumerate(programs):
                self.assertEqual(program[:at],base[:at]);self.assertEqual(program[at+8:],base[at+8:])
                self.assertEqual(program[at:at+8],struct.pack('<2f',*leaf_palette.CURVES[i]))
        for slope,bias in leaf_palette.CURVES:
            self.assertAlmostEqual(slope+bias,1)
            self.assertLessEqual(max(0,slope*(-1)+bias),0)
            self.assertTrue(all(0<=min(1,max(0,slope*(-1+j/100)+bias))<=1 for j in range(201)))
    def test_only_fixed_indices_and_owned_source_allowed(self):
        for value in(-1,3,True,'A',None):
            with self.assertRaises(ValueError):leaf_palette.generate('x',b'',value)
        with self.assertRaises(ValueError):leaf_palette.generate('arbitrary',b'',0)

if __name__=='__main__':unittest.main()
