"""Three fixed recipient-generated leaf PS programs; no arbitrary curve input."""
import struct
from . import bytecode,leaf_wrapped,recipes,shaderlib
CURVES=((.8,.2),(2/3,1/3),(4/7,3/7))
NAMES=('A','B','C')
RECIPES=('leaf-palette-A-v5','leaf-palette-B-v6','leaf-palette-C-v7')
def generate(name,resource,variant):
    if type(variant)is not int or variant not in(0,1,2):raise ValueError('Fixed Leaf palette index0..2 required')
    original=leaf_wrapped.generate(name,resource);program=bytearray(original.program);constant=original.manifest['local_constant']
    definitions=[r for r in bytecode.decode(original.program)if r.opcode==81 and r.words[1]&2047==constant]
    if len(definitions)!=1 or definitions[0].words[2:4]!=struct.unpack('<2I',struct.pack('<2f',2/3,1/3)):
        raise ValueError('Authored local wrap coefficients absent')
    at=definitions[0].offset+8;program[at:at+8]=struct.pack('<2f',*CURVES[variant]);program=bytes(program)
    bytecode.decode(program)
    return recipes.Generated(program,{**original.manifest,'recipe':RECIPES[variant],'recipe_version':5+variant,
        'material_config_version':4,'variant_index':variant,'variant_name':NAMES[variant],
        'fixed_coefficients':list(CURVES[variant]),'wrap':(.25,.5,.75)[variant],
        'generated_pixel_sha256':shaderlib.digest(program),'generated_bytes':len(program),
        'palette_program':True,'active':False,'runtime_qualified':False})
