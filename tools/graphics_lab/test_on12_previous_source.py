"""Authored CPU-only source reconstruction tests; no GPU or game starts."""
import struct
import unittest
import os

from . import on12_previous_source as previous

SOURCE='''
float4x4 g_ViewProjection : register(c12);
float4x4 g_World : register(c68);
float4 g_Bones[150] : register(c80);
float4 g_Wind : register(c43);
float g_Timer : register(c63);
struct Input {float4 position:POSITION0;float bone:TEXCOORD0;};
struct Output {float4 position:POSITION0;float2 uv:TEXCOORD0;};
Output Scene(Input input) {
    Output output;
    float4 p=input.position;
    p.xyz+=g_Bones[(int)input.bone].xyz;
    p.x+=sin(g_Timer+p.y)*g_Wind.x;
    output.position=mul(g_ViewProjection,mul(g_World,p));
    output.uv=p.xy;
    return output;
}
'''


class Tests(unittest.TestCase):
    def test_bones_world_camera_and_wind_use_previous_texture(self):
        source,uniforms=previous.prepare(SOURCE,'Scene')
        self.assertEqual(len(uniforms),5)
        self.assertIn('sampler2D S7TemporalPreviousConstants : register(s3)',source)
        self.assertNotIn('register(c',source)
        self.assertNotIn('g_Bones[',source)
        self.assertIn('sin(((float)S7TemporalPreviousRead',source)
        self.assertIn('transpose(float4x4(',source)
        self.assertTrue(source.endswith('return Scene(input).position;}\n'))

    def test_column_and_row_major_layout(self):
        column=previous.Uniform('m','float3x4','c',4,1,False,False)
        row=previous.Uniform('m','float3x4','c',4,1,False,True)
        self.assertEqual((column.stride,row.stride),(4,3))
        self.assertIn('transpose(float4x3(',column.expression())
        self.assertTrue(row.expression().startswith('float3x4('))

    def test_unsupported_or_ambiguous_source_is_rejected(self):
        for source in (SOURCE+'\n#include "outside.inc"',SOURCE.replace('g_Bones[150]','g_Bones[300]'),
                       SOURCE.replace('float4 p=input.position;','float g_Timer=1;float4 p=input.position;'),
                       SOURCE.replace('g_Bones[(int)input.bone]','g_Bones'),SOURCE+'\n/*'):
            with self.subTest(source=source[-40:]),self.assertRaises(ValueError):previous.prepare(source,'Scene')

    def test_complete_constant_texture_and_precision_guard(self):
        floats=[[float(i),1,2,3]for i in range(256)];integers=[[i,0,1,-1]for i in range(16)];booleans=[i%2==0 for i in range(16)]
        packed=previous.constant_texture(floats,integers,booleans)
        self.assertEqual(len(packed),288*16)
        self.assertEqual(struct.unpack_from('<4f',packed,80*16),(80,1,2,3))
        self.assertEqual(struct.unpack_from('<4f',packed,260*16),(4,0,1,-1))
        self.assertEqual(struct.unpack_from('<4f',packed,272*16),(1,0,0,0))
        integers[0][0]=16777217
        with self.assertRaises(ValueError):previous.constant_texture(floats,integers,booleans)

    @unittest.skipUnless(os.name=='nt','Windows CPU shader compiler')
    def test_cpu_shader_compiler_and_explicit_include_provider(self):
        requested=[]
        def include(name):
            requested.append(name)
            if name!='authored/scene.inc':raise ValueError('Unexpected include.')
            return SOURCE.encode()
        expanded=previous.preprocess(b'#include "scene.inc"\n','authored/root.hlsl',include)
        generated,_=previous.prepare(expanded,'Scene');code=previous.compile_previous(generated)
        self.assertEqual(requested,['authored/scene.inc']);self.assertEqual(code[:4],b'\0\x03\xfe\xff')
        with self.assertRaises(ValueError):previous.preprocess(b'#include "../secret.inc"\n','authored/root.hlsl',include)
        unsafe='float g_Unbound;\n'+SOURCE.replace('p.xyz+=','p.x+=g_Unbound;p.xyz+=')
        generated,_=previous.prepare(unsafe,'Scene')
        with self.assertRaisesRegex(ValueError,'still reads external'):previous.compile_previous(generated)


if __name__=='__main__':unittest.main()
