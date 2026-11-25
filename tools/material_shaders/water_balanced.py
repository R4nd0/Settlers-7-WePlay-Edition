"""Fixed balanced River variant B, generated from recipient-owned resources.

A remains immutable. B changes only its shader-local Fresnel base/range:
0.02/0.85 becomes0.05/0.78. Depth transfer, broad energy-limited specular,
texture/flow/atmosphere/output operations and scope/restoration requirements are
identical to A. This is not global colour grading or an active runtime switch.
"""
from __future__ import annotations

import math
import struct

from . import bytecode, recipes, shaderlib, water

RECIPE_ID='river-depth-fresnel-v2-balanced'
BASE_A_SHA256='8a27791a9907f73d4f42c847bc74a0a25b57da48b81f8e9cb566eca59bfcf0d1'
PROGRAM_SHA256='a8a591b346911622bfc9899fa28cbb022a42152df41ed822ef3cfff7c254e2f4'
F0=.05
GRAZING_CAP=.78


def _balance(program, optical_register):
    if type(optical_register) is not int or not 0<=optical_register<224:
        raise ValueError('Unsupported balanced-water optical register')
    definitions=[item for item in bytecode.decode(program) if item.opcode==81
                 and len(item.words)==6 and item.words[1]==0xa00f0000|optical_register]
    if len(definitions)!=1:
        raise ValueError('No unique variant-A optical definition')
    definition=definitions[0]
    expected=struct.pack('<4f',water.F0,water.GRAZING_CAP-water.F0,3,2)
    at=definition.offset+8
    if program[at:at+16]!=expected:
        raise ValueError('Variant-A optical definition differs from its pinned policy')
    result=program[:at]+struct.pack('<2f',F0,GRAZING_CAP-F0)+program[at+8:]
    bytecode.decode(result)
    if len(result)!=len(program) or result[:at]!=program[:at] or result[at+8:]!=program[at+8:]:
        raise ValueError('Balanced water changed non-optical bytecode')
    return result,dict(variant=2,variant_name='balanced',normal_incidence_reflectance=F0,
                      grazing_cap=GRAZING_CAP,optical_constant=optical_register,
                      modified_float_components=2,depth_shape_preserved_from_A=True,
                      anti_glitter_specular_preserved_from_A=True,global_colour_grade=False)


def response(cosine, optical_depth_tint, reflectiveness=1.0):
    values=(cosine,optical_depth_tint,reflectiveness)
    if any(type(v) not in (float,int) or not math.isfinite(v) for v in values):
        raise ValueError('Water response requires finite scalar inputs')
    cosine=max(0,min(1,cosine));tint=max(0,min(1,optical_depth_tint))
    return dict(reflection=max(0,min(1,(F0+(GRAZING_CAP-F0)*(1-cosine)**5)*reflectiveness)),
                optical_depth_tint=tint*tint*(3-2*tint))


def generate(resource_name, original_resource):
    base=water.generate(resource_name,original_resource)
    if len(base.program)!=3840 or shaderlib.digest(base.program)!=BASE_A_SHA256:
        raise ValueError('Balanced water requires the exact immutable generated variant A')
    program,details=_balance(base.program,base.manifest['local_constants'][0])
    if shaderlib.digest(program)!=PROGRAM_SHA256:
        raise ValueError('Balanced water generated identity changed')
    metadata=dict(base.manifest)
    metadata.update(recipe=RECIPE_ID,base_variant_A_sha256=BASE_A_SHA256,
                    generated_pixel_sha256=shaderlib.digest(program),generated_bytes=len(program),
                    active=False,runtime_verified=False,replacement_ready=False,
                    visible_quality_qualified=False,**details)
    return recipes.Generated(program,metadata)
