"""Append unjittered previous-pixel motion to an original SM3 pixel program.

The caller must bind an unused MRT and qualified per-draw previous data. This
does not choose render targets, establish object identity, or enable game TAA.
"""
from dataclasses import dataclass
import math
import struct

from tools.material_shaders.bytecode import decode,register_type
from tools.material_shaders.semantics import reflect
from . import on12_motion_program as vertex,on12_previous_source as compiler

INPUT=1;FLOAT=2;OUTPUT=8;DEPTH=9;MISC=17
RASTER='S7TemporalMotionRaster';CONFIDENCE='S7TemporalMotionConfidence'


def current_layout(value):
    layout=((value,0),(value,1),(value,3))if type(value)is int else tuple(value)
    if len(layout)!=3 or len(set(layout))!=3 or any(len(item)!=2 or type(item[0])is not int or not 0<=item[0]<16 or type(item[1])is not int or not 0<=item[1]<4 for item in layout):
        raise ValueError('Invalid current clip layout.')
    return layout


def helper_source(texcoord,current):
    layout=current_layout(current)
    if type(texcoord)is not int or not 0<=texcoord<16 or any(index==texcoord for index,_ in layout):raise ValueError('Invalid paired clip-position semantics.')
    indices=sorted({index for index,_ in layout});names={index:'clip'+str(n)for n,index in enumerate(indices)}
    inputs=','.join(f'float4 {names[index]}:TEXCOORD{index}'for index in indices)
    values=[names[index]+'.'+'xyzw'[component]for index,component in layout]
    expression=f'float4({values[0]},{values[1]},0,{values[2]})'
    return f'''
float4 {RASTER}:register(c0);float {CONFIDENCE}:register(c1);
float4 Motion(float4 previous:TEXCOORD{texcoord},{inputs}):COLOR0{{
    float4 current={expression};
    bool valid=all(abs(previous)<3.4e38)&&all(abs(current)<3.4e38)&&previous.w>1e-6&&current.w>1e-6&&{CONFIDENCE}>0&&{CONFIDENCE}<=1;
    float denominator=valid?previous.w:1;
    float current_denominator=valid?current.w:1;
    float2 motion=(previous.xy/denominator-current.xy/current_denominator)*{RASTER}.xy+{RASTER}.zw;
    return valid?float4(motion,previous.w,{CONFIDENCE}):0;
}}
'''


def reference_motion(previous,current,width,height,jitter=(0,0),confidence=1):
    if len(previous)!=4 or len(current)!=4 or len(jitter)!=2 or not 0<width<=16384 or not 0<height<=16384:
        raise ValueError('Invalid motion reference dimensions.')
    if not all(math.isfinite(x)and abs(x)<3.4e38 for x in(*previous,*current))or previous[3]<=1e-6 or current[3]<=1e-6 or not 0<confidence<=1:return(0,0,0,0)
    return((previous[0]/previous[3]-current[0]/current[3])*width*.5+jitter[0],
           -(previous[1]/previous[3]-current[1]/current[3])*height*.5+jitter[1],previous[3],confidence)


def checked(program):
    if not isinstance(program,bytes)or not 16<=len(program)<=65536:raise ValueError('Invalid motion pixel program size.')
    instructions=decode(program,stage='pixel')
    for ins in instructions:
        if ins.opcode!=vertex.COMMENT and(ins.opcode in vertex.FORBIDDEN or ins.words[0]&0x50000000):raise ValueError('Unsupported pixel control-flow envelope.')
        if ins.opcode==vertex.DCL and len(ins.words)!=3:raise ValueError('Invalid pixel declaration.')
        if ins.opcode in(vertex.DEF,vertex.DEFI,vertex.DEFB)and len(ins.words)!=(3 if ins.opcode==vertex.DEFB else 6):raise ValueError('Invalid pixel constant definition.')
        for at in vertex.parameters(ins):
            token=ins.words[at];kind=register_type(token);index=token&vertex.INDEX
            if not token&vertex.PARAMETER or kind in(11,12,13):raise ValueError('Unsupported pixel register envelope.')
            maximum={0:32,INPUT:10,FLOAT:224,7:16,OUTPUT:4,DEPTH:1,10:16,14:16,MISC:2}.get(kind)
            if maximum is not None and index>=maximum:raise ValueError('Pixel register is out of range.')
    return instructions


@dataclass(frozen=True)
class MotionPixelProgram:
    code:bytes
    target:int
    raster_register:int
    confidence_register:int
    previous_input:int
    current_input:int
    current_inputs:tuple


def merge_pixel(current,texcoord,current_texcoord):
    original=checked(current);helper=compiler.compile_shader(helper_source(texcoord,current_texcoord),'Motion','ps_3_0');tail=checked(helper)
    bindings=reflect(helper)
    if {b['name']for b in bindings}!={RASTER,CONFIDENCE}or any(b['register_set']!='float4'or b['register_count']!=1 for b in bindings):
        raise ValueError('Unexpected motion parameter layout.')
    declarations={};colors=set();occupied={FLOAT:set(),7:set(),14:set()}
    for b in reflect(current):
        kind={'float4':FLOAT,'int4':7,'bool':14}.get(b['register_set'])
        if kind is not None:occupied[kind].update(range(b['register_index'],b['register_index']+b['register_count']))
    for ins in original:
        if ins.opcode==vertex.DCL:
            token=ins.words[2];key=(register_type(token),token&vertex.INDEX)
            if key in declarations:raise ValueError('Duplicate pixel declaration.')
            declarations[key]=(ins.words[1],token&vertex.MASK)
        for at in vertex.parameters(ins):
            token=ins.words[at];kind=register_type(token);index=token&vertex.INDEX
            if kind in occupied:occupied[kind].add(index)
            if kind==OUTPUT:colors.add(index)
    if 0 not in colors:raise ValueError('Expected an original COLOR0 program.')
    target=next((n for n in range(1,4)if n not in colors),None)
    slots=[n for n in reversed(range(10))if(INPUT,n)not in declarations]
    if target is None:raise ValueError('No free motion MRT.')
    layout=current_layout(current_texcoord);current_semantics={(5,index)for index,_ in layout}
    semantics={};extensions={};existing_semantics={vertex.semantic(value[0]):(index,value[1])for(kind,index),value in declarations.items()if kind==INPUT}
    if (5,texcoord)in existing_semantics:raise ValueError('Previous-position input semantic collision.')
    parameter_base=next((n for n in reversed(range(223))if n not in occupied[FLOAT]and n+1 not in occupied[FLOAT]),None)
    if parameter_base is None:raise ValueError('No safe motion parameter bank.')
    occupied[FLOAT].update((parameter_base,parameter_base+1));mapping={FLOAT:{0:parameter_base,1:parameter_base+1},7:{},14:{}}
    for ins in tail:
        if ins.opcode in(vertex.DEF,vertex.DEFI,vertex.DEFB):
            token=ins.words[1];kind=register_type(token);old=token&vertex.INDEX
            if kind not in mapping or old in mapping[kind]:raise ValueError('Conflicting helper definition.')
            free=next((n for n in reversed(range(224 if kind==FLOAT else 16))if n not in occupied[kind]),None)
            if free is None:raise ValueError('No free internal motion constant.')
            mapping[kind][old]=free;occupied[kind].add(free)
    setup=[];executable=[];helper_inputs={}
    for ins in tail:
        if ins.opcode==vertex.COMMENT:continue
        words=list(ins.words)
        if ins.opcode==vertex.DCL:
            token=words[2];kind=register_type(token);index=token&vertex.INDEX
            if kind==INPUT:
                name=vertex.semantic(words[1]);required=token&vertex.MASK
                if name not in current_semantics|{(5,texcoord)}or index in helper_inputs:raise ValueError('Unexpected motion interpolant.')
                existing=existing_semantics.get(name)
                if existing:
                    slot,used=existing
                    if required&used:raise ValueError('Current clip data overlaps an original pixel input.')
                    extensions[slot]=extensions.get(slot,0)|required
                    helper_inputs[index]=slot;semantics[name]=slot;continue
                if not slots:raise ValueError('No free pixel input for clip data.')
                slot=slots.pop(0);semantics[name]=slot
                helper_inputs[index]=slot;words[2]=vertex.replaced(token,INPUT,slot)
            elif kind==MISC and index==0:
                existing=declarations.get((MISC,0))
                if existing:
                    if token&vertex.MASK&~existing[1]:raise ValueError('Existing VPOS declaration lacks required components.')
                    continue
            else:raise ValueError('Unexpected motion helper input.')
            setup.extend(words);continue
        for at in vertex.parameters(ins):
            token=words[at];kind=register_type(token);index=token&vertex.INDEX
            if kind in mapping:
                if token&vertex.RELATIVE or index not in mapping[kind]:raise ValueError('Unbound motion helper constant.')
                words[at]=vertex.replaced(token,kind,mapping[kind][index])
            elif kind==INPUT:
                if token&vertex.RELATIVE or index not in helper_inputs:raise ValueError('Unexpected motion helper input access.')
                words[at]=vertex.replaced(token,INPUT,helper_inputs[index])
            elif kind==OUTPUT:
                if index!=0:raise ValueError('Unexpected motion helper output.')
                words[at]=vertex.replaced(token,OUTPUT,target)
        (setup if ins.opcode in(vertex.DEF,vertex.DEFI,vertex.DEFB)else executable).extend(words)
    if set(semantics)!=current_semantics|{(5,texcoord)}:raise ValueError('Motion helper lacks paired clip-position inputs.')
    result=[0xffff0300];inserted=False
    for ins in original:
        if not inserted and ins.opcode not in vertex.SETUP:result.extend(setup);inserted=True
        words=list(ins.words)
        if ins.opcode==vertex.DCL and register_type(words[2])==INPUT and words[2]&vertex.INDEX in extensions:words[2]|=extensions[words[2]&vertex.INDEX]
        result.extend(words)
    if not inserted:raise ValueError('Original pixel shader has no executable instructions.')
    result.extend(executable);result.append(0xffff)
    code=struct.pack('<'+str(len(result))+'I',*result)
    checked(code)
    current_inputs=tuple(semantics[name]for name in sorted(current_semantics))
    return MotionPixelProgram(code,target,parameter_base,parameter_base+1,semantics[(5,texcoord)],current_inputs[0],current_inputs)
