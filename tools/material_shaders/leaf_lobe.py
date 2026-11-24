"""Offline candidate: broaden the proven leaf angular lobe from dot^4 to dot^2.

Development only; neither staging nor the native replacement pins enable this
recipe. The public entry point accepts only the recipient's exact original
resource and original VS/PS pair. No shader source or program is bundled here.
"""
from __future__ import annotations

import struct

from . import bytecode,recipes,shaderlib


def _transform(program: bytes) -> tuple[bytes,dict]:
    instructions=bytecode.decode(program,stage="vertex")
    outputs=[row.words[2]&0x7ff for row in instructions
             if row.opcode==31 and len(row.words)==3
             and row.words[1]&31==5 and (row.words[1]>>16)&15==7
             and bytecode.register_type(row.words[2])==6]
    if len(outputs)!=1:raise ValueError("No unique TEXCOORD7 output")
    target=outputs[0]
    writers=[row for row in instructions if row.opcode not in (31,47,48,81,0xfffe)
             and len(row.words)>1 and bytecode.register_type(row.words[1])==6
             and row.words[1]&0x7ff==target and row.words[1]&0x00020000]
    if len(writers)!=1:raise ValueError("No unique TEXCOORD7.y writer")
    writer=writers[0]
    if writer.opcode!=5 or len(writer.words)!=4 or writer.words[0]!=0x03000005:
        raise ValueError("Final backlight writer is not an ordinary MUL")
    destination,left,right=writer.words[1:]
    if destination&0x0fff0000!=0x00020000 or destination&0x2000:
        raise ValueError("Backlight destination is not an isolated ordinary y component")
    if left!=right or bytecode.register_type(left)!=0 or left&0x0f002000:
        raise ValueError("Backlight writer is not a direct unmodified temporary self product")
    # At this exact original identity the source is the already-squared
    # saturated view/sun dot. Dropping only the final square broadens the lobe;
    # it introduces no constant, temp, texture read or RGB gain.
    replacement=struct.pack("<3I",0x02000001,destination|0x00100000,left)
    result=program[:writer.offset]+replacement+program[writer.offset+16:]
    bytecode.decode(result,stage="vertex")
    return result,{"effect":"leaf_angular_lobe_broadening","semantic":"TEXCOORD7.y",
                   "original_exponent":4,"candidate_exponent":2,
                   "saturated_to_unit_interval":True,"added_constants":0,"added_temporaries":0,
                   "added_texture_reads":0,"instruction_count_change":0,"program_byte_change":-4,
                   "position_wind_and_other_interpolants_preserved":True,
                   "source_semantics_proven_by_exact_original_identity":True,
                   "runtime_verified":False,"replacement_ready":False}


def generate(resource_name: str,original_resource: bytes) -> recipes.Generated:
    expected=recipes.RESOURCES.get(resource_name)
    if expected is None or shaderlib.digest(original_resource)!=expected:
        raise ValueError("Unsupported original leaf shader resource identity")
    blocks={block.stage:block for block in shaderlib.parse(original_resource).blocks
            if block.shader=="LeafCardShader"}
    if set(blocks)!={"vertex","pixel"} or shaderlib.digest(blocks["vertex"].code)!=recipes.ORIGINAL_VERTEX or shaderlib.digest(blocks["pixel"].code)!=recipes.ORIGINAL_PIXEL:
        raise ValueError("Original LeafCard VS/PS pair identity mismatch")
    program,details=_transform(blocks["vertex"].code)
    return recipes.Generated(program,{"format":"s7-recipient-material-program-v1",
        "recipe":"leaf-angular-lobe-v2-candidate","maintainer":"WePlay Community",
        "source_resource":resource_name,"source_resource_sha256":expected,
        "original_vertex_sha256":recipes.ORIGINAL_VERTEX,"original_pixel_sha256":recipes.ORIGINAL_PIXEL,
        "generated_vertex_sha256":shaderlib.digest(program),"generated_bytes":len(program),
        "generated_from_recipient_owned_resource":True,"public_binary_distribution_permitted":False,
        "classify_original_pair_before_temporary_bind":True,"restore_original_shader_after_draw":True,
        "active":False,"production_staging_supported":False,**details})
