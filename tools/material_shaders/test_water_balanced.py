import math
import struct
import unittest

from tools.material_shaders import bytecode, water, water_balanced as balanced
from tools.material_shaders.test_water import fixture,evaluate


class BalancedWaterTests(unittest.TestCase):
    def candidate(self):
        a,meta=water._transform(fixture())
        b,details=balanced._balance(a,meta['local_constants'][0])
        return a,b,meta,details

    def test_only_two_optical_floats_change_A_otherwise_immutable(self):
        a,b,meta,details=self.candidate()
        constant=meta['local_constants'][0]
        definition=next(i for i in bytecode.decode(a) if i.opcode==81 and i.words[1]==0xa00f0000|constant)
        at=definition.offset+8
        self.assertEqual(a[:at],b[:at]);self.assertEqual(a[at+8:],b[at+8:]);self.assertEqual(len(a),len(b))
        self.assertEqual(water._outputs(a),water._outputs(b))
        self.assertEqual(details['variant'],2)
        self.assertTrue(details['anti_glitter_specular_preserved_from_A'])
        self.assertFalse(details['global_colour_grade'])

    def test_emitted_equation_matches_reference_over_view_depth_classes(self):
        _,b,_,_=self.candidate()
        for cosine in (-.2,0,.1,.3,.5,.8,1,1.2):
            for depth in (0,.1,.25,.5,.75,.9,1):
                for reflection in (0,.4,1,3):
                    actual=evaluate(b,cosine=cosine,depth=depth,reflectiveness=reflection)
                    expected=balanced.response(cosine,depth,reflection)
                    self.assertAlmostEqual(actual[0],expected['reflection'],places=6)
                    self.assertAlmostEqual(actual[1],expected['optical_depth_tint'],places=6)
                    self.assertEqual(actual[3],1)

    def test_more_normal_angle_gloss_but_lower_extreme_grazing(self):
        a,b,_,_=self.candidate()
        self.assertAlmostEqual(evaluate(b,cosine=1)[0],.05,places=6)
        self.assertAlmostEqual(evaluate(b,cosine=0)[0],.78,places=6)
        self.assertAlmostEqual(evaluate(b,cosine=.5)[0],.0728125,places=6)
        for cosine in (.3,.5,.8,1):self.assertGreater(evaluate(b,cosine=cosine)[0],evaluate(a,cosine=cosine)[0])
        for cosine in (0,.1):self.assertLess(evaluate(b,cosine=cosine)[0],evaluate(a,cosine=cosine)[0])

    def test_depth_and_broad_specular_are_bitwise_same_equations(self):
        a,b,_,_=self.candidate()
        for depth in (0,.1,.2,.5,.8,1):
            for dot in (0,.5,.9,.99,1):
                before=evaluate(a,depth=depth,highlight=dot);after=evaluate(b,depth=depth,highlight=dot)
                self.assertEqual(before[1:],after[1:])

    def test_unknown_missing_ambiguous_or_modified_A_policy_refused(self):
        a,_,meta,_=self.candidate();register=meta['local_constants'][0]
        for n in (-1,224,True,'223'):
            with self.assertRaises(ValueError):balanced._balance(a,n)
        with self.assertRaises(ValueError):balanced._balance(a,register-2)
        with self.assertRaises(ValueError):balanced._balance(a.replace(struct.pack('<f',.02),struct.pack('<f',.03)),register)
        with self.assertRaisesRegex(ValueError,'resource identity'):balanced.generate(next(iter(water.RESOURCES)),fixture())

    def test_reference_rejects_nonfinite_and_boolean_inputs(self):
        for value in (True,math.nan,math.inf,'1'):
            with self.assertRaises(ValueError):balanced.response(value,.5)


if __name__=='__main__':unittest.main()
