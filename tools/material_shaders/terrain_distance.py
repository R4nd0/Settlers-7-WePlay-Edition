"""Offline exact-original near-detail distance candidate; no runtime activation.

The original terrain VS programs fade normal detail from 2000 to 3200 world
units; parallax also uses this weight. Only the two local DEF literal components
for this interval change. Far layer visibility (15000..19200), positions, UVs,
fog and lighting inputs remain byte-for-byte original. Texture mip selection is
preserved. This development candidate is excluded from the production stager.
"""
from __future__ import annotations

import math
import struct

from . import bytecode,recipes,shaderlib

LAYOUTS={"BumpLayerShader":((2,3),(3,1)),"ParallaxLayerShader":((2,2),(2,3))}


def _bounds(full: float,zero: float) -> tuple[float,float]:
    if any(isinstance(v,bool) or not isinstance(v,(int,float)) or not math.isfinite(v) for v in (full,zero)):
        raise ValueError("Detail distances must be finite numbers")
    if not 2000<=full<=8000 or not 3200<=zero<=12800 or zero-full<1200:
        raise ValueError("Detail interval exceeds bounded candidate distances")
    return float(full),float(zero)


def weight(distance: float,*,full=8000,zero=12800) -> float:
    full,zero=_bounds(full,zero)
    if isinstance(distance,bool) or not isinstance(distance,(int,float)) or not math.isfinite(distance) or distance<0:
        raise ValueError("Eye distance must be finite and nonnegative")
    return min(1.0,max(0.0,(zero-distance)/(zero-full)))


def _transform(program: bytes,role: str,*,full=8000,zero=12800) -> tuple[bytes,dict]:
    full,zero=_bounds(full,zero)
    if role not in LAYOUTS:raise ValueError("Unsupported original terrain role")
    points=LAYOUTS[role];instructions=bytecode.decode(program,stage="vertex")
    definitions={}
    for row in instructions:
        if row.opcode==81 and len(row.words)==6:
            index=row.words[1]&0x7ff
            if index in definitions:raise ValueError("Ambiguous local constant definition")
            definitions[index]=row
    result=bytearray(program)
    expected=(struct.pack("<f",3200),struct.pack("<f",1/1200))
    replacement=(struct.pack("<f",zero),struct.pack("<f",1/(zero-full)))
    changed_offsets=[]
    for (register,component),old,new in zip(points,expected,replacement):
        row=definitions.get(register)
        if row is None or bytecode.register_type(row.words[1])!=2:
            raise ValueError("Missing original near-detail local constant")
        offset=row.offset+8+component*4
        if program[offset:offset+4]!=old:raise ValueError("Original near-detail literal mismatch")
        result[offset:offset+4]=new;changed_offsets.append(offset)
    changed=bytes(result);bytecode.decode(changed,stage="vertex")
    allowed={position for offset in changed_offsets for position in range(offset,offset+4)}
    if any(a!=b and index not in allowed for index,(a,b) in enumerate(zip(program,changed))):
        raise ValueError("Unexpected modification outside near-detail constants")
    return changed,{"effect":"terrain_near_detail_distance","full_detail_distance":full,
                    "zero_detail_distance":zero,"original_full_distance":2000,"original_zero_distance":3200,
                    "literal_components_changed":2,"program_size_preserved":True,
                    "far_layer_visibility_preserved":True,"position_fog_uv_programs_preserved":True,
                    "normal_weight_changed":True,"parallax_weight_changed":role=="ParallaxLayerShader",
                    "texture_mip_selection_preserved":True,"added_instructions":0,"added_texture_reads":0,
                    "runtime_verified":False,"replacement_ready":False}


def generate(resource_name: str,original_resource: bytes,role: str,*,full=8000,zero=12800) -> recipes.Generated:
    expected=recipes.TERRAIN_RESOURCES.get(resource_name);pair=recipes.TERRAIN_PAIRS.get(role)
    if expected is None or pair is None or shaderlib.digest(original_resource)!=expected:
        raise ValueError("Unsupported original terrain resource identity")
    blocks={b.stage:b for b in shaderlib.parse(original_resource).blocks if b.shader==role}
    if set(blocks)!={"vertex","pixel"} or shaderlib.digest(blocks["vertex"].code)!=pair[0] or shaderlib.digest(blocks["pixel"].code)!=pair[1]:
        raise ValueError("Original terrain VS/PS pair identity mismatch")
    program,details=_transform(blocks["vertex"].code,role,full=full,zero=zero)
    return recipes.Generated(program,{"format":"s7-recipient-material-program-v1",
        "recipe":"terrain-near-distance-"+role+"-candidate","maintainer":"WePlay Community",
        "source_resource":resource_name,"source_resource_sha256":expected,
        "original_vertex_sha256":pair[0],"original_pixel_sha256":pair[1],
        "generated_vertex_sha256":shaderlib.digest(program),"generated_bytes":len(program),
        "generated_from_recipient_owned_resource":True,"public_binary_distribution_permitted":False,
        "classify_original_pair_before_temporary_bind":True,"restore_original_shader_after_draw":True,
        "active":False,"production_staging_supported":False,**details})
