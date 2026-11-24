"""Authored packed-normal detail extension for original terrain PS programs.

The original shader reconstructs normal Z without a protective clamp. Therefore
normal strength must be followed by a radial XY limit, not independent channel
gain. No file IO, game state change or automatic deployment is implemented.
"""
from __future__ import annotations

import math
import struct

from .bytecode import decode,register_type
from .semantics import reflect


def extend_normal_detail(program: bytes,*,strength=1.10) -> tuple[bytes,dict]:
    if isinstance(strength,bool) or not isinstance(strength,(float,int)) or not math.isfinite(strength) or not 1<=strength<=1.25:
        raise ValueError("Normal strength must be finite in [1,1.25]")
    strength=struct.unpack("<f",struct.pack("<f",strength))[0]
    instructions=decode(program)
    bindings=reflect(program)
    normals=[row for row in bindings if row["name"]=="g_DetailNormalMap" and row["register_set"]=="sampler" and row["register_count"]==1]
    if len(normals)!=1 or normals[0]["register_index"]!=5 or normals[0]["class"]!=4 or normals[0]["type"]!=12:
        raise ValueError("No exact original detail-normal sampler binding")
    occupied=set();temps=set();fetches=[]
    for row in bindings:
        if row["register_set"]=="float4":
            start,count=row["register_index"],row["register_count"]
            if not 0<=start<224 or not 0<count<=224-start:raise ValueError("Invalid declared pixel constant range")
            occupied.update(range(start,start+count))
    for item in instructions:
        if item.opcode==0xfffe:continue
        operands=item.words[1:2] if item.opcode in (47,48,81) else item.words[2:] if item.opcode==31 else item.words[1:]
        for token in operands:
            if not token&0x80000000 or token&0x2000:raise ValueError("Unsupported relative/immediate operand")
            kind,number=register_type(token),token&0x7ff
            if kind==2:occupied.add(number)
            elif kind==0:temps.add(number)
            elif kind in (11,12,13):raise ValueError("Extended constant bank is unsupported")
        if item.opcode in (20,21,22,23,24):raise ValueError("Implicit matrix ranges are unsupported in this pixel recipe")
        if item.opcode in (66,95) and len(item.words)==4 and register_type(item.words[3])==10 and item.words[3]&0x7ff==5:
            if item.words[0]&0x10000000 or register_type(item.words[1])!=0:
                raise ValueError("Predicated/non-temporary normal fetch unsupported")
            if ((item.words[3]>>16)&255)!=0xe4 or (item.words[1]&0x000a0000)!=0x000a0000:
                raise ValueError("Normal fetch does not expose original unswizzled AG channels")
            fetches.append(item)
    if not fetches:raise ValueError("No original normal-map texture fetch")
    free_constants=[index for index in range(223,-1,-1) if index not in occupied]
    free_temps=[index for index in range(32) if index not in temps]
    if len(free_constants)<2 or len(free_temps)<2:raise ValueError("No two unused local constants/temporaries")
    count=sum(item.opcode!=0xfffe for item in instructions)
    if count+2+9*len(fetches)>512:raise ValueError("Normal extension exceeds minimum SM3 instruction budget")
    c0,c1=free_constants[:2];normal,scalar=free_temps[:2]
    first=next(item.offset for item in instructions if item.opcode not in (31,47,48,81,0xfffe))
    definitions=struct.pack("<2I4f2I4f",0x05000051,0xa00f0000|c0,strength,.99,.5,.000001,
                            0x05000051,0xa00f0000|c1,2,-1,1,0)
    edits=[(first,first,definitions)]
    for item in fetches:
        texture=item.words[1]&0x7ff
        words=(
            0x04000004,0x80030000|normal,0x80070000|texture,0xa0000000|c1,0xa0550000|c1,
            0x03000005,0x80030000|normal,0x80e40000|normal,0xa0000000|c0,
            0x0400005a,0x80010000|scalar,0x80e40000|normal,0x80e40000|normal,0xa0ff0000|c1,
            0x0300000b,0x80010000|scalar,0x80000000|scalar,0xa0ff0000|c0,
            0x02000007,0x80010000|scalar,0x80000000|scalar,
            0x03000005,0x80010000|scalar,0x80000000|scalar,0xa0550000|c0,
            0x0300000a,0x80010000|scalar,0x80000000|scalar,0xa0aa0000|c1,
            0x03000005,0x80030000|normal,0x80e40000|normal,0x80000000|scalar,
            0x04000004,0x800a0000|texture,0x80040000|normal,0xa0aa0000|c0,0xa0aa0000|c0,
        )
        after=item.offset+len(item.words)*4
        edits.append((after,after,struct.pack("<"+"I"*len(words),*words)))
    result=bytearray();position=0
    for start,end,replacement in sorted(edits):result.extend(program[position:start]);result.extend(replacement);position=end
    result.extend(program[position:]);changed=bytes(result);decode(changed)
    return changed,{"effect":"terrain_packed_normal_detail","strength":strength,"normal_sampler":5,
                    "packed_channels":"AG","radial_xy_limit":.99,"epsilon_protected":True,
                    "local_constants":[c0,c1],"local_temporaries":[normal,scalar],"normal_fetches":len(fetches),
                    "original_other_texture_channels_preserved":True,"original_lighting_and_output_instructions_preserved":True,
                    "runtime_verified":False,"replacement_ready":False}
