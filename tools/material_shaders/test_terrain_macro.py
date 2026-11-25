"""Authored programs only; no original shader source/binaries or game IO."""
from contextlib import contextmanager
import math,struct,unittest
from unittest.mock import patch
from tools.material_shaders import bytecode,recipes,shaderlib,terrain_macro,terrain_distance
from tools.material_shaders.test_bytecode import packed
from tools.material_shaders.test_terrain_distance import fixture as distance_fixture

def fixture(role):
    target=terrain_macro.UV_OUTPUTS[role]
    return distance_fixture(role)[:-4]+packed(0x03000005,0xe00c0000|target,0xa0aa0043,0x90440000,0xffff)

@contextmanager
def authored():
    pairs={role:(shaderlib.digest(fixture(role)),"1"*64) for role in terrain_macro.DISTANCES}
    with patch.object(recipes,"TERRAIN_PAIRS",pairs):yield

class TerrainMacroTests(unittest.TestCase):
    def test_only_fade_literals_and_detail_uv_writer_change(self):
        with authored():
            for role in terrain_macro.DISTANCES:
                original=fixture(role);changed,meta=terrain_macro.transform(original,role)
                before=bytecode.decode(original,stage="vertex");after=list(bytecode.decode(changed,stage="vertex"))
                index=next(i for i,r in enumerate(before) if r.offset==meta["original_UV_writer_offset"])
                added=after.pop(index+1)
                self.assertEqual(added.opcode,5);self.assertEqual(added.words[1],before[index].words[1])
                self.assertEqual(added.words[2]&2047,meta["temporary"])
                self.assertEqual(added.words[3]&2047,3)
                self.assertEqual((added.words[3]>>16)&255,meta["local_half_component"]*0x55)
                self.assertEqual(len(changed),len(original)+16)
                for a,b in zip(before,after):
                    if a.offset==meta["original_UV_writer_offset"]:
                        self.assertEqual(a.words[0],b.words[0]);self.assertEqual(a.words[2:],b.words[2:])
                        self.assertEqual(a.words[1]&0xf0000,b.words[1]&0xf0000)
                    elif a.opcode==81:
                        for j,(x,y) in enumerate(zip(a.words,b.words)):
                            if a.offset+j*4 not in meta["changed_literal_offsets"]:self.assertEqual(x,y)
                    else:self.assertEqual(a.words,b.words)
                self.assertEqual(len(before),len(after));self.assertFalse(meta["visible_gain_qualified"])

    def test_half_frequency_and_integer_region_phase(self):
        for x in (0,.125,.25,.5,.75,1):self.assertEqual((8*x)*.5,4*x)
        self.assertEqual(4%1,0)
        for footprint in (2,4,8,16,64,256):self.assertEqual(math.log2(footprint*.5),math.log2(footprint)-1)
        self.assertEqual(terrain_macro.DISTANCES["BumpLayerShader"],(12000,18000))
        self.assertEqual(terrain_macro.DISTANCES["ParallaxLayerShader"],(8000,12800))
        self.assertGreater((18000-16856)/6000,0)

    def test_unpinned_program_role_resource_and_regenerated_output_refused(self):
        with authored():
            for role in terrain_macro.DISTANCES:
                original=fixture(role);changed,_=terrain_macro.transform(original,role)
                for program,kind in ((changed,role),(original,"other"),(original+b"x",role)):
                    with self.assertRaises(ValueError):terrain_macro.transform(program,kind)
                with self.assertRaisesRegex(ValueError,"resource"):terrain_macro.generate("other",original,role)

    def test_writer_shape_and_local_half_proofs_remain_required(self):
        role="BumpLayerShader";original=fixture(role)
        cases=(original.replace(packed(0xe00c0006),packed(0xe00f0006)),
               original.replace(packed(0xa0aa0043),packed(0xa0000043)),
               original.replace(struct.pack("<f",.5),struct.pack("<f",.6)))
        for program in cases:
            with patch.object(recipes,"TERRAIN_PAIRS",{role:(shaderlib.digest(program),"1"*64)}):
                with self.assertRaises(ValueError):terrain_macro.transform(program,role)

if __name__=="__main__":unittest.main()
