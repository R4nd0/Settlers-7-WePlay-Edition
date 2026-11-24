import math
import struct
import unittest

from tools.material_shaders import bytecode,terrain
from tools.material_shaders.test_bytecode import packed
from tools.material_shaders.test_semantics import fixture as ctab_fixture


def fixture():
    comment=bytearray(ctab_fixture(name=b"g_DetailNormalMap",register_set=3,index=5)[4:-8])
    # CTAB starts eight bytes after the comment token/FOURCC; its type starts48.
    struct.pack_into("<6HI",comment,8+48,4,12,1,1,1,0,0)
    return (packed(0xffff0300)+bytes(comment)+packed(0x0200001f,0x90000000,0xa00f0805,
            0x03000042,0x800f0000,0x90e40000,0xa0e40805,
            0x02000001,0x800f0800,0x80e40000,0xffff))


def evaluate(program,texel):
    regs={}
    def key(token):return (bytecode.register_type(token),token&0x7ff)
    def read(token):return [regs[key(token)][(token>>(16+2*i))&3]for i in range(4)]
    for item in bytecode.decode(program):
        if item.opcode in (31,0xfffe):continue
        if item.opcode==81:regs[key(item.words[1])]=list(struct.unpack("<4f",packed(*item.words[2:])));continue
        dst=item.words[1]
        if item.opcode==66:value=list(texel)
        else:
            first=read(item.words[2])
            if item.opcode==1:value=first
            elif item.opcode==7:value=[1/math.sqrt(abs(first[0]))]*4
            else:
                second=read(item.words[3])
                if item.opcode==5:value=[a*b for a,b in zip(first,second)]
                elif item.opcode==10:value=[min(a,b)for a,b in zip(first,second)]
                elif item.opcode==11:value=[max(a,b)for a,b in zip(first,second)]
                elif item.opcode in (4,90):
                    third=read(item.words[4])
                    value=([a*b+c for a,b,c in zip(first,second,third)]if item.opcode==4
                           else[first[0]*second[0]+first[1]*second[1]+third[0]]*4)
                else:raise AssertionError("Unsupported authored fixture operation")
        output=regs.setdefault(key(dst),[0.0]*4)
        for index in range(4):
            if dst&(1<<(16+index)):output[index]=value[index]
    return regs[(8,0)]


class TerrainNormalExtensionTests(unittest.TestCase):
    def test_only_packed_normal_ag_changes_and_flat_normal_stays_flat(self):
        original=fixture();changed,report=terrain.extend_normal_detail(original,strength=1.1)
        result=evaluate(changed,(.3,.7,.8,.8))
        self.assertEqual((result[0],result[2]),(.3,.8))
        self.assertAlmostEqual(result[1],.72,places=6)
        self.assertAlmostEqual(result[3],.83,places=6)
        self.assertEqual(evaluate(changed,(.3,.5,.8,.5)),[.3,.5,.8,.5])
        self.assertEqual(report["normal_fetches"],1)
        self.assertFalse(report["runtime_verified"])

    def test_radial_limit_prevents_original_sqrt_negative_for_steep_normals(self):
        changed,_=terrain.extend_normal_detail(fixture(),strength=1.25)
        for texel in ((.3,.85,.8,.85),(.3,1,.8,1),(.3,0,.8,0),(.3,.5,.8,.5)):
            result=evaluate(changed,texel);x=2*result[3]-1;y=2*result[1]-1
            self.assertLessEqual(x*x+y*y,.981)
            self.assertTrue(math.isfinite(math.sqrt(1-x*x-y*y)))

    def test_wrong_sampler_binding_swizzle_predication_and_strength_refused(self):
        source=fixture()
        for changed in (source.replace(b"g_DetailNormalMap",b"g_AnotherSampler"),
                        source.replace(packed(0xa0e40805),packed(0xa0000805)),
                        source.replace(packed(0x03000042),packed(0x13000042))):
            with self.assertRaises(ValueError):terrain.extend_normal_detail(changed)
        for strength in (math.nan,math.inf,.9,1.3,True):
            with self.subTest(strength=strength),self.assertRaises(ValueError):terrain.extend_normal_detail(source,strength=strength)


if __name__=="__main__":unittest.main()
