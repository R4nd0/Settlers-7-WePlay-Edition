"""Authored leaf backlighting extension on a compiler-declared VS interpolant.

Original S7 source inspection identifies TEXCOORD7.y as the leaf angular
backlighting factor. This tool does no file IO or runtime activation. A caller
must bind it only to the exact original LeafCard VS/PS pair observed in-game.
"""
from __future__ import annotations

import math
import struct

from .bytecode import decode,register_type
from .semantics import reflect


def extend_leaf_backlight(program: bytes,*,factor=1.25) -> tuple[bytes,dict]:
    if isinstance(factor,bool) or not isinstance(factor,(float,int)) or not math.isfinite(factor) or not 1<=factor<=1.5:
        raise ValueError("Backlight factor must be finite and in [1,1.5]")
    factor=struct.unpack("<f",struct.pack("<f",factor))[0]
    instructions=decode(program,stage="vertex")
    bindings=reflect(program)
    outputs=[]
    for item in instructions:
        if item.opcode==31 and len(item.words)==3:
            usage,destination=item.words[1:]
            if (usage&31)==5 and ((usage>>16)&15)==7 and register_type(destination)==6:
                outputs.append(destination&0x7ff)
    if len(outputs)!=1:
        raise ValueError("No unique compiler-declared TEXCOORD7 output")
    target=outputs[0];occupied=set();ranges=[];temps=set();writers=[]
    for row in bindings:
        if row["register_set"]!="float4":continue
        start,count=row["register_index"],row["register_count"]
        if not isinstance(start,int) or not isinstance(count,int) or not 0<=start<256 or not 1<=count<=256-start:
            raise ValueError("Invalid declared VS float-constant range")
        occupied.update(range(start,start+count))
        kind=row.get("class");elements=max(1,row.get("elements",0))
        width=1 if kind in (0,1) else row.get("rows",0) if kind==2 else row.get("columns",0) if kind==3 else 0
        # Relative allocation relies on the original compiler-declared array/
        # matrix contract. Opaque structs, mismatched footprints and aliases
        # without a complete bounded declaration are never treated as proof.
        if row.get("type")==3 and not row.get("struct_members") and width and count==width*elements:
            ranges.append((start,start+count))
    forbidden={25,26,27,28,29,30,38,39,44,45,96}
    matrix_rows={20:4,21:3,22:4,23:3,24:2}
    for item in instructions:
        if item.opcode==0xfffe:continue
        if item.opcode in forbidden or item.words[0]&0x10000000:
            raise ValueError("Subroutine, loop or predicated VS is unsupported")
        raw=list(item.words[1:2] if item.opcode in (47,48,81) else item.words[2:] if item.opcode==31 else item.words[1:])
        operands=[];index=0
        while index<len(raw):
            token=raw[index]
            if not token&0x80000000:raise ValueError("Unsupported VS operand")
            kind,number=register_type(token),token&0x7ff
            relative=bool(token&0x2000)
            if relative:
                if kind!=2 or not any(start<=number<end for start,end in ranges):
                    raise ValueError("Relative constant has no declared CTAB array range")
                index+=1
                if index>=len(raw) or register_type(raw[index]) not in (3,15):
                    raise ValueError("Unsupported relative address operand")
            if kind==2:occupied.add(number)
            elif kind==0:temps.add(number)
            elif kind in (11,12,13):raise ValueError("Extended VS constant bank is unsupported")
            operands.append((kind,number,relative));index+=1
        if item.opcode in matrix_rows and len(operands)>=3:
            kind,number,relative=operands[2];count=matrix_rows[item.opcode]
            if kind==2:
                if number+count>256:raise ValueError("Matrix constant range exceeds VS bank")
                occupied.update(range(number,number+count))
            elif kind==0:
                if relative or number+count>32:raise ValueError("Unsupported temporary matrix range")
                temps.update(range(number,number+count))
        if item.opcode not in (31,47,48,81) and len(item.words)>1:
            destination=item.words[1]
            if register_type(destination)==6 and destination&0x7ff==target and destination&0x00020000:
                if destination&0x2000 or item.opcode not in set(range(1,25))|{32,33,34,35,36,37,46,78,79,80,88,89,90,91,92,93,94,95}:
                    raise ValueError("Unsupported TEXCOORD7.y writer")
                writers.append(item)
    if not writers:raise ValueError("No TEXCOORD7.y writer")
    constant=next((index for index in range(255,-1,-1) if index not in occupied),None)
    temporary=next((index for index in range(32) if index not in temps),None)
    if constant is None or temporary is None:raise ValueError("No unused VS constant/temporary")
    executable=[item for item in instructions if item.opcode!=0xfffe]
    if len(executable)+len(writers)*2+1>512:raise ValueError("VS extension exceeds minimum SM3 instruction budget")
    first=next(item.offset for item in instructions if item.opcode not in (31,47,48,81,0xfffe))
    literal=struct.pack("<2I4f",0x05000051,0xa00f0000|constant,float(factor),1,1,1)
    edits=[(first,first,literal)]
    for item in writers:
        destination=item.words[1];words=list(item.words);words[1]=(destination&~0x70001fff)|temporary
        changed=struct.pack("<"+"I"*len(words),*words)
        remaining_mask=(destination&0x000f0000)&~0x00020000
        if remaining_mask:
            changed+=struct.pack("<3I",0x02000001,0xe0000000|remaining_mask|target,0x80e40000|temporary)
        changed+=struct.pack("<4I",0x03000005,0xe0120000|target,0x80e40000|temporary,0xa0000000|constant)
        edits.append((item.offset,item.offset+len(item.words)*4,changed))
    changed=bytearray();position=0
    for start,end,replacement in sorted(edits):changed.extend(program[position:start]);changed.extend(replacement);position=end
    changed.extend(program[position:]);result=bytes(changed);decode(result,stage="vertex")
    return result,{"effect":"leaf_angular_backlighting","factor":float(factor),"semantic":"TEXCOORD7.y",
                   "saturated_to_unit_interval":True,"local_constant":constant,"local_temporary":temporary,
                   "declared_float_registers_reserved":len(occupied),"position_and_wind_programs_preserved":True,
                   "relative_constant_ranges_verified_from_ctab":True,
                   "relative_index_contract":"original declared HLSL array bounds; address calculations and inputs unchanged",
                   "other_interpolant_components_preserved":True,"runtime_verified":False,"replacement_ready":False,
                   "requires_exact_observed_leaf_vs_ps_pair":True}
