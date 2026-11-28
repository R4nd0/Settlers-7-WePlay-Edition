"""Authored GPU validation of original geometry plus previous deformation."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

from . import on12_previous_source as source,on12_motion_program as program,on12_spatial,recover,dxwrapper

ROOT=on12_spatial.ROOT
SOURCES=('native/on12_motion_gpu.cpp','native/water_on12_identity.h','tools/graphics_lab/on12_previous_source.py',
         'tools/graphics_lab/on12_motion_program.py','tools/graphics_lab/on12_motion.py')
ARTIFACTS=('current.vs.bin','combined.vs.bin','reference.vs.bin','color.ps.bin','previous.ps.bin','on12_motion_gpu.exe')
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


def authored_programs():
    current=source.compile_vertex(CURRENT,'Scene')
    generated,_=source.prepare(CURRENT,'Scene');prior=source.compile_previous(generated)
    combined=program.merge_vertex(current,prior)
    if combined.texcoord!=1:raise RuntimeError('Unexpected authored interpolation layout.')
    previous_body=BODY
    for a,b in (('g_VSMainViewProjectionMatrix','p_Projection'),('g_World','p_World'),('g_Bones','p_Bones'),('g_Wind','p_Wind'),('g_Timer','p_Timer')):
        previous_body=previous_body.replace(a,b)
    reference=CURRENT+'''
float4x4 p_Projection:register(c232);float4x4 p_World:register(c236);
float4 p_Bones[6]:register(c240);float4 p_Wind:register(c246);float p_Timer:register(c247);
'''+ 'Output Before(Input input){'+previous_body+'''}
struct Both {float4 position:POSITION0;float2 uv:TEXCOORD0;float4 previous:TEXCOORD1;};
Both Reference(Input input){Output now=Scene(input);Both output;output.position=now.position;output.uv=now.uv;output.previous=Before(input).position;return output;}
'''
    return {'current.vs.bin':current,'combined.vs.bin':combined.code,'reference.vs.bin':source.compile_vertex(reference,'Reference'),
        'color.ps.bin':source.compile_shader('float4 Color(float2 uv:TEXCOORD0):COLOR0{return float4(saturate(uv*0.1+0.5),0.6,1);}','Color','ps_3_0'),
        'previous.ps.bin':source.compile_shader('float4 Previous(float4 p:TEXCOORD1):COLOR0{return p;}','Previous','ps_3_0')}


def prepare(output):
    output=recover.lab(output)
    if output.exists()or not output.parent.is_dir():raise ValueError('Use a fresh motion build below work/.')
    payload=authored_programs();output.mkdir()
    for name,raw in payload.items():recover._write_new(output/name,raw)
    return output


def verify_build(directory):
    directory=recover.lab(directory);raw,_=recover._read(directory/MANIFEST,32768);value=json.loads(raw.decode('utf-8-sig'))
    expected={*SOURCES,*ARTIFACTS}
    if (not isinstance(value,dict)or set(value)!={'format','build_sha256','files','gpu_executed'}or value['format']!='s7-on12-motion-build-v1'
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
    result=dict(exit_code=done.returncode,stdout=done.stdout.decode(errors='replace'),stderr=done.stderr.decode(errors='replace'),synthetic_only=True,game_taa_enabled=False)
    recover._write_new(output/'result.private.json',json.dumps(result,indent=2).encode());print(json.dumps(result),flush=True)
    if done.returncode:raise RuntimeError('Previous deformation GPU test failed.')
    report=json.loads(result['stdout'])
    if any(report.get(k)is not True for k in ('passed','synthetic_only','current_image_unchanged','previous_reference_matched','bone_wind_rigid_perspective')):
        raise RuntimeError('Incomplete previous deformation GPU result.')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--prepare',type=Path);parser.add_argument('--build',type=Path)
    parser.add_argument('--package',type=Path);parser.add_argument('--output',type=Path);args=parser.parse_args()
    if args.prepare is not None:
        if any(x is not None for x in(args.build,args.package,args.output)):parser.error('--prepare only compiles authored shaders on the CPU')
        print(prepare(args.prepare))
    elif all(x is not None for x in(args.build,args.package,args.output)):gpu(args.build,args.package,args.output)
    else:parser.error('Use --prepare, or --build --package --output for an explicit GPU test')


if __name__=='__main__':main()
