"""Conservative authored D3D9 SM3 pixel-output extension; no game IO.

An eligible original writes COLOR0 RGB through ordinary unpredicated arithmetic
instructions. A private temporary receives the original output calculation;
an authored local constant/RGB MUL then writes COLOR0. Alpha, other MRT outputs
and original material texture/constant inputs survive.
"""
from __future__ import annotations

from dataclasses import dataclass
import math
import struct

from .inventory import KNOWN_OPCODES, MAX_PAYLOAD


@dataclass(frozen=True)
class Instruction:
    offset: int
    opcode: int
    words: tuple[int,...]


def decode(program: bytes,*,stage="pixel") -> tuple[Instruction,...]:
    if not isinstance(program,bytes) or not 12<=len(program)<=MAX_PAYLOAD or len(program)%4:
        raise ValueError("Unsupported compiled program size")
    words=struct.unpack("<"+"I"*(len(program)//4),program)
    if stage not in ("pixel","vertex") or words[0]!=(0xffff0300 if stage=="pixel" else 0xfffe0300):
        raise ValueError("Only the selected Shader Model 3 stage is supported")
    result=[];index=1
    while index<len(words):
        first=words[index];opcode=first&0xffff
        if first==0xffff:
            if index!=len(words)-1:
                raise ValueError("Trailing program bytes")
            return tuple(result)
        if first&0x80000000:
            raise ValueError("Invalid instruction token")
        count=(first>>16)&0x7fff if opcode==0xfffe else (first>>24)&0xf
        if opcode!=0xfffe and opcode not in KNOWN_OPCODES:
            raise ValueError("Unknown instruction")
        if index+count>=len(words):
            raise ValueError("Truncated instruction")
        result.append(Instruction(index*4,opcode,tuple(words[index:index+count+1])))
        index+=count+1
    raise ValueError("Missing program END")


def register_type(token: int) -> int:
    return ((token>>28)&7)|((token>>8)&0x18)


def extend_rgb_gain(program: bytes,gain: tuple[float,float,float]) -> tuple[bytes,dict]:
    if len(gain)!=3 or any(isinstance(x,bool) or not isinstance(x,(int,float))
                           or not math.isfinite(x) or not .5<=x<=1.5 for x in gain):
        raise ValueError("RGB gain must contain three finite values in [0.5,1.5]")
    instructions=decode(program)
    executable=[item for item in instructions if item.opcode!=0xfffe]
    if not executable:
        raise ValueError("Empty pixel program")
    outputs=[]
    used=set();temps=set()
    forbidden={20,21,22,23,24,25,26,27,28,29,30,38,39,44,45,96}
    for item in executable:
        if item.opcode in forbidden or item.words[0]&0x10000000:
            raise ValueError("Indirect, matrix, loop or subroutine program is unsupported")
        operands=item.words[1:2] if item.opcode in (47,48,81) else item.words[2:] if item.opcode==31 else item.words[1:]
        for token in operands:
            if not token&0x80000000:
                raise ValueError("Unrecognized shader operand")
            if token&0x2000:
                raise ValueError("Relative addressing is unsupported")
            kind=register_type(token);number=token&0x7ff
            if kind in (11,12,13):
                raise ValueError("Extended constant banks are unsupported")
            if kind==2:
                used.add(number)
            if kind==0:
                temps.add(number)
        # Only destination operands are writes. Other render-target/depth outputs
        # and separate alpha writes are retained exactly as authored originally.
        if item.opcode not in (31,47,48,81) and len(item.words)>1:
            destination=item.words[1]
            if register_type(destination)==8 and not destination&0x7ff and (destination>>16)&7:
                # Control-flow opcodes and declarations cannot own a COLOR
                # destination. Texture/ALU destination writes are kept intact,
                # including all source swizzles/modifiers and original flags.
                if item.opcode not in set(range(1,20))|{32,33,34,35,36,37,66,78,79,80,88,89,90,91,92,93,94,95}:
                    raise ValueError("Unsupported COLOR0 RGB writer")
                outputs.append(item)
    if not outputs:
        raise ValueError("No ordinary COLOR0 RGB writer")
    free=next((index for index in range(223,-1,-1) if index not in used),None)
    if free is None:
        raise ValueError("No unused local float constant")
    temporary=next((index for index in range(32) if index not in temps),None)
    if temporary is None:
        raise ValueError("No unused pixel temporary")
    if len(executable)+len(outputs)*2+1>512:
        raise ValueError("Extension exceeds the minimum SM3 instruction budget")
    # DEF has one destination plus four literal float operands (six DWORDs).
    literal=struct.pack("<2I4f",0x05000051,0xa00f0000|free,*map(float,gain),1.0)
    constant_source=0xa0e40000|free
    # Keep comments and all original declarations in order, insert the authored
    # DEF just before the first executable instruction after the declaration run.
    preamble={31,47,48,81,0xfffe}
    first=next(item.offset for item in instructions if item.opcode not in preamble)
    edits=[(first,first,literal)]
    for item in outputs:
        destination=item.words[1]
        # Original destination modifiers apply exactly once to the temporary.
        # The authored output write uses only its original RGB component mask.
        changed_words=list(item.words)
        changed_words[1]=(destination&~0x70001fff)|temporary
        output=struct.pack("<"+"I"*len(changed_words),*changed_words)
        rgb_destination=0x80000800|(destination&0x00070000)
        source=0x80e40000|temporary
        output+=struct.pack("<4I",0x03000005,rgb_destination,source,constant_source)
        if destination&0x00080000:
            alpha_destination=0x80080800
            output+=struct.pack("<3I",0x02000001,alpha_destination,source)
        edits.append((item.offset,item.offset+len(item.words)*4,output))
    changed=bytearray();position=0
    for start,end,replacement in sorted(edits):
        changed.extend(program[position:start]);changed.extend(replacement);position=end
    changed.extend(program[position:]);changed=bytes(changed)
    decode(changed)
    return changed,{"effect":"material_rgb_gain","gain":list(map(float,gain)),"local_constant":free,
                    "local_temporary":temporary,
                    "alpha_preserved":True,"original_material_inputs_preserved":True,
                    "color0_rgb_writes_extended":len(outputs),"other_mrt_outputs_preserved":True,
                    "runtime_verified":False,"replacement_ready":False}
