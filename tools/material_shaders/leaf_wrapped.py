"""One fixed leaf-specific wrapped-direct-diffuse candidate, recipient only.

The original diffuse DP3 must become unsaturated BEFORE the new saturated MAD.
Specular, ambient, alpha, texture inputs and the complete original VS survive.
"""
import math,struct
from . import bytecode,recipes,semantics,shaderlib
RECIPE='leaf-wrapped-diffuse-v4'
def wrapped(dot):
    if isinstance(dot,bool)or not isinstance(dot,(int,float))or not math.isfinite(dot):raise ValueError('Finite light dot required')
    return min(1,max(0,(float(dot)+.5)/1.5))
def _transform(pixel):
    rows=bytecode.decode(pixel);decl=[r.words[2]&2047 for r in rows if r.opcode==31 and len(r.words)==3
        and r.words[1]&31==5 and(r.words[1]>>16)&15==5 and bytecode.register_type(r.words[2])==1]
    if len(decl)!=1:raise ValueError('Unique typed diffuse TSLight input required')
    light=decl[0]
    dots=[r for r in rows if r.opcode==8 and len(r.words)==4 and bytecode.register_type(r.words[1])==0
        and any(bytecode.register_type(t)==1 and t&2047==light for t in r.words[2:])]
    if len(dots)!=1:raise ValueError('Unique diffuse dot required')
    dot=dots[0];dest,normal,sun=dot.words[1:]
    if dot.words[0]!=0x03000008 or dest&0x00ff0000!=0x00110000 or dest&0x2000:
        raise ValueError('Diffuse dot destination must be ordinary saturated scalar x')
    if bytecode.register_type(normal)!=0 or(normal>>16)&255!=0xe4 or normal&0x0f002000:
        raise ValueError('Diffuse normal source must be ordinary temporary')
    if bytecode.register_type(sun)!=1 or sun&2047!=light or(sun>>16)&255!=0xe4 or sun&0x0f002000:
        raise ValueError('Diffuse light source must be direct typed input')
    index=rows.index(dot);following=rows[index+1:index+3]
    scalar=0x80000000|(dest&2047)
    if (len(following)!=2 or following[0].words!=(0x03000005,dest&~0x00100000,scalar,0xa000004c)
        or following[1].words!=(0x03000005,(dest&~0x00100000)|0x00070000,scalar,0xa0e40006)):
        raise ValueError('Exact scalar diffuse-scale then sun-RGB chain required')
    occupied=set()
    for binding in semantics.reflect(pixel):
        if binding['register_set']!='float4':continue
        start,count=binding['register_index'],binding['register_count']
        if not 0<=start<224 or not 1<=count<=224-start:raise ValueError('Invalid PS constant binding')
        occupied.update(range(start,start+count))
    for row in rows:
        if row.opcode==0xfffe:continue
        if row.opcode in(20,21,22,23,24,25,26,27,28,29,30,38,39,44,45,96)or row.words[0]&0x10000000:
            raise ValueError('Indirect/control/matrix pixel program unsupported')
        tokens=row.words[1:2]if row.opcode in(47,48,81)else row.words[2:]if row.opcode==31 else row.words[1:]
        for token in tokens:
            if not token&0x80000000 or token&0x2000:raise ValueError('Unsupported/relative pixel operand')
            kind=bytecode.register_type(token)
            if kind==2:occupied.add(token&2047)
            if kind in(11,12,13):raise ValueError('Extended constant bank unsupported')
    free=next((i for i in range(223,-1,-1)if i not in occupied),None)
    if free is None:raise ValueError('No proven-unused local constant')
    first=next(r.offset for r in rows if r.opcode not in(0xfffe,31,47,48,81))
    definition=struct.pack('<2I4f',0x05000051,0xa00f0000|free,2/3,1/3,0,0)
    unsaturated=struct.pack('<4I',dot.words[0],dest&~0x00100000,normal,sun)
    mad=struct.pack('<5I',0x04000004,dest,scalar,0xa0000000|free,0xa0550000|free)
    edits=[(first,first,definition),(dot.offset,dot.offset+16,unsaturated+mad)]
    result=bytearray();position=0
    for begin,end,changed in sorted(edits):
        if begin<position:raise ValueError('Overlapping transformation edits')
        result.extend(pixel[position:begin]);result.extend(changed);position=end
    result.extend(pixel[position:]);changed=bytes(result);bytecode.decode(changed)
    return changed,{'recipe':RECIPE,'wrap':.5,'old_curve':'saturate(dot)','new_curve':'saturate((dot+.5)/1.5)',
        'local_constant':free,'definition_insert_offset':first,'diffuse_dot_offset':dot.offset,
        'diffuse_temporary':dest&2047,'light_input_register':light,'normal_temporary':normal&2047,
        'old_dot_saturation_removed':True,'new_MAD_saturated':True,'added_pixel_instructions':1,'added_local_DEF':1,
        'added_texture_fetches':0,'program_byte_change':44,'original_VS_required':True,'no_Hemi_stack':True,
        'ambient_specular_alpha_shadow_fog_material_programs_preserved':True,'actual_normal_histogram_measured':False,
        'visible_gain_qualified':False,'runtime_qualified':False}
def generate(name,resource):
    if name not in recipes.RESOURCES or shaderlib.digest(resource)!=recipes.RESOURCES[name]:raise ValueError('Exact original recipient resource required')
    pair={b.stage:b for b in shaderlib.parse(resource).blocks if b.shader=='LeafCardShader'}
    if set(pair)!={'vertex','pixel'}or shaderlib.digest(pair['vertex'].code)!=recipes.ORIGINAL_VERTEX or shaderlib.digest(pair['pixel'].code)!=recipes.ORIGINAL_PIXEL:
        raise ValueError('Exact original LeafCard pair required')
    program,details=_transform(pair['pixel'].code)
    return recipes.Generated(program,{**details,'format':'s7-recipient-material-program-v1','maintainer':'WePlay Community',
        'recipe_version':4,'material_config_version':3,'source_resource':name,'source_resource_sha256':recipes.RESOURCES[name],
        'original_vertex_sha256':recipes.ORIGINAL_VERTEX,'original_pixel_sha256':recipes.ORIGINAL_PIXEL,
        'generated_pixel_sha256':shaderlib.digest(program),'generated_bytes':len(program),
        'generated_from_recipient_owned_resource':True,'public_binary_distribution_permitted':False,
        'classify_original_pair_before_temporary_bind':True,'restore_original_shader_after_draw':True,
        'active':False,'production_staging_supported':False})
