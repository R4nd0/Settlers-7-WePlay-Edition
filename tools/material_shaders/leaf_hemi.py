"""Recipient-authored candidate: replace the existing hemisphere response.

Exact original program pair only. Pixel TEXCOORD1.z has one ambient-only
consumer. Original normals used by sun/specular remain in their original
temporaries; only the outgoing z lighting carrier receives2*z-1.
"""
import math,struct
from . import bytecode,recipes,shaderlib

RECIPE='leaf-hemisphere-contrast-v3'
def hemisphere(z):
    if isinstance(z,bool)or not isinstance(z,(int,float))or not math.isfinite(z):raise ValueError('Finite normal z required')
    return .5+.5*min(1,max(0,float(z)))
def original_hemisphere(z):
    if isinstance(z,bool)or not isinstance(z,(int,float))or not math.isfinite(z):raise ValueError('Finite normal z required')
    return .5+.5*min(1,max(0,.5*float(z)+.5))
def _declaration(code,stage):
    rows=bytecode.decode(code,stage=stage)
    found=[r.words[2]for r in rows if r.opcode==31 and len(r.words)==3 and(r.words[1]&31)==5
        and((r.words[1]>>16)&15)==1 and bytecode.register_type(r.words[2])==(6 if stage=='vertex'else 1)]
    if len(found)!=1:raise ValueError('Unique typed TEXCOORD1 declaration required')
    return rows,found[0]&2047
def _pixel_proof(pixel):
    rows,target=_declaration(pixel,'pixel');refs=[]
    for row in rows:
        if row.opcode in(0xfffe,31,47,48,81):continue
        for position,token in enumerate(row.words[1:],1):
            if token&0x80000000 and bytecode.register_type(token)==1 and token&2047==target:refs.append((row,position,token))
    if len(refs)!=1:raise ValueError('Normal carrier has other pixel consumers')
    row,position,token=refs[0]
    if row.opcode!=4 or len(row.words)!=5 or position!=2 or(token>>16)&255!=0xaa or token&0x0f002000:
        raise ValueError('Ambient consumer is not ordinary z-only MAD')
    if row.words[1]&0x00f00000!=0x00100000:raise ValueError('Original hemisphere saturation missing')
    constants=row.words[3:]
    if len(set(constants))!=1 or bytecode.register_type(constants[0])!=2 or constants[0]&2047 or(constants[0]>>16)&255!=0x55:
        raise ValueError('Original hemisphere offset/scale not proven')
    definitions=[r for r in rows if r.opcode==81 and r.words[1]&2047==0]
    if len(definitions)!=1 or definitions[0].words[3]!=0x3f000000:raise ValueError('Original pixel localhalf missing')
    return {'pixel_input_register':target,'consumer_byte_offset':row.offset,'consumer_count':1,'source_swizzle':'zzzz','use':'ambient hemisphere only'}
def transform(vertex,pixel):
    if shaderlib.digest(vertex)!=recipes.ORIGINAL_VERTEX or shaderlib.digest(pixel)!=recipes.ORIGINAL_PIXEL:
        raise ValueError('Exact original LeafCard pair required')
    proof=_pixel_proof(pixel);rows,target=_declaration(vertex,'vertex')
    writers=[r for r in rows if r.opcode not in(0xfffe,31,47,48,81)and len(r.words)>1
        and bytecode.register_type(r.words[1])==6 and r.words[1]&2047==target and r.words[1]&0x40000]
    if len(writers)!=1:raise ValueError('Unique worldnormal z output writer required')
    writer=writers[0]
    if writer.opcode!=1 or writer.words[0]!=0x02000001 or len(writer.words)!=3 or(writer.words[1]>>16)&0xfff!=7:
        raise ValueError('Original worldnormal writer is not ordinary xyz MOV')
    source=writer.words[2]
    if bytecode.register_type(source)!=0 or(source>>16)&255!=0xe4 or source&0x0f002000:
        raise ValueError('Original worldnormal source must be direct identity-swizzle temporary')
    defs=[r for r in rows if r.opcode==81 and r.words[1]&2047==5]
    if len(defs)!=1 or len(defs[0].words)!=6 or defs[0].words[2:4]!=(0x40000000,0xbf800000):
        raise ValueError('Original local2/-1 not available')
    # Keep the original MOV xyz byte-for-byte, then overwrite only outgoing z.
    # Original r4 and every other output/input/constant instruction survive.
    destination=(writer.words[1]&~0xf0000)|0x40000
    normal_z=(source&~0x00ff0000)|0x00aa0000
    authored=struct.pack('<5I',0x04000004,destination,normal_z,0xa0000005,0xa0550005)
    offset=writer.offset+12;changed=vertex[:offset]+authored+vertex[offset:]
    bytecode.decode(changed,stage='vertex')
    return changed,{'recipe':RECIPE,'recipe_version':3,'added_vertex_instructions':1,'added_constants':0,'added_temporaries':0,
        'added_texture_reads':0,'added_pixel_instructions':0,'insert_byte_offset':offset,'original_writer_byte_offset':writer.offset,
        'vertex_output_register':target,'worldnormal_temporary':source&2047,'new_output_z':'2*original_worldnormal.z-1',
        'old_hemisphere':'.5+.5*saturate(.5*z+.5)','new_hemisphere':'.5+.5*saturate(z)',
        'ambient_floor_preserved':.5,'upward_top_preserved':1,'ambient_and_sun_RGB_vectors_preserved':True,
        'sunlight_and_specular_normal_temporaries_preserved':True,'no_uniform_tree_RGB_gain':True,
        'original_vertex_bytes_unchanged_except_insert':True,'original_pixel_program_unchanged':True,
        'position_wind_silhouette_alpha_LOD_programs_preserved':True,'actual_normal_distribution_measured':False,
        'runtime_qualified':False,'visible_gain_qualified':False,'pixel_use_proof':proof}
def generate(name,resource):
    if name not in recipes.RESOURCES or shaderlib.digest(resource)!=recipes.RESOURCES[name]:raise ValueError('Exact recipient original resource required')
    pair={b.stage:b for b in shaderlib.parse(resource).blocks if b.shader=='LeafCardShader'}
    if set(pair)!={'vertex','pixel'}:raise ValueError('Complete original pair required')
    program,metadata=transform(pair['vertex'].code,pair['pixel'].code)
    return recipes.Generated(program,{**metadata,'format':'s7-recipient-material-program-v1','maintainer':'WePlay Community',
        'source_resource':name,'source_resource_sha256':recipes.RESOURCES[name],
        'original_vertex_sha256':recipes.ORIGINAL_VERTEX,'original_pixel_sha256':recipes.ORIGINAL_PIXEL,
        'generated_vertex_sha256':shaderlib.digest(program),'generated_bytes':len(program),
        'generated_from_recipient_owned_resource':True,'public_binary_distribution_permitted':False,
        'classify_original_pair_before_temporary_bind':True,'restore_original_shader_after_draw':True,
        'material_config_version':2,'active':False,'production_staging_supported':False})
