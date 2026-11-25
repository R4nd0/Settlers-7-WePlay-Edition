"""Metadata-only original DX9 pass inventory, including mixed Vision libraries.

Nested/per-record source fences and explicit non-DX9 envelopes are handled here
without broadening the material replacement parser. No shader/source body is
exported, no runtime is activated, and resource names do not prove pixel origin.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import mmap
from pathlib import Path
import re
import struct

from tools.material_shaders import catalog, inventory, semantics, shaderlib

MAX_PROGRAM = 256*1024
MAX_RECORDS = 256  # The original S7 Entity High library has 165 records.
FENCE = re.compile(rb'(?:SOURCECODE|vs_sourcecode|ps_sourcecode)[ \t]*=[ \t]*\$([A-Za-z_][A-Za-z0-9_]{0,63})\{', re.I)
CODE = re.compile(rb'(?m)^[ \t]*(vs|ps)_bytecode\[([A-Za-z0-9_]+)\][ \t]*=[ \t]*\{([0-9A-Fa-f \t\r\n]+)\}')
STATE_KEYS = frozenset(('blendenable','srcblend','dstblend','alphatestenabled','alphatestfunc','alphatestref',
    'zwriteenable','ztestfunc','depthwriteenabled','depthcomparisonfunc','rst_flags',
    'cullmode','cullenable','numsamplers_ps','numsamplers_vs'))
DEFAULT_RESOURCES = (
    'Graphics/Vision/Shaders/BlobShadow.ShaderLib',
    'Graphics/Shaders/GUI.ShaderLib', 'Graphics/Shaders/Overlay.ShaderLib',
) + tuple(f'Graphics/Shaders/{kind}_{vendor}_High.ShaderLib'
          for kind in ('Decal','PostFX','Shadow','Water') for vendor in ('ATI','ATIx','NV'))


def masked(data):
    """Mask every independent source fence/comment/string, preserving offsets."""
    out=bytearray(data)
    def erase(start,end):
        out[start:end]=re.sub(rb'[^\r\n]',b' ',out[start:end])
    position=0;fences=0
    while position<len(data):
        source=FENCE.match(data,position) if position==0 or data[position-1] in b' \t\r\n' else None
        if source:
            fences+=1
            if fences>1024:raise ValueError('Source fence bound exceeded')
            closer=b'}$'+source[1];end=data.find(closer,source.end())
            if end<0:raise ValueError('Unterminated independent source fence')
            end+=len(closer)
        elif data[position:position+2]==b'//':
            end=data.find(b'\n',position+2);end=len(data) if end<0 else end
        elif data[position:position+2]==b'/*':
            end=data.find(b'*/',position+2)
            if end<0:raise ValueError('Unterminated pass comment')
            end+=2
        elif data[position]==34:
            end=position+1
            while end<len(data):
                if data[end]==92:end+=2
                elif data[end]==34:end+=1;break
                else:end+=1
            else:raise ValueError('Unterminated pass string')
            if end>len(data):raise ValueError('Unterminated escaped pass string')
        else:
            position+=1;continue
        erase(position,end);position=end
    return out


def inspect_library(data):
    if not isinstance(data,bytes) or not 0<len(data)<=inventory.MAX_PAYLOAD or not shaderlib.HEADER.match(data):
        raise ValueError('Expected bounded original ShaderLib2.3/DX9 resource')
    view=masked(data);matches=list(shaderlib.RECORD.finditer(view))
    if not 1<=len(matches)<=MAX_RECORDS:raise ValueError('Unsupported bounded pass record count')
    records=[];programs=[];ignored=[];names=set();prior_end=0
    for match in matches:
        name=match[1].decode('ascii')
        if name in names or match.start()<prior_end:raise ValueError('Duplicate or nested shader record')
        names.add(name);depth=1;end=match.end()
        while end<len(view) and depth:
            depth+=(view[end]==123)-(view[end]==125);end+=1
        if depth:raise ValueError('Unterminated pass record')
        prior_end=end;body=bytes(view[match.end():end-1]);raw=data[match.end():end-1]
        declarations=list(re.finditer(rb'\b(?:vs|ps)_bytecode\b',body));encoded=list(CODE.finditer(body))
        if len(declarations)!=len(encoded):raise ValueError('Malformed compiled pass declaration')
        declared={}
        for state in re.finditer(rb'(?m)^[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*=[ \t]*([A-Za-z0-9_.+-]+)[ \t]*;',body):
            key=state[1].decode('ascii').lower()
            if key in STATE_KEYS:declared.setdefault(key,[]).append(state[2].decode('ascii'))
        records.append({'shader_record':name,'declared_states':declared,'runtime_state_verified':False})
        seen=set()
        for code in encoded:
            stage='vertex' if code[1]==b'vs' else 'pixel';profile=code[2].decode('ascii')
            value=bytes.fromhex(raw[code.start(3):code.end(3)].decode('ascii'))
            if len(value)<8 or len(value)>MAX_PROGRAM+4 or struct.unpack_from('<I',value)[0]!=len(value)-4:
                raise ValueError('Invalid bounded encoded pass envelope length')
            program=value[4:];supported=profile in ('vs_2_0','vs_3_0','ps_2_0','ps_3_0')
            expected=(0xfffe if stage=='vertex' else 0xffff)<<16
            if supported and not profile.startswith(code[1].decode('ascii')+'_'):
                raise ValueError('Pass stage/profile mismatch')
            expected|=int(profile[3])<<8 if supported else 0
            actual_version=struct.unpack_from('<I',program)[0]
            if supported and actual_version in (0xfffe0200,0xfffe0300,0xffff0200,0xffff0300) and actual_version!=expected:
                raise ValueError('Advertised D3D9 stage/model disagrees with its actual envelope')
            if not supported or actual_version!=expected:
                ignored.append({'shader_record':name,'stage':stage,'declared_profile':profile,
                    'encoded_bytes':len(value),'encoded_sha256':shaderlib.digest(value),
                    'reason':'explicit non-DX9 or unsupported shader envelope'})
                continue
            candidates=inventory.token_candidates(program)
            model=profile[3]+'.0'
            if (len(candidates)!=1 or candidates[0]['offset']!=0 or candidates[0]['bytes']!=len(program)
                    or candidates[0]['stage']!=stage or candidates[0]['model']!=model):
                raise ValueError('Malformed advertised D3D9 program token stream')
            key=(stage,model)
            if key in seen:raise ValueError('Ambiguous duplicate D3D9 program for one pass/profile')
            seen.add(key)
            programs.append({'shader_record':name,'stage':stage,'model':model,
                'compiled_bytes':len(program),'compiled_sha256':shaderlib.digest(program),
                'bindings':semantics.reflect(program),'material_binding_verified':False})
    if not programs:raise ValueError('No verified D3D9 program envelopes')
    return {'resource_sha256':shaderlib.digest(data),'records':records,'programs':programs,
        'ignored_envelopes':ignored,'contains_program_bytes':False,'runtime_binding_verified':False}


def build(resources):
    if not 1<=len(resources)<=32:raise ValueError('Select one to32 explicit pass libraries')
    rows=[];identities=[];seen=set()
    for name,data in sorted(resources):
        inventory.safe_name(name)
        if name in seen:raise ValueError('Duplicate original pass resource')
        seen.add(name);library=inspect_library(data)
        group='water' if '/Water_' in name else 'diagnostic-pass'
        identities.append({'resource_name':name,**{k:v for k,v in library.items() if k!='programs'}})
        for program in library['programs']:
            if len(rows)>=catalog.MAX_ENTRIES:raise ValueError('Observer512 entry limit exceeded')
            rows.append({'id':len(rows)+1,'family':1 if group=='water' else 2,
                'observation_group':group,'resource_name':name,**program})
    binary=catalog.HEADER.pack(catalog.MAGIC,1,len(rows))
    for row in rows:
        binary+=catalog.ENTRY.pack(row['id'],row['family'],1 if row['stage']=='vertex' else 2,
            row['compiled_bytes'],bytes.fromhex(row['compiled_sha256']))
    aliases={}
    for row in rows:aliases.setdefault((row['stage'],row['compiled_bytes'],row['compiled_sha256']),[]).append(row['id'])
    return binary,{'format':'s7-original-pass-observer-catalog-v1','entry_count':len(rows),'catalog_bytes':len(binary),
        'catalog_sha256':shaderlib.digest(binary),'entries':rows,'resources':identities,
        'alias_groups':[ids for ids in aliases.values() if len(ids)>1],
        'numeric_family_note':'Legacy wire family2 labels diagnostic programs only; never enables a material effect.',
        'contains_program_bytes':False,'runtime_binding_verified':False,'replacement_enabled':False,
        'streak_origin_established':False}


def from_archive(path,expected_directory,resources=DEFAULT_RESOURCES):
    path=Path(path).absolute()
    for part in (path,*path.parents):
        if part.is_symlink() or (hasattr(part,'is_junction') and part.is_junction()):raise ValueError('No archive links/junctions')
    path=path.resolve()
    if not path.is_relative_to((inventory.ROOT/'work').resolve()):raise ValueError('Use the explicit owned archive below work/')
    baseline=inventory.inspect_archive(path,None,list(resources),expected_directory=expected_directory)
    pins={item['name']:item['sha256'] for item in baseline['inspected']};values=[]
    with path.open('rb') as stream,mmap.mmap(stream.fileno(),0,access=mmap.ACCESS_READ) as raw:
        archive=inventory.bba.Archive(raw,copy=False)
        if shaderlib.digest(archive.header+archive.directory)!=expected_directory:raise ValueError('Original archive directory changed')
        for name in resources:
            data=inventory.read_payload(archive,name)
            if shaderlib.digest(data)!=pins[name]:raise ValueError('Selected original pass resource changed')
            values.append((name,data))
    binary,metadata=build(values)
    metadata.update(input_directory_sha256=expected_directory,full_archive_fingerprint_verified=False)
    return binary,metadata


def capture_config(metadata):
    """Stage eight exact-pair read-only targets, with every requested CTAB checked."""
    from tools.graphics_lab.reconfigure import validate_config
    rows=metadata['entries']
    def pair(resource,record):
        found=[r for r in rows if r['resource_name']==resource and r['shader_record']==record]
        if len(found)!=2 or {r['stage'] for r in found}!={'vertex','pixel'}:
            raise ValueError('Observation requires an explicit source VS/PS pair, never inferred inheritance')
        return {r['stage']:r for r in found}
    targets=[]
    def target(resource,record,requests,samplers):
        stages=pair(resource,record);constants=[]
        for stage,name,index,count,mask in requests:
            found=[b for b in stages[stage]['bindings'] if b['name']==name]
            if len(found)!=1 or found[0]['register_set']!='float4' or found[0]['register_index']!=index or found[0]['register_count']!=count:
                raise ValueError('Original pass CTAB constant changed')
            constants.extend((1 if stage=='vertex' else 2,index+i,mask) for i in range(count))
        sampler_mask=0
        for name,index in samplers:
            found=[b for b in stages['pixel']['bindings'] if b['name']==name]
            if len(found)!=1 or found[0]['register_set']!='sampler' or found[0]['register_index']!=index or found[0]['register_count']!=1:
                raise ValueError('Original pass CTAB sampler changed')
            sampler_mask|=1<<index
        if len(constants)>8:raise ValueError('Pass request exceeds existing bounded observer ABI')
        targets.append({'resource':resource,'shader_record':record,'vertex_id':stages['vertex']['id'],
            'pixel_id':stages['pixel']['id'],'vertex_sha256':stages['vertex']['compiled_sha256'],
            'pixel_sha256':stages['pixel']['compiled_sha256'],'requests':constants,'sampler_mask':sampler_mask})
    target('Graphics/Shaders/Water_ATI_High.ShaderLib','RiverColor',[
        ('vertex','g_VSMainViewProjectionMatrix',12,4,15),('vertex','g_VSEye',16,1,7),
        ('pixel','g_PSSunDirection',5,1,7),('pixel','g_PSSunSpecularColor',7,1,7),('pixel','g_UVScrollTimer',80,1,15)],[])
    for record,color in (('BlobShadow',2),('BlobShadow_Terrain',7)):
        requests=[('vertex','matMVP',8,4,15),('vertex','TexRefPlaneU',64,1,15),('vertex','TexRefPlaneV',65,1,15),('pixel','ShadowColor',color,1,15)]
        if record.endswith('_Terrain'):requests.insert(1,('vertex','SectorScaleOfs',38,1,15))
        target('Graphics/Vision/Shaders/BlobShadow.ShaderLib',record,requests,[('ShadowTexture',0)])
    target('Graphics/Shaders/Shadow_ATI_High.ShaderLib','MaskShader',[
        ('vertex','g_ScreenMatrix',64,4,15),('vertex','g_VSScreenSize',18,1,15),
        ('pixel','g_InverseShadowMapSize',57,1,3),('pixel','g_KernelRadius',33,1,15),('pixel','g_FadeoutTransform',47,1,15)],
        [('g_DepthMap',0),('g_ShadowMap',1),('g_RotationMap',2)])
    target('Graphics/Shaders/PostFX_ATI_High.ShaderLib','SSAOShader',[
        ('vertex','g_ScreenMatrix',64,4,15),('vertex','g_TargetSize',68,1,15),
        ('pixel','g_PSCamera',0,1,15),('pixel','g_SSAOStrength',81,1,15)],[('g_DepthMap',0)])
    target('Graphics/Shaders/PostFX_ATI_High.ShaderLib','SSAOBlurShader',[
        ('vertex','g_ScreenMatrix',64,4,15),('vertex','g_VSScreenSize',18,1,15),
        ('pixel','g_PSTargetSize',37,1,15)],[('g_DepthMap',0),('g_SSAOMap',1)])
    target('Graphics/Shaders/GUI.ShaderLib','UIShader',[('vertex','g_GUIMatrix',64,4,15)],
        [('g_Texture1',0),('g_Texture2',1),('g_Texture3',2),('g_Texture4',3)])
    target('Graphics/Shaders/Decal_ATI_High.ShaderLib','OuterShader',[
        ('vertex','g_VSDecalViewProjMatrix',70,4,15),('vertex','g_VSScreenSize',18,1,15),
        ('vertex','g_Transform',68,1,15),('pixel','g_ColorMark',69,1,15),('pixel','g_PSShadowFactor',2,1,15)],
        [('g_NormalMap',0),('g_OuterDiffuseMap',2),('g_OuterMaskMap',4),('g_ShadowMap',5),('g_AOMap',6)])
    raw=struct.pack('<4IQ',952,1,len(targets),1,0)
    for row in targets:
        raw+=struct.pack('<5I',row['vertex_id'],row['pixel_id'],3,len(row['requests']),row['sampler_mask'])
        raw+=b''.join(struct.pack('<3I',*r) for r in row['requests'])+bytes((8-len(row['requests']))*12)
    pin=shaderlib.digest(raw);validate_config(raw,pin)
    return raw,{'format':'s7-original-pass-observation-config-v1','config_sha256':pin,
        'catalog_sha256':metadata['catalog_sha256'],'targets':targets,'frame_stride':1,'sample_limit':3,
        'read_only':True,'suppression_enabled':False,'runtime_binding_verified':False,'streak_origin_established':False,
        'limits':['first matching draw per target/frame; not every instance/cascade',
                  'current ABI records blend/test booleans only; no blend factors/depth/stencil/sampler states',
                  'zero observations are inconclusive if cache/query/capture/unknown overflows occur']}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive',type=Path,required=True)
    parser.add_argument('--directory-sha256',required=True)
    parser.add_argument('--output-directory',type=Path,required=True)
    parser.add_argument('--include-entity-ati',action='store_true',help='Add the original145 ATI Entity programs for known-pair observation; does not infer inherited pairings')
    args=parser.parse_args()
    resources=DEFAULT_RESOURCES+(('Graphics/Shaders/Entity_ATI_High.ShaderLib',) if args.include_entity_ati else ())
    binary,metadata=from_archive(args.archive,args.directory_sha256,resources)
    config,proof=capture_config(metadata)
    catalog.write(binary,metadata,args.output_directory)
    with (args.output_directory/'capture.bin').open('xb') as stream:stream.write(config)
    with (args.output_directory/'capture.private.json').open('x',encoding='utf-8') as stream:json.dump(proof,stream,indent=2)
    print(json.dumps({'entries':metadata['entry_count'],'catalog_sha256':metadata['catalog_sha256'],
        'runtime_binding_verified':False,'streak_origin_established':False}))


if __name__=='__main__':main()
