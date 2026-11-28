"""Compose original SM3 geometry with a separately compiled previous position.

Current-frame instructions remain byte-identical. The appended program reads
previous constants through a vertex texture and exposes one new TEXCOORD.
This does not establish temporal draw identity or bind any game resources.
"""
from dataclasses import dataclass
import struct

from tools.material_shaders.bytecode import decode,register_type
from tools.material_shaders.semantics import reflect

COMMENT=0xfffe;DCL=31;DEF=81;DEFI=48;DEFB=47
SETUP={COMMENT,DCL,DEF,DEFI,DEFB}
TEMP=0;INPUT=1;FLOAT=2;OUTPUT=6;INTEGER=7;SAMPLER=10;BOOLEAN=14
PARAMETER=0x80000000;INDEX=0x7ff;RELATIVE=0x2000;MASK=0xf0000
FORBIDDEN={25,26,28,30}  # CALL, CALLNZ, RET, LABEL.


def register(kind,index):return PARAMETER|((kind&7)<<28)|((kind&0x18)<<8)|index
def semantic(token):return token&15,(token>>16)&15
def replaced(token,kind,index):return (token&~(0x70001800|INDEX))|register(kind,index)


@dataclass(frozen=True)
class MotionVertexProgram:
    code:bytes
    previous_output:int
    texcoord:int
    previous_sampler:int
    relocated_float_constants:tuple


def parameters(instruction):
    if instruction.opcode==COMMENT:return ()
    if instruction.opcode in(DEF,DEFI,DEFB):return (1,)
    if instruction.opcode==DCL:return (2,)
    return range(1,len(instruction.words))


def checked(program):
    if not isinstance(program,bytes)or not 16<=len(program)<=65536:raise ValueError('Invalid motion shader size.')
    instructions=decode(program,stage='vertex')
    for ins in instructions:
        if ins.opcode!=COMMENT and(ins.opcode in FORBIDDEN or ins.words[0]&0x50000000):raise ValueError('Subroutines and predicated instructions require a separate motion translation.')
        if ins.opcode==DCL and len(ins.words)!=3:raise ValueError('Invalid vertex declaration.')
        if ins.opcode in(DEF,DEFI,DEFB) and len(ins.words)!=(3 if ins.opcode==DEFB else 6):raise ValueError('Invalid constant definition.')
        for at in parameters(ins):
            token=ins.words[at];kind=register_type(token);index=token&INDEX
            if not token&PARAMETER or kind in(11,12,13):raise ValueError('Unsupported shader register envelope.')
            if kind==OUTPUT and token&RELATIVE:raise ValueError('Relative output writes cannot be extended safely.')
            maximum={TEMP:32,INPUT:16,FLOAT:256,OUTPUT:12,INTEGER:16,SAMPLER:4,BOOLEAN:16}.get(kind)
            if maximum is not None and index>=maximum:raise ValueError('Shader register is out of range.')
    return instructions


def declarations(instructions):
    inputs={};outputs={};samplers={}
    for ins in instructions:
        if ins.opcode!=DCL:continue
        token=ins.words[2];kind=register_type(token);index=token&INDEX
        target=inputs if kind==INPUT else outputs if kind==OUTPUT else samplers if kind==SAMPLER else None
        if target is None or index in target:raise ValueError('Ambiguous shader declaration.')
        target[index]=(ins.words[1],token&MASK)
    return inputs,outputs,samplers


def merge_vertex(current,previous,*,previous_sampler=3):
    if type(previous_sampler)is not int or not 0<=previous_sampler<4:raise ValueError('Invalid previous-constant sampler.')
    original=checked(current);prior=checked(previous)
    inputs,outputs,samplers=declarations(original);old_inputs,old_outputs,old_samplers=declarations(prior)
    if previous_sampler in samplers or previous_sampler not in old_samplers:raise ValueError('Previous-constant sampler collision or missing declaration.')
    if len(old_outputs)!=1 or semantic(next(iter(old_outputs.values()))[0])!=(0,0):raise ValueError('Expected a previous POSITION0-only program.')
    if sum(semantic(value[0])==(0,0)for value in outputs.values())!=1:raise ValueError('Expected a current POSITION0 declaration.')
    output=next((n for n in reversed(range(12))if n not in outputs),None)
    used_semantics={semantic(value[0])for value in outputs.values()}
    texcoord=next((n for n in range(16)if(5,n)not in used_semantics),None)
    if output is None or texcoord is None:raise ValueError('No previous-position interpolant available.')
    by_semantic={semantic(value[0]):(index,value[1])for index,value in inputs.items()}
    if len(by_semantic)!=len(inputs):raise ValueError('Duplicate input semantic.')
    input_map={}
    for index,value in old_inputs.items():
        matching=by_semantic.get(semantic(value[0]))
        if matching is None or value[1]&~matching[1]:raise ValueError('Previous geometry requires an unbound input component.')
        input_map[index]=matching[0]
    occupied={FLOAT:set(),INTEGER:set(),BOOLEAN:set()}
    sets={'float4':FLOAT,'int4':INTEGER,'bool':BOOLEAN}
    for binding in reflect(current):
        kind=sets.get(binding['register_set'])
        if kind is not None:occupied[kind].update(range(binding['register_index'],binding['register_index']+binding['register_count']))
    for ins in original:
        for at in parameters(ins):
            token=ins.words[at];kind=register_type(token)
            if kind in occupied:occupied[kind].add(token&INDEX)
    definitions={FLOAT:{},INTEGER:{},BOOLEAN:{}}
    for ins in prior:
        if ins.opcode in(DEF,DEFI,DEFB):
            token=ins.words[1];kind=register_type(token);index=token&INDEX
            expected={DEF:FLOAT,DEFI:INTEGER,DEFB:BOOLEAN}[ins.opcode]
            if kind!=expected or index in definitions[kind]:raise ValueError('Invalid previous constant definition.')
            definitions[kind][index]=None
    relative_float=any(register_type(ins.words[at])==FLOAT and ins.words[at]&RELATIVE for ins in prior for at in parameters(ins))
    for kind,mapping in definitions.items():
        if not mapping:continue
        if kind==FLOAT and relative_float:
            # FXC uses an internal identity-table lookup for dynamic vector
            # indexing. Relocate the entire DEF bank with one offset so its
            # defined relative accesses retain their original ordering.
            low,high=min(mapping),max(mapping);span=high-low+1
            if len(mapping)!=span:raise ValueError('Relative internal constants require a contiguous definition bank.')
            start=next((n for n in reversed(range(257-span))if not occupied[kind].intersection(range(n,n+span))),None)
            if start is None:raise ValueError('No safe contiguous bank for relative previous-shader constants.')
            for old in mapping:mapping[old]=start+old-low
            occupied[kind].update(range(start,start+span))
        else:
            for old in mapping:
                free=next((n for n in reversed(range(256 if kind==FLOAT else 16))if n not in occupied[kind]),None)
                if free is None:raise ValueError('No safe slot for an internal previous-shader constant.')
                mapping[old]=free;occupied[kind].add(free)
    helpers=[b for b in reflect(previous)if b['name']=='S7TemporalPreviousConstants']
    if len(helpers)!=1 or helpers[0]['register_set']!='sampler' or helpers[0]['register_index']!=previous_sampler or helpers[0]['register_count']!=1:
        raise ValueError('The previous program lacks its explicit constant-texture binding.')
    if any(b['register_set']!='sampler'for b in reflect(previous)):raise ValueError('Previous geometry still depends on current external constants.')
    for index,value in old_samplers.items():
        if index!=previous_sampler and samplers.get(index)!=value:raise ValueError('Previous geometry requires an unbound vertex texture.')
    setup=[DCL|(2<<24),PARAMETER|5|(texcoord<<16),register(OUTPUT,output)|MASK]
    for index,value in old_samplers.items():
        if index not in samplers:setup.extend((DCL|(2<<24),value[0],register(SAMPLER,index)|value[1]))
    tail=[]
    for ins in prior:
        if ins.opcode in(COMMENT,DCL):continue
        words=list(ins.words)
        for at in parameters(ins):
            token=words[at];kind=register_type(token);index=token&INDEX
            if kind in definitions:
                if index not in definitions[kind]or token&RELATIVE and kind!=FLOAT:raise ValueError('Previous program reads an undefined or unsupported relative constant.')
                words[at]=replaced(token,kind,definitions[kind][index])
            elif kind==INPUT:
                if token&RELATIVE or index not in input_map:raise ValueError('Previous input indexing cannot be remapped.')
                words[at]=replaced(token,kind,input_map[index])
            elif kind==OUTPUT:words[at]=replaced(token,OUTPUT,output)
        (setup if ins.opcode in(DEF,DEFI,DEFB)else tail).extend(words)
    result=[0xfffe0300];inserted=False
    for ins in original:
        if not inserted and ins.opcode not in SETUP:result.extend(setup);inserted=True
        result.extend(ins.words)
    if not inserted:raise ValueError('Current shader has no executable instructions.')
    result.extend(tail);result.append(0xffff)
    code=struct.pack('<'+str(len(result))+'I',*result)
    if len(code)>65536:raise ValueError('Merged vertex shader exceeds its bound.')
    checked(code)
    return MotionVertexProgram(code,output,texcoord,previous_sampler,tuple(sorted(definitions[FLOAT].items())))
