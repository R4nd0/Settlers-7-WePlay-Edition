"""Compose original SM3 geometry with a separately compiled previous position.

Original instructions remain byte-identical. Position writes are mirrored to
a current-clip TEXCOORD; the appended previous program exposes a second one.
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
    current_output:int
    current_texcoord:int
    current_components:tuple
    original_execution_offsets:tuple


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


def merge_vertex(current,previous,*,previous_sampler=3,pixel_program=None):
    if type(previous_sampler)is not int or not 0<=previous_sampler<4:raise ValueError('Invalid previous-constant sampler.')
    original=checked(current);prior=checked(previous)
    inputs,outputs,samplers=declarations(original);old_inputs,old_outputs,old_samplers=declarations(prior)
    if previous_sampler in samplers or previous_sampler not in old_samplers:raise ValueError('Previous-constant sampler collision or missing declaration.')
    if len(old_outputs)!=1 or semantic(next(iter(old_outputs.values()))[0])!=(0,0):raise ValueError('Expected a previous POSITION0-only program.')
    if sum(semantic(value[0])==(0,0)for value in outputs.values())!=1:raise ValueError('Expected a current POSITION0 declaration.')
    free_outputs=[n for n in reversed(range(12))if n not in outputs]
    used_semantics={semantic(value[0])for value in outputs.values()}
    free_semantics=[n for n in range(16)if(5,n)not in used_semantics]
    compact=len(free_outputs)<2;pixel_inputs={};carriers=[];temporary=None
    if pixel_program is not None:
        for ins in decode(pixel_program,stage='pixel'):
            if ins.opcode==DCL and register_type(ins.words[2])==INPUT:
                key=semantic(ins.words[1])
                if key in pixel_inputs:raise ValueError('Ambiguous paired pixel input.')
                pixel_inputs[key]=ins.words[2]&MASK
        compact=compact or len(pixel_inputs)>8
    if compact:
        if pixel_program is None:raise ValueError('A packed position layout requires the original pixel program.')
        for reg,value in outputs.items():
            usage,index=semantic(value[0]);used=pixel_inputs.get((usage,index))
            if usage==5 and used is not None:
                for component in range(4):
                    if not used&(1<<(16+component)):carriers.append((reg,index,component))
        if len(carriers)<3:raise ValueError('No safe channels for current clip x/y/w.')
        carriers=carriers[:3]
        reusable=[(reg,semantic(value[0])[1])for reg,value in outputs.items()if semantic(value[0])[0]==5 and semantic(value[0])not in pixel_inputs]
        if reusable:output,texcoord=reusable[0]
        elif free_outputs and free_semantics:output,texcoord=free_outputs[0],free_semantics[0]
        else:raise ValueError('No safe previous-position output.')
        used_temps={ins.words[at]&INDEX for ins in original for at in parameters(ins)if register_type(ins.words[at])==TEMP}
        temporary=next((n for n in reversed(range(32))if n not in used_temps),None)
        if temporary is None:raise ValueError('No safe current-position shadow temporary.')
        current_output=current_texcoord=-1
        current_components=tuple((index,component)for _,index,component in carriers)
    else:
        if len(free_outputs)<2 or len(free_semantics)<2:raise ValueError('No paired clip-position interpolants available.')
        output,current_output=free_outputs[:2];texcoord,current_texcoord=free_semantics[:2]
        current_components=((current_texcoord,0),(current_texcoord,1),(current_texcoord,3))
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
    setup=[];extensions={}
    if output not in outputs:setup.extend((DCL|(2<<24),PARAMETER|5|(texcoord<<16),register(OUTPUT,output)|MASK))
    else:extensions[output]=MASK
    if not compact:setup.extend((DCL|(2<<24),PARAMETER|5|(current_texcoord<<16),register(OUTPUT,current_output)|MASK))
    for reg,_,component in carriers:extensions[reg]=extensions.get(reg,0)|(1<<(16+component))
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
    position=next(index for index,value in outputs.items()if semantic(value[0])==(0,0))
    result=[0xfffe0300];inserted=False;mirrored=0;original_offsets=[]
    for ins in original:
        if not inserted and ins.opcode not in SETUP:result.extend(setup);inserted=True
        words=list(ins.words)
        if ins.opcode==DCL and register_type(words[2])==OUTPUT and words[2]&INDEX in extensions:words[2]|=extensions[words[2]&INDEX]
        if ins.opcode not in SETUP:original_offsets.append(len(result)*4)
        result.extend(words)
        if ins.opcode not in SETUP and len(ins.words)>1 and register_type(ins.words[1])==OUTPUT and ins.words[1]&INDEX==position:
            duplicate=list(ins.words);mirrored|=duplicate[1]&MASK;duplicate[1]=replaced(duplicate[1],TEMP if compact else OUTPUT,temporary if compact else current_output);result.extend(duplicate)
    if not inserted:raise ValueError('Current shader has no executable instructions.')
    if mirrored!=MASK:raise ValueError('Current clip position is not fully defined.')
    if compact:
        for (reg,_,component),source in zip(carriers,(0,1,3)):
            result.extend((1|(2<<24),register(OUTPUT,reg)|(1<<(16+component)),register(TEMP,temporary)|(source*0x55<<16)))
    result.extend(tail);result.append(0xffff)
    code=struct.pack('<'+str(len(result))+'I',*result)
    if len(code)>65536:raise ValueError('Merged vertex shader exceeds its bound.')
    checked(code)
    return MotionVertexProgram(code,output,texcoord,previous_sampler,tuple(sorted(definitions[FLOAT].items())),current_output,current_texcoord,current_components,tuple(original_offsets))
