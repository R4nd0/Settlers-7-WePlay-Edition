"""Authored shader-envelope fixtures only; no original source/bytecode."""
import json
import struct
import unittest

from tools.material_shaders import shaderlib
from tools.shadow_detail import pass_inventory as p

HEADER=b'FILEVERSION = 2.3.0.0;\nPLATFORM = DX9;\n'


def program(stage='vs',major=2):
    version=((0xfffe if stage=='vs' else 0xffff)<<16)|(major<<8)
    # Authored MOV r0,c0 plus END: enough for the existing checked envelope
    # recognizer. These fixtures are never submitted to a graphics device.
    return struct.pack('<5I',version,0x02000001,0x800f0000,0xa0e40000,0x0000ffff)


def declaration(stage='vs',major=2,body=None,profile=None):
    body=program(stage,major) if body is None else body
    encoded=struct.pack('<I',len(body))+body
    return f'{stage}_bytecode[{profile or stage+"_"+str(major)+"_0"}]={{'.encode()+encoded.hex().encode()+b'}\n'


def record(name='Pass',body=None):
    if body is None:body=declaration()+declaration('ps')
    return b'SHADER '+name.encode()+b' {\n'+body+b'}\n'


class PassInventoryTests(unittest.TestCase):
    def test_per_record_fences_repeated_tags_and_comments_mask_fake_programs(self):
        fake=record('NotARecord')
        data=HEADER+b'// SOURCECODE=$Unterminated{\n'+record(body=b'vs_sourcecode=$TAG{\n'+fake+b'}$TAG\nps_sourcecode=$TAG{\n'+fake+b'}$TAG\n'+declaration()+declaration('ps'))
        result=p.inspect_library(data)
        self.assertEqual([r['shader_record'] for r in result['records']],['Pass'])
        self.assertEqual(len(result['programs']),2)
        self.assertEqual({r['model'] for r in result['programs']},{'2.0'})

    def test_global_source_fence_agrees_with_existing_parser(self):
        data=HEADER+b'SOURCECODE=$GLOBAL{\nfloat4 helper(){return 0;}\n}$GLOBAL\n'+record(body=declaration(major=3)+declaration('ps',3))
        old=shaderlib.parse(data);new=p.inspect_library(data)
        self.assertEqual({(b.stage,b.model,shaderlib.digest(b.code)) for b in old.blocks},
                         {(b['stage'],b['model'],b['compiled_sha256']) for b in new['programs']})

    def test_non_dx9_envelopes_are_explicitly_reported_not_promoted(self):
        data=HEADER+record(body=declaration()+declaration('ps')+declaration(major=3,body=b'CONSOLE!')+declaration(body=b'DXBCxxxx',profile='vs_4_0'))
        result=p.inspect_library(data)
        self.assertEqual(len(result['programs']),2)
        self.assertEqual(len(result['ignored_envelopes']),2)
        self.assertTrue(all('non-DX9' in x['reason'] for x in result['ignored_envelopes']))

    def test_declared_render_states_are_metadata_not_runtime_claims(self):
        data=HEADER+record(body=b'blendenable = 255;\nsrcblend = zero;\ndstblend = one_minus_src_color;\ndepthwriteenabled = false;\ndepthcomparisonfunc = equal;\n'+declaration()+declaration('ps'))
        row=p.inspect_library(data)['records'][0]
        self.assertEqual(row['declared_states']['depthwriteenabled'],['false'])
        self.assertFalse(row['runtime_state_verified'])

    def test_broken_fence_comment_string_or_record_fails_closed(self):
        for body in (b'SOURCECODE=$BAD{',b'/* bad',b'"bad',b'SHADER Bad {\n'+declaration()):
            with self.subTest(body=body[:20]),self.assertRaises(ValueError):p.inspect_library(HEADER+body)

    def test_wrong_size_stage_and_malformed_dx9_token_envelope_rejected(self):
        malformed=struct.pack('<2I',0xfffe0200,0)  # No END.
        for body in (b'vs_bytecode[vs_2_0]={000000000002feffffff0000}\n',
                     declaration(profile='ps_2_0'),declaration(body=program('ps')),declaration(body=malformed)):
            with self.subTest(body=body[:30]),self.assertRaises(ValueError):p.inspect_library(HEADER+record(body=body))

    def test_duplicate_records_and_duplicate_dx9_stage_profile_rejected(self):
        for data in (HEADER+record()+record(),HEADER+record(body=declaration()+declaration())):
            with self.assertRaises(ValueError):p.inspect_library(data)

    def test_only_other_platform_programs_cannot_supply_dx9_bindings(self):
        with self.assertRaisesRegex(ValueError,'No verified D3D9'):
            p.inspect_library(HEADER+record(body=declaration(body=b'DXBCxxxx',profile='vs_4_0')))

    def test_catalog_is_scalar_only_and_has_aliases_not_unique_pass_claims(self):
        data=HEADER+record()
        binary,metadata=p.build([('Graphics/Shaders/First.ShaderLib',data),('Graphics/Shaders/Second.ShaderLib',data)])
        self.assertEqual(len(binary),16+4*48)
        self.assertEqual(metadata['entry_count'],4)
        self.assertEqual(len(metadata['alias_groups']),2)
        self.assertFalse(metadata['contains_program_bytes'])
        self.assertFalse(metadata['runtime_binding_verified'])
        self.assertFalse(metadata['streak_origin_established'])
        json.dumps(metadata,allow_nan=False)

    def test_resource_duplicates_and_catalog_overflow_rejected(self):
        data=HEADER+record()
        with self.assertRaises(ValueError):p.build([('Graphics/Shaders/A.ShaderLib',data)]*2)
        large=HEADER+b''.join(record('P'+str(i)) for i in range(32))
        with self.assertRaisesRegex(ValueError,'512'):
            p.build([(f'Graphics/Shaders/P{i}.ShaderLib',large) for i in range(9)])

    def test_header_bounds_and_unbalanced_source_tricks_reject(self):
        for data in (b'',HEADER.replace(b'DX9',b'DX11')+record(),HEADER+record(body=b'vs_bytecode[vs_2_0]={ZZ}\n')):
            with self.assertRaises(ValueError):p.inspect_library(data)
        with self.assertRaisesRegex(ValueError,'record count'):
            p.inspect_library(HEADER+b''.join(record('P'+str(i)) for i in range(p.MAX_RECORDS+1)))


if __name__=='__main__':unittest.main()
