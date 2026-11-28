"""Authored GPU validation of original geometry plus previous deformation."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import struct

from . import on12_previous_source as source,on12_motion_program as program,on12_spatial,recover,dxwrapper
from . import on12_motion_pixel as pixel

ROOT=on12_spatial.ROOT
SOURCES=('native/on12_motion_gpu.cpp','native/on12_pose_history.h','native/water_on12_identity.h','tools/graphics_lab/on12_previous_source.py',
         'tools/graphics_lab/on12_motion_program.py','tools/graphics_lab/on12_motion.py','tools/graphics_lab/on12_motion_pixel.py')
ARTIFACTS=('current.vs.bin','combined.vs.bin','reference.vs.bin','color.ps.bin','previous.ps.bin',
           'combined.ps.bin','motion-reference.ps.bin','motion-layout.bin','on12_motion_gpu.exe')
MANIFEST='on12-motion-build.json'

DECLARATIONS='''
float4x4 g_VSMainViewProjectionMatrix:register(c12);
float4x4 g_World:register(c68);
float4 g_Bones[150]:register(c80);
float4 g_Wind:register(c43);
float g_Timer:register(c63);
struct Input {float4 position:POSITION0;float bone:TEXCOORD0;};
struct Output {float4 position:POSITION0;float2 uv:TEXCOORD0;};
'''
BODY='''
    float4 p=input.position;int b=(int)input.bone*3;
    p=float4(dot(g_Bones[b],p),dot(g_Bones[b+1],p),dot(g_Bones[b+2],p),1);
    p.x+=sin(g_Timer+p.y)*g_Wind.x;
    p.y+=input.position[(int)input.bone]*0.03;
    Output output;output.position=mul(g_VSMainViewProjectionMatrix,mul(g_World,p));output.uv=p.xy;return output;
'''
CURRENT=DECLARATIONS+'Output Scene(Input input){'+BODY+'}\n'


def authored_programs(*,packed=False):
    declarations=DECLARATIONS;body=BODY
    color_source='float4 Color(float2 uv:TEXCOORD0):COLOR0{clip(uv.x+0.6);return float4(saturate(uv*0.1+0.5),0.6,128.0/255.0);}'
    if packed:
        fields='float4 tint:COLOR0;'+''.join(f'float4 t{n}:TEXCOORD{n};'for n in range(9))
        declarations=DECLARATIONS.replace('float2 uv:TEXCOORD0;',fields)
        assignments='output.tint=float4(.2,.3,.4,1);'+''.join(f'output.t{n}=float4(p.xyz,{n+1}.0);'for n in range(9))
        body=BODY.replace('output.uv=p.xy;',assignments)
        ps_fields='float3 tint:COLOR0;'+''.join(f'float{3 if n in(1,5,7)else 4} t{n}:TEXCOORD{n};'for n in range(9)if n!=4)
        total='dot(i.tint,float3(.01,.02,.03))+'+'+'.join(f'dot(i.t{n},float{3 if n in(1,5,7)else 4}({",".join(str(.01*(c+1))for c in range(3 if n in(1,5,7)else 4))}))'for n in range(9)if n!=4)
        color_source='struct P{'+ps_fields+'};float4 Color(P i):COLOR0{clip(i.t0.x+.6);float value='+total+';return float4(saturate(value*.04+.4),.6,.4,128.0/255.0);}'
    current_source=declarations+'Output Scene(Input input){'+body+'}\n'
    color=source.compile_shader(color_source,'Color','ps_3_0')
    current=source.compile_vertex(current_source,'Scene')
    generated,_=source.prepare(current_source,'Scene');prior=source.compile_previous(generated)
    combined=program.merge_vertex(current,prior,pixel_program=color if packed else None)
    expected_layout=((1,3),(5,3),(7,3))if packed else((2,0),(2,1),(2,3))
    if combined.texcoord!=(4 if packed else 1)or combined.current_components!=expected_layout:raise RuntimeError('Unexpected authored interpolation layout.')
    previous_body=body
    for a,b in (('g_VSMainViewProjectionMatrix','p_Projection'),('g_World','p_World'),('g_Bones','p_Bones'),('g_Wind','p_Wind'),('g_Timer','p_Timer')):
        previous_body=previous_body.replace(a,b)
    reference=current_source+'''
float4x4 p_Projection:register(c232);float4x4 p_World:register(c236);
float4 p_Bones[6]:register(c240);float4 p_Wind:register(c246);float p_Timer:register(c247);
'''+ 'Output Before(Input input){'+previous_body+'}\n'
    if packed:
        reference+='Output Reference(Input input){Output output=Scene(input);output.t4=Before(input).position;output.t1.w=output.position.x;output.t5.w=output.position.y;output.t7.w=output.position.w;return output;}'
    else:reference+='''
struct Both {float4 position:POSITION0;float2 uv:TEXCOORD0;float4 previous:TEXCOORD1;float4 current:TEXCOORD2;};
Both Reference(Input input){Output now=Scene(input);Both output;output.position=now.position;output.current=now.position;output.uv=now.uv;output.previous=Before(input).position;return output;}
'''
    motion=pixel.merge_pixel(color,combined.texcoord,combined.current_components)
    reference_pixel='float4 Raster:register(c200);float Confidence:register(c201);\n'
    if packed:reference_pixel+='''
float4 Reference(float4 previous:TEXCOORD4,float4 a:TEXCOORD1,float4 b:TEXCOORD5,float4 c:TEXCOORD7,float2 uv:TEXCOORD0):COLOR0{
    float4 current=float4(a.w,b.w,0,c.w);
'''
    else:reference_pixel+='float4 Reference(float4 previous:TEXCOORD1,float2 uv:TEXCOORD0,float4 current:TEXCOORD2):COLOR0{\n'
    reference_pixel+='''
    clip(uv.x+0.6);
    if(previous.w<=1e-6||Confidence<=0)return 0;
    return float4((previous.xy/previous.w-current.xy/current.w)*Raster.xy+Raster.zw,previous.w,Confidence);
}'''
    return {'current.vs.bin':current,'combined.vs.bin':combined.code,'reference.vs.bin':source.compile_vertex(reference,'Reference'),
        'color.ps.bin':color,'previous.ps.bin':source.compile_shader(f'float4 Previous(float4 p:TEXCOORD{combined.texcoord}):COLOR0{{return p;}}','Previous','ps_3_0'),
        'combined.ps.bin':motion.code,'motion-reference.ps.bin':source.compile_shader(reference_pixel,'Reference','ps_3_0'),
        'motion-layout.bin':struct.pack('<3I',motion.target,motion.raster_register,motion.confidence_register)}


def prepare(output,*,packed=False):
    output=recover.lab(output)
    if output.exists()or not output.parent.is_dir():raise ValueError('Use a fresh motion build below work/.')
    payload=authored_programs(packed=packed);output.mkdir()
    for name,raw in payload.items():recover._write_new(output/name,raw)
    return output


def verify_build(directory):
    directory=recover.lab(directory);raw,_=recover._read(directory/MANIFEST,32768);value=json.loads(raw.decode('utf-8-sig'))
    expected={*SOURCES,*ARTIFACTS}
    if (not isinstance(value,dict)or set(value)!={'format','build_sha256','files','gpu_executed','packed'}or value['format']!='s7-on12-motion-build-v1'or type(value['packed'])is not bool
            or value['gpu_executed']is not False or value['build_sha256']!=on12_spatial.loader.digest(ROOT/'native/on12_motion_build.ps1')
            or not isinstance(value['files'],dict)or set(value['files'])!=expected):raise ValueError('Unknown or stale motion GPU preparation.')
    for name,pin in value['files'].items():
        path=ROOT/name if name in SOURCES else directory/name
        if on12_spatial.loader.digest(path)!=pin:raise ValueError('Motion source or binary changed; rebuild.')
        if name in SOURCES and on12_spatial.loader.digest(directory/Path(name).name)!=pin:raise ValueError('Frozen motion source changed.')
    return directory,value


def gpu(build,package,output):
    directory,manifest=verify_build(build);output=recover.lab(output)
    if output.exists()or not output.parent.is_dir()or output.is_relative_to(directory)or directory.is_relative_to(output):raise ValueError('Use a fresh separate motion GPU result below work/.')
    payload=dxwrapper.artifacts(package,'dxwrapper-d3d9on12');recover._no_games();output.mkdir()
    for name in ARTIFACTS:
        shutil.copyfile(directory/name,output/name)
        if on12_spatial.loader.digest(output/name)!=manifest['files'][name]:raise RuntimeError('Motion artifact changed during copying.')
    for name,raw in payload.items():
        if name=='dxwrapper.ini':raw=raw.replace(b'IncludeProcess=Settlers7R.exe',b'IncludeProcess=on12_motion_gpu.exe')
        recover._write_new(output/name,raw)
    verify_build(directory);recover._no_games()
    done=subprocess.run([str(output/'on12_motion_gpu.exe'),on12_spatial.WINDOWS_ON12],cwd=output,capture_output=True,timeout=45,creationflags=subprocess.CREATE_NO_WINDOW)
    result=dict(exit_code=done.returncode,stdout=done.stdout.decode(errors='replace'),stderr=done.stderr.decode(errors='replace'),synthetic_only=True,game_taa_enabled=False,packed=manifest['packed'])
    recover._write_new(output/'result.private.json',json.dumps(result,indent=2).encode());print(json.dumps(result),flush=True)
    if done.returncode:raise RuntimeError('Previous deformation GPU test failed.')
    report=json.loads(result['stdout'])
    if any(report.get(k)is not True for k in ('passed','synthetic_only','current_image_unchanged','previous_reference_matched','bone_wind_rigid_perspective',
                                             'motion_pixels_matched','static_jitter_excluded','invalid_previous_rejected','alpha_discard_preserved','all_alpha_functions_preserved')):
        raise RuntimeError('Incomplete previous deformation GPU result.')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--prepare',type=Path);parser.add_argument('--build',type=Path)
    parser.add_argument('--package',type=Path);parser.add_argument('--output',type=Path);parser.add_argument('--packed',action='store_true');args=parser.parse_args()
    if args.prepare is not None:
        if any(x is not None for x in(args.build,args.package,args.output)):parser.error('--prepare only compiles authored shaders on the CPU')
        print(prepare(args.prepare,packed=args.packed))
    elif args.packed:parser.error('--packed requires --prepare')
    elif all(x is not None for x in(args.build,args.package,args.output)):gpu(args.build,args.package,args.output)
    else:parser.error('Use --prepare, or --build --package --output for an explicit GPU test')


if __name__=='__main__':main()
