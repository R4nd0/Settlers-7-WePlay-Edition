"""CPU composition checks with authored HLSL; no GPU or game is started."""
import os
import unittest
import struct

from . import on12_motion_program as motion,on12_previous_source as previous
from .test_on12_previous_source import SOURCE


@unittest.skipUnless(os.name=='nt','Windows CPU shader compiler')
class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.current=previous.compile_vertex(SOURCE,'Scene')
        generated,_=previous.prepare(SOURCE,'Scene');cls.previous=previous.compile_previous(generated)

    def test_original_instructions_and_outputs_are_preserved(self):
        merged=motion.merge_vertex(self.current,self.previous)
        old=motion.checked(self.current);new=motion.checked(merged.code)
        expected=[i.words for i in old if i.opcode not in motion.SETUP]
        observed=[i.words for i in new if i.opcode not in motion.SETUP and not(len(i.words)>1 and motion.register_type(i.words[1])==motion.OUTPUT and i.words[1]&motion.INDEX==merged.current_output)]
        self.assertEqual(observed[:len(expected)],expected)
        self.assertEqual(len(merged.original_execution_offsets),len(expected))
        for at,words in zip(merged.original_execution_offsets,expected):self.assertEqual(struct.unpack_from('<'+str(len(words))+'I',merged.code,at),words)
        inputs,outputs,samplers=motion.declarations(old)
        ni,no,ns=motion.declarations(new)
        self.assertEqual(ni,inputs)
        for key,value in outputs.items():self.assertEqual(no[key],value)
        self.assertEqual(motion.semantic(no[merged.previous_output][0]),(5,merged.texcoord))
        self.assertIn(3,ns);self.assertTrue(merged.relocated_float_constants)

    def test_truncation_and_sampler_collisions_are_rejected(self):
        for current,prior in ((self.current[:-4],self.previous),(self.current,self.previous[:-4])):
            with self.assertRaises(ValueError):motion.merge_vertex(current,prior)
        with self.assertRaises(ValueError):motion.merge_vertex(self.current,self.previous,previous_sampler=0)

    def test_previous_external_constants_cannot_be_hidden(self):
        with self.assertRaises(ValueError):motion.merge_vertex(self.current,self.current)

    def test_relative_internal_identity_table_remains_contiguous(self):
        source=SOURCE.replace('p.xyz+=','p.x+=input.position[(int)input.bone];p.xyz+=')
        current=previous.compile_vertex(source,'Scene');generated,_=previous.prepare(source,'Scene');prior=previous.compile_previous(generated)
        relative=[ins for ins in motion.checked(prior)if any(motion.register_type(ins.words[at])==motion.FLOAT and ins.words[at]&motion.RELATIVE for at in motion.parameters(ins))]
        self.assertTrue(relative)
        merged=motion.merge_vertex(current,prior)
        offsets={new-old for old,new in merged.relocated_float_constants}
        self.assertEqual(len(offsets),1)

    def test_previous_compiler_input_order_is_remapped_by_semantic(self):
        words=list(struct.unpack('<'+str(len(self.previous)//4)+'I',self.previous))
        for ins in motion.checked(self.previous):
            for at in motion.parameters(ins):
                token=ins.words[at]
                if motion.register_type(token)==motion.INPUT:
                    words[ins.offset//4+at]=motion.replaced(token,motion.INPUT,15-(token&motion.INDEX))
        reordered=struct.pack('<'+str(len(words))+'I',*words)
        merged=motion.merge_vertex(self.current,reordered)
        inputs=motion.declarations(motion.checked(self.current))[0]
        self.assertEqual(motion.declarations(motion.checked(merged.code))[0],inputs)
        for ins in motion.checked(merged.code):
            for at in motion.parameters(ins):
                token=ins.words[at]
                if motion.register_type(token)==motion.INPUT:self.assertIn(token&motion.INDEX,inputs)

    def test_occupied_sampler_and_full_constant_bank_fail_closed(self):
        source=SOURCE.replace('float4 p=input.position;','float4 p=input.position;p.x+=tex2Dlod(noise,float4(0,0,0,0)).x;')
        source='sampler2D noise:register(s3);\n'+source
        current=previous.compile_vertex(source,'Scene')
        with self.assertRaises(ValueError):motion.merge_vertex(current,self.previous)
        full='float4 all[256]:register(c0);struct I{float4 p:POSITION0;float b:TEXCOORD0;};struct O{float4 p:POSITION0;};O V(I i){O o;o.p=i.p+all[(int)i.b];return o;}'
        current=previous.compile_vertex(full,'V')
        with self.assertRaises(ValueError):motion.merge_vertex(current,self.previous)

    def test_omitted_ctab_entry_does_not_hide_a_current_constant_read(self):
        source='float g_Unbound;\n'+SOURCE.replace('p.xyz+=','p.x+=g_Unbound;p.xyz+=')
        generated,_=previous.prepare(source,'Scene');code=previous.compile_vertex(generated,previous.PREFIX+'Position')
        patched=bytearray(code)
        for ins in motion.checked(code):
            if ins.opcode!=motion.COMMENT or ins.words[1]!=0x42415443:continue
            base=ins.offset+8;count,offset=struct.unpack_from('<2I',code,base+12)
            for index in range(count):
                entry=base+offset+index*20;name=struct.unpack_from('<I',code,entry)[0];end=code.index(b'\0',base+name)
                if code[base+name:end]==(previous.PREFIX+'Constants').encode():
                    patched[base+offset:base+offset+20]=code[entry:entry+20];struct.pack_into('<I',patched,base+12,1);break
        with self.assertRaisesRegex(ValueError,'undefined'):motion.merge_vertex(self.current,bytes(patched))


if __name__=='__main__':unittest.main()
