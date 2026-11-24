"""Authored shader programs only; guard and output/alpha/MRT semantics checks."""
import math
import struct
import unittest

from tools.material_shaders import bytecode


def packed(*words):
    return struct.pack("<"+"I"*len(words),*words)


def fixture():
    return (packed(0xffff0300)+struct.pack("<2I4f",0x05000051,0xa00f0000,.1,.2,.3,.4)
            +packed(0x02000001,0x800f0000,0xa0e40000,
                    0x03000002,0x800f0800,0x80e40000,0xa0e40000,
                    0x02000001,0x800f0801,0x80e40000,0xffff))


def evaluate(program,*,stage="pixel"):
    """Small independent MOV/ADD/MUL/DEF interpreter for authored fixtures."""
    registers={}
    def key(token):return (bytecode.register_type(token),token&0x7ff)
    def read(token):
        values=registers[key(token)]
        return [values[(token>>(16+2*index))&3] for index in range(4)]
    for item in bytecode.decode(program,stage=stage):
        if item.opcode==81:
            registers[key(item.words[1])]=list(struct.unpack("<4f",packed(*item.words[2:])))
            continue
        if item.opcode in (0,31,0xfffe):continue
        destination=item.words[1];source=read(item.words[2])
        if item.opcode in (2,5):
            other=read(item.words[3])
            source=[a+b if item.opcode==2 else a*b for a,b in zip(source,other)]
        elif item.opcode!=1:
            raise AssertionError("Fixture interpreter only covers authored operations")
        output=registers.setdefault(key(destination),[0.0]*4)
        for index in range(4):
            if destination&(1<<(16+index)):
                value=source[index]
                output[index]=min(1.0,max(0.0,value)) if destination&0x00100000 else value
    return registers


class BytecodeExtensionTests(unittest.TestCase):
    def test_original_alpha_mrt_and_material_inputs_survive_gain(self):
        source=fixture();before=evaluate(source)
        changed,report=bytecode.extend_rgb_gain(source,(.9,1.05,1.2));after=evaluate(changed)
        expected=[before[(8,0)][index]*gain for index,gain in enumerate((.9,1.05,1.2))]
        for value,wanted in zip(after[(8,0)][:3],expected):self.assertAlmostEqual(value,wanted,places=6)
        self.assertEqual(after[(8,0)][3],before[(8,0)][3])
        self.assertEqual(after[(8,1)],before[(8,1)])
        self.assertEqual(after[(2,0)],before[(2,0)])
        self.assertTrue(report["alpha_preserved"])
        self.assertTrue(report["other_mrt_outputs_preserved"])
        self.assertEqual(report["local_temporary"],1)
        self.assertEqual(report["local_constant"],223)
        self.assertFalse(report["runtime_verified"])

    def test_unity_gain_keeps_authored_output(self):
        source=fixture();changed,_=bytecode.extend_rgb_gain(source,(1,1,1))
        self.assertEqual(evaluate(changed)[(8,0)],evaluate(source)[(8,0)])

    def test_relative_matrix_subroutine_and_predication_refused(self):
        source=fixture()
        candidates=(source.replace(packed(0x80e40000),packed(0x80e42000),1),
                    source[:4]+packed(0x03000014,0x800f0002,0x80e40000,0xa0e40000)+source[4:],
                    source[:4]+packed(0x0000001c)+source[4:],
                    source.replace(packed(0x03000002),packed(0x13000002)))
        for candidate in candidates:
            with self.subTest(),self.assertRaises(ValueError):bytecode.extend_rgb_gain(candidate,(1,1.1,1))

    def test_all_temporary_registers_in_use_refused(self):
        instructions=b"".join(packed(0x02000001,0x800f0000|index,0xa0e40000) for index in range(32))
        source=fixture();source=source[:4]+instructions+source[4:]
        with self.assertRaisesRegex(ValueError,"temporary"):bytecode.extend_rgb_gain(source,(1,1,1))

    def test_all_float_constants_in_use_refused(self):
        instructions=b"".join(packed(0x02000001,0x800f0000,0xa0e40000|index) for index in range(224))
        source=fixture();source=source[:4]+instructions+source[4:]
        with self.assertRaisesRegex(ValueError,"constant"):bytecode.extend_rgb_gain(source,(1,1,1))

    def test_instruction_budget_and_nonfinite_out_of_range_values(self):
        source=fixture();long=source[:4]+packed(*([0]*512))+source[4:]
        with self.assertRaisesRegex(ValueError,"budget"):bytecode.extend_rgb_gain(long,(1,1,1))
        for gain in ((math.nan,1,1),(math.inf,1,1),(1.6,1,1),(.49,1,1),(True,1,1)):
            with self.subTest(gain=gain),self.assertRaises(ValueError):bytecode.extend_rgb_gain(source,gain)

    def test_malformed_profile_length_end_and_no_rgb_writes_refused(self):
        source=fixture()
        for candidate in (source[:-1],source[:-4],source+packed(0),source.replace(packed(0xffff0300),packed(0xffff0200)),
                          source.replace(packed(0x800f0800),packed(0x80080800))):
            with self.subTest(),self.assertRaises(ValueError):bytecode.extend_rgb_gain(candidate,(1,1,1))


if __name__=="__main__":unittest.main()
