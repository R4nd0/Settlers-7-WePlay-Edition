import math
import struct
import unittest

from tools.material_shaders import bytecode, water


def packed(*words):
    return struct.pack('<'+'I'*len(words),*words)


def fixture():
    # Entirely authored small test program. It models the three scalar effects,
    # without any game shader source, textures, original compiled body or data.
    defs=b''.join(struct.pack('<2I4f',0x05000051,0xa00f0000|index,*values) for index,values in (
        (0,(1,0,0,1)), (1,(0,0,1,0)), (4,(-1,0,0,0)),
        (12,(1,.8,.9,1.4)), (13,(.78,-.96,.04,-.05)), (14,(34,500,256,96))))
    return packed(0xffff0300)+defs+packed(
        0x0200001f,0x80000005,0x900f0000,
        0x0200001f,0x80020005,0x900f0003,
        0x0200001f,0x80060005,0x900f0006,
        0x02000001,0x80010000,0x90ff0006,
        0x03000020,0x80120001,0x80000000,0x90aa0006,
        0x02000001,0x80020002,0x80550001,
        0x02000001,0x80070001,0xa0e40001,
        0x03000008,0x80010000,0x91e40003,0x80e40001,
        0x03000005,0x80010000,0x80000000,0x80000000,
        0x03000005,0x80010000,0x80000000,0xa0ff000c,
        0x0300000b,0x80010001,0x80000000,0xa000000d,
        0x03000002,0x80010000,0x80000001,0xa055000d,
        0x03000002,0x80040000,0x81000001,0xa1000004,
        0x04000058,0x80010000,0x80000000,0xa0aa000d,0x80aa0000,
        0x03000005,0x80010000,0x80000000,0x90550006,
        0x02000001,0x80010002,0x80000000,
        0x02000001,0x80040000,0x90000000,
        0x03000020,0x80010001,0x80aa0000,0xa0ff000e,
        0x03000020,0x80020001,0x80aa0000,0xa0aa000e,
        0x02000001,0x800c0001,0x80550001,
        0x03000005,0x80070001,0x80000001,0x80f90001,
        0x02000001,0x80040002,0x80000001,
        0x02000001,0x80080002,0xa0ff0000,
        0x02000001,0x800f0800,0x80e40002,
        0xffff)


def evaluate(program, *, cosine=.5, depth=.25, fog=1.0, reflectiveness=1.0, highlight=.99):
    regs={(1,0):[highlight]*4,(1,3):[0,0,-cosine,0],(1,6):[0,reflectiveness,fog,depth]}
    def key(token):return bytecode.register_type(token),token&0x7ff
    def read(token):
        value=regs.get(key(token),[0.0]*4)
        result=[value[(token>>(16+2*i))&3] for i in range(4)]
        if token&0x0f000000==0x01000000:result=[-v for v in result]
        elif token&0x0f000000:raise AssertionError('Unsupported test source modifier')
        return result
    for item in bytecode.decode(program):
        if item.opcode in (31,0xfffe):continue
        if item.opcode==81:
            regs[key(item.words[1])]=list(struct.unpack('<4f',packed(*item.words[2:])));continue
        dst=item.words[1];a=read(item.words[2])
        if item.opcode==1:result=a
        else:
            b=read(item.words[3])
            if item.opcode==2:result=[x+y for x,y in zip(a,b)]
            elif item.opcode==5:result=[x*y for x,y in zip(a,b)]
            elif item.opcode==8:result=[sum(a[i]*b[i] for i in range(3))]*4
            elif item.opcode==11:result=[max(x,y) for x,y in zip(a,b)]
            elif item.opcode==32:result=[abs(a[0])**b[0]]*4
            elif item.opcode in (4,88):
                c=read(item.words[4])
                result=([x*y+z for x,y,z in zip(a,b,c)] if item.opcode==4 else
                        [y if x>=0 else z for x,y,z in zip(a,b,c)])
            else:raise AssertionError('Unsupported authored fixture instruction')
        if dst&0x00100000:result=[max(0,min(1,v)) for v in result]
        target=regs.setdefault(key(dst),[0.0]*4)
        for i in range(4):
            if dst&(1<<(16+i)):target[i]=result[i]
    return regs[(8,0)]


class WaterOpticsTests(unittest.TestCase):
    def test_authored_bytecode_matches_bounded_schlick_and_depth_equations(self):
        candidate,meta=water._transform(fixture())
        for cosine in (-.2,0,.1,.3,.5,.8,1,1.2):
            for depth in (0,.1,.25,.5,.75,.9,1):
                for reflection in (0,.4,1,3):
                    actual=evaluate(candidate,cosine=cosine,depth=depth,reflectiveness=reflection)
                    expected=water.response(cosine,depth,reflection)
                    self.assertAlmostEqual(actual[0],expected['reflection'],places=6)
                    self.assertAlmostEqual(actual[1],expected['optical_depth_tint'],places=6)
                    self.assertEqual(actual[3],1)
        self.assertFalse(meta['runtime_verified']);self.assertFalse(meta['replacement_ready'])

    def test_shallow_clearer_deep_more_separated_and_all_endpoints_preserved(self):
        candidate,_=water._transform(fixture())
        self.assertAlmostEqual(evaluate(candidate,depth=.2)[1],.104,places=6)
        self.assertAlmostEqual(evaluate(candidate,depth=.8)[1],.896,places=6)
        self.assertEqual(evaluate(candidate,depth=0)[1],0)
        self.assertEqual(evaluate(candidate,depth=1)[1],1)
        values=[evaluate(candidate,depth=i/100)[1] for i in range(101)]
        self.assertEqual(values,sorted(values))

    def test_angle_response_is_not_constant_rgb_gain(self):
        candidate,_=water._transform(fixture())
        self.assertAlmostEqual(evaluate(candidate,cosine=1)[0],.02,places=6)
        self.assertAlmostEqual(evaluate(candidate,cosine=0)[0],.85,places=6)
        # Reflection decreases when viewed from above and increases at grazing.
        self.assertLess(evaluate(candidate,cosine=.5)[0],evaluate(fixture(),cosine=.5)[0])
        self.assertGreater(evaluate(candidate,cosine=.1)[0],evaluate(fixture(),cosine=.1)[0])

    def test_actual_emitted_specular_exponents_and_energy_limit(self):
        candidate,_=water._transform(fixture())
        for dot in (0,.5,.9,.99,1):
            self.assertAlmostEqual(evaluate(candidate,highlight=dot)[2],
                                   dot**128*water.SPECULAR_PEAK_SCALE,places=6)
        ratio=water.SPECULAR_PEAK_SCALE*(352+1)/(128+1)
        self.assertGreater(ratio,.99);self.assertLess(ratio,1.01)
        self.assertLess(evaluate(candidate,highlight=1)[2],evaluate(fixture(),highlight=1)[2])
        self.assertGreater(evaluate(candidate,highlight=.99)[2],evaluate(fixture(),highlight=.99)[2])

    def test_fog_class_input_used_before_transfer_not_replaced_by_global_colour(self):
        candidate,_=water._transform(fixture())
        for fog in (.25,.5,.75,1):
            original_tint=.16**fog
            result=evaluate(candidate,depth=.16,fog=fog)
            self.assertAlmostEqual(result[1],original_tint**2*(3-2*original_tint),places=6)

    def test_output_writers_and_untouched_foam_values_are_preserved(self):
        source=fixture();candidate,meta=water._transform(source)
        self.assertEqual(water._outputs(source),water._outputs(candidate))
        definitions=[i for i in bytecode.decode(candidate) if i.opcode==81 and i.words[1]==0xa00f000e]
        self.assertEqual(struct.unpack('<4f',packed(*definitions[0].words[2:])),(34,500,96,32))
        self.assertEqual(meta['added_texture_reads'],0)
        self.assertLess(meta['conservative_instruction_slots'],512)

    def test_occupied_local_constant_is_not_reused(self):
        source=fixture();candidate,meta=water._transform(source[:4]+struct.pack('<2I4f',0x05000051,0xa00f00df,1,2,3,4)+source[4:])
        self.assertNotIn(223,meta['local_constants'])
        self.assertIn(struct.pack('<2I4f',0x05000051,0xa00f00df,1,2,3,4),candidate)

    def test_unrelated_render_target_and_alpha_writers_survive(self):
        source=fixture()[:-4]+packed(0x02000001,0x800f0801,0x80e40002,0xffff)
        candidate,_=water._transform(source)
        self.assertEqual(water._outputs(source),water._outputs(candidate))
        self.assertEqual(evaluate(candidate)[3],evaluate(source)[3])

    def test_missing_ambiguous_modified_or_controlled_original_refused(self):
        source=fixture()
        cases=(
            source.replace(packed(0x80120001),packed(0x80110001)),
            source.replace(packed(0x90550006),packed(0x90000006)),
            source.replace(struct.pack('<f',256),struct.pack('<f',255)),
            source.replace(packed(0x03000008),packed(0x13000008)),
            source[:-4]+packed(0x03000020,0x80120001,0x80000000,0x90aa0006,0xffff),
            source[:-4]+packed(0x00000028,0xffff))
        for program in cases:
            with self.subTest(program=program),self.assertRaises(ValueError):
                water._transform(program)

    def test_exact_owned_resource_required_before_any_rewrite(self):
        for name in (next(iter(water.RESOURCES)),'Graphics/Shaders/Unknown.ShaderLib'):
            with self.assertRaisesRegex(ValueError,'resource identity'):
                water.generate(name,fixture())

    def test_invalid_reference_inputs_rejected(self):
        for value in (True,math.nan,math.inf,'1'):
            with self.assertRaises(ValueError):water.response(value,.5)


if __name__=='__main__':unittest.main()
