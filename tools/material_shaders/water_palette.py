"""Fixed recipient-generated Water palette. Accepted B bytes are immutable index0."""
import math,struct
from tools.material_shaders import bytecode,recipes,shaderlib,water_balanced
POLICIES=(
    dict(name='accepted_B',recipe=water_balanced.RECIPE_ID,F0=.05,cap=.78,depth=(3.,2.)),
    dict(name='clear_bank',recipe='river-palette-clear-bank-v3',F0=.035,cap=.70,depth=(2.4,1.4)),
    dict(name='calm_gloss',recipe='river-palette-calm-gloss-v4',F0=.075,cap=.58,depth=(3.,2.)),
)
PINS=(water_balanced.PROGRAM_SHA256,
      '0bf632f2862eb635c25131e575b4c2d5c35ba9fa73cb3aa01591f9afaecd543c',
      '5d027025eb77c23d902c8ccdbc4cddde70064f387e0c34bd4039ee1cf52c6c70')

def _transform(program,optical_register,index):
    if type(index)is not int or index not in(0,1,2):raise ValueError('Fixed palette index0..2 required')
    if type(optical_register)is not int or not 0<=optical_register<224:raise ValueError('Invalid optical register')
    definitions=[i for i in bytecode.decode(program)if i.opcode==81 and len(i.words)==6 and i.words[1]==(0xa00f0000|optical_register)]
    if len(definitions)!=1:raise ValueError('No unique accepted-B optical declaration')
    at=definitions[0].offset+8
    if program[at:at+16]!=struct.pack('<4f',.05,.78-.05,3,2):raise ValueError('Immutable B optical policy required')
    if index==0:return program
    p=POLICIES[index]
    result=program[:at]+struct.pack('<4f',p['F0'],p['cap']-p['F0'],*p['depth'])+program[at+16:]
    bytecode.decode(result)
    if result[:at]!=program[:at]or result[at+16:]!=program[at+16:]:raise ValueError('Changed non-optical instructions')
    return result

def response(index,cosine,depth,reflectiveness=1.):
    if type(index)is not int or index not in(0,1,2):raise ValueError('Fixed palette index0..2 required')
    if any(type(v)not in(int,float)or not math.isfinite(v)for v in(cosine,depth,reflectiveness)):raise ValueError('Finite scalar inputs required')
    p=POLICIES[index];c=max(0,min(1,cosine));d=max(0,min(1,depth));a,b=p['depth']
    return dict(reflection=max(0,min(1,(p['F0']+(p['cap']-p['F0'])*(1-c)**5)*reflectiveness)),optical_depth_tint=max(0,min(1,d*d*(a-b*d))))

def generate(resource_name,original_resource,index):
    if type(index)is not int or index not in(0,1,2):raise ValueError('Fixed palette index0..2 required')
    base=water_balanced.generate(resource_name,original_resource)
    if len(base.program)!=3840 or shaderlib.digest(base.program)!=water_balanced.PROGRAM_SHA256:raise ValueError('Immutable accepted B required')
    program=_transform(base.program,base.manifest['local_constants'][0],index);digest=shaderlib.digest(program)
    if PINS[index]and digest!=PINS[index]:raise ValueError('Fixed palette generated hash changed')
    p=POLICIES[index]
    return recipes.Generated(program,{**base.manifest,'recipe':p['recipe'],'palette_index':index,'palette_name':p['name'],
        'normal_incidence_reflectance':p['F0'],'grazing_cap':p['cap'],'depth_coefficients':list(p['depth']),
        'generated_pixel_sha256':digest,'generated_bytes':len(program),'accepted_B_sha256':water_balanced.PROGRAM_SHA256,
        'accepted_B_bitwise_preserved':index==0,'changed_shader_local_float_components':(0,4,2)[index],
        'original_B_specular_code_preserved':True,'original_B_texture_and_output_code_preserved':True,
        'added_instructions':0,'added_texture_reads':0,'wave_flow_uv_changed':False,
        'active':False,'runtime_verified':False,'visible_quality_qualified':False})
