import math,struct,unittest
from tools.material_shaders import bytecode,water,water_balanced,water_palette as palette
from tools.material_shaders.test_water import fixture,evaluate
class WaterPaletteTests(unittest.TestCase):
    def candidates(self):
        a,meta=water._transform(fixture());b,_=water_balanced._balance(a,meta['local_constants'][0]);return b,meta,[palette._transform(b,meta['local_constants'][0],i)for i in range(3)]
    def test_exact_B_and_only_declared_local_optical_float4_changes(self):
        b,meta,programs=self.candidates();self.assertEqual(programs[0],b);self.assertEqual(len(set(programs)),3)
        definition=next(d for d in bytecode.decode(b)if d.opcode==81 and d.words[1]==0xa00f0000|meta['local_constants'][0]);at=definition.offset+8
        for i,p in enumerate(programs):
            self.assertEqual(p[:at],b[:at]);self.assertEqual(p[at+16:],b[at+16:]);self.assertEqual(len(p),len(b))
            self.assertEqual(water._outputs(p),water._outputs(b))
            self.assertEqual([x.words for x in bytecode.decode(p)if x.opcode in(66,95)],[x.words for x in bytecode.decode(b)if x.opcode in(66,95)])
    def test_actual_bytecode_matches_fixed_reference_and_B_specular_alpha_unchanged(self):
        b,_,programs=self.candidates()
        for i,p in enumerate(programs):
            for cosine in(-.2,0,.1,.3,.5,.8,1,1.2):
                for depth in(0,.1,.25,.5,.75,.9,1):
                    for reflectiveness in(0,.4,1,3):
                        actual=evaluate(p,cosine=cosine,depth=depth,reflectiveness=reflectiveness)
                        expected=palette.response(i,cosine,depth,reflectiveness)
                        self.assertAlmostEqual(actual[0],expected['reflection'],places=6);self.assertAlmostEqual(actual[1],expected['optical_depth_tint'],places=6)
                        self.assertEqual(actual[2:],evaluate(b,cosine=cosine,depth=depth,reflectiveness=reflectiveness)[2:])
    def test_monotone_bounded_depth_and_reflection_plus_distinct_fixed_policies(self):
        for i in range(3):
            curve=[palette.response(i,.5,j/1000)['optical_depth_tint']for j in range(1001)]
            self.assertEqual(curve,sorted(curve));self.assertEqual(curve[0],0);self.assertEqual(curve[-1],1)
            self.assertTrue(all(0<=v<=1 for v in curve))
            reflect=[palette.response(i,j/1000,.5)['reflection']for j in range(1001)]
            self.assertEqual(reflect,sorted(reflect,reverse=True));self.assertTrue(all(0<=v<=1 for v in reflect))
        self.assertAlmostEqual(palette.response(1,.5,.5)['optical_depth_tint'],.425)
        self.assertLess(palette.response(1,.5,.5)['reflection'],palette.response(0,.5,.5)['reflection'])
        self.assertGreater(palette.response(2,1,.5)['reflection'],palette.response(0,1,.5)['reflection'])
        self.assertLess(palette.response(2,0,.5)['reflection'],palette.response(0,0,.5)['reflection'])
    def test_unknown_source_modified_B_and_arbitrary_policy_refused(self):
        b,meta,_=self.candidates();register=meta['local_constants'][0]
        for index in(-1,3,True,'B',None):
            with self.assertRaises(ValueError):palette._transform(b,register,index)
        for register in(-1,224,True,223.0):
            with self.assertRaises(ValueError):palette._transform(b,register,1)
        with self.assertRaises(ValueError):palette._transform(b.replace(struct.pack('<f',.05),struct.pack('<f',.051)),meta['local_constants'][0],1)
        with self.assertRaises(ValueError):palette.generate('arbitrary',b'',0)
        for v in(True,math.nan,math.inf,'1'):
            with self.assertRaises(ValueError):palette.response(0,v,.5)
if __name__=='__main__':unittest.main(verbosity=2)
