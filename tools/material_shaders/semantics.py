"""Bounded D3D9 CTAB reflection metadata; no original source/body export.

The table layout follows Microsoft's D3DXSHADER_CONSTANTTABLE/CONSTANTINFO
structures. Names describe compiler bindings, not observed live device values.
"""
from __future__ import annotations

import re
import struct

from .inventory import token_candidates

HEADER=struct.Struct("<7I")
CONSTANT=struct.Struct("<IHHHHII")
TYPE=struct.Struct("<6HI")
SETS={0:"bool",1:"int4",2:"float4",3:"sampler"}


def reflect(program: bytes) -> list[dict]:
    envelope=token_candidates(program)
    if len(envelope)!=1 or envelope[0]["offset"]!=0 or envelope[0]["bytes"]!=len(program):
        raise ValueError("Unsupported shader envelope")
    cursor=4;tables=[]
    while cursor+4<=len(program):
        token=struct.unpack_from("<I",program,cursor)[0]
        opcode=token&0xffff
        if token==0xffff:break
        count=(token>>16)&0x7fff if opcode==0xfffe else (token>>24)&0xf
        if opcode==0xfffe and program[cursor+4:cursor+8]==b"CTAB":
            tables.append(program[cursor+8:cursor+4+count*4])
        cursor+=(count+1)*4
    if len(tables)>1:raise ValueError("Multiple CTAB tables are unsupported")
    if not tables:return []
    data=tables[0]
    if len(data)<HEADER.size:raise ValueError("Truncated CTAB header")
    size,creator,version,count,offset,flags,target=HEADER.unpack_from(data)
    if size!=HEADER.size or version!=struct.unpack_from("<I",program)[0] or count>512:
        raise ValueError("Unsupported CTAB header")
    if count==0:return []
    if offset<HEADER.size or offset+count*CONSTANT.size>len(data):
        raise ValueError("Invalid CTAB entry range")
    rows=[]
    for index in range(count):
        name,register_set,register_index,register_count,reserved,type_offset,default=CONSTANT.unpack_from(data,offset+index*CONSTANT.size)
        if register_set not in SETS or register_index>4095 or register_count>4096:
            raise ValueError("Unsupported CTAB register metadata")
        if not HEADER.size<=name<len(data) or not HEADER.size<=type_offset<=len(data)-TYPE.size:
            raise ValueError("Invalid CTAB name/type range")
        end=data.find(b"\0",name,min(len(data),name+256))
        if end<0:raise ValueError("Unterminated CTAB name")
        raw_name=data[name:end]
        if not re.fullmatch(rb"[A-Za-z_$][A-Za-z0-9_$.\[\]]{0,254}",raw_name):
            raise ValueError("Unsupported CTAB name")
        kind,value_type,types_rows,columns,elements,members,member_offset=TYPE.unpack_from(data,type_offset)
        if kind>5 or value_type>19 or types_rows>4 or columns>4 or elements>4096 or members>512:
            raise ValueError("Unsupported CTAB type metadata")
        rows.append({"name":raw_name.decode("ascii"),"register_set":SETS[register_set],
                     "register_index":register_index,"register_count":register_count,
                     "class":kind,"type":value_type,"rows":types_rows,"columns":columns,
                     "elements":elements,"struct_members":members,"runtime_values_observed":False})
    return rows
