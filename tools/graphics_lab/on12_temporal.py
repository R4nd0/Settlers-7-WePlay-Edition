"""Source-bound CPU preparation and explicitly separate temporal GPU validation."""
import argparse
from array import array
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess

from . import capture_bootstrap as loader, recover

ROOT=Path(__file__).resolve().parents[2]
SOURCES=('on12_temporal.h','on12_temporal.cpp','on12_temporal.hlsl','on12_temporal_pass.h',
         'on12_temporal_pass.cpp','on12_temporal_fixture.cpp','on12_temporal_gpu.cpp',
         'on12_temporal_inputs.h','on12_temporal_inputs_fixture.cpp','on12_temporal_camera.h',
         'on12_temporal_camera.cpp','on12_temporal_camera.hlsl','on12_temporal_camera_fixture.cpp',
         'on12_temporal_camera_pass.h','on12_temporal_camera_pass.cpp','on12_jitter_program.h','on12_jitter_program_fixture.cpp',
         'on12_native_temporal.h','on12_native_temporal.cpp','on12_native_temporal_fixture.cpp','on12_scene_projection.h','water_on12_identity.h')
ARTIFACTS=('on12_temporal_fixture.exe','on12_temporal_gpu.exe','on12_temporal_vs.bin',
           'on12_temporal_ps.bin','cpu-result.private.json','on12_temporal_inputs_fixture.exe',
           'on12_temporal_camera_fixture.exe','on12_temporal_camera_vs.bin','on12_temporal_camera_ps.bin','camera-result.private.json',
           'on12_jitter_program_fixture.exe','jitter-original.private.bin','jitter-patched.private.bin','jitter-result.private.json',
           'on12_native_temporal_fixture.exe','native-jitter-result.private.json')
MANIFEST='on12-temporal-build.json'


def same_float32(a,b):
    if type(a)not in(int,float) or type(b)not in(int,float):return False
    try:return math.isfinite(a) and math.isfinite(b) and struct.pack('<f',a)==struct.pack('<f',b)
    except (OverflowError,struct.error):return False


def request_inputs(case,sequence,frames=2,*,depth_pixels=False):
    """Request bounded metadata on an initialized scene-only host, never enable TAA."""
    if type(sequence)is not int or not 1<=sequence<=0xffffffff or type(frames)is not int or not 1<=frames<=4 or type(depth_pixels)is not bool:
        raise ValueError('Use a positive sequence and one to four scene input pairs.')
    case=recover.lab(case)
    if (case/'result.private.json').exists():
        raise ValueError('The diagnostic case is already finished.')
    value=json.loads(recover._read(case/'on12-spatial-live.private.json',32768)[0])
    if (not isinstance(value,dict) or value.get('format')!='s7-on12-spatial-live-v1'
            or value.get('initialized')is not True or value.get('scene_only')is not True
            or value.get('stopped')is not False or value.get('failure')!=0):
        raise ValueError('Temporal input observation requires the initialized scene-only host.')
    command=case/'on12-temporal-inputs.private.command'
    if command.exists():
        raw,_=recover._read(command,24)
        fields=struct.unpack('<'+'I'*(len(raw)//4),raw) if len(raw)in(16,24) else ()
        shape=fields and (fields[:2]==(16,1) or fields[:2]==(24,2) and fields[4:]==(1,0))
        if not shape or not 1<=fields[3]<=4 or fields[2]>=sequence:
            raise ValueError('Use a fresh temporal input sequence.')
    payload=struct.pack('<6I',24,2,sequence,frames,1,0) if depth_pixels else struct.pack('<4I',16,1,sequence,frames)
    recover._atomic(command,payload)


def analyze_depth(case,sequence):
    """Inspect captured pixels; this never grants motion/jitter or TAA proof."""
    if type(sequence)is not int or not 1<=sequence<=0xffffffff:raise ValueError('Invalid depth sequence.')
    case=recover.lab(case)
    report=json.loads(recover._read(case/'on12-temporal-inputs.private.json',128*1024)[0])
    if (not isinstance(report,dict) or report.get('format')!='s7-on12-temporal-inputs-v1' or report.get('sequence')!=sequence
            or report.get('game_taa_enabled')is not False or not isinstance(report.get('pairs'),list)):
        raise ValueError('Invalid scene depth report.')
    info=report.get('depth_capture',{})
    if (not isinstance(info,dict) or info.get('requested')is not True or info.get('attempted')is not True
            or info.get('written')is not True or info.get('hr')!=0):raise ValueError('Depth pixels were not captured successfully.')
    pairs=[p for p in report['pairs'] if isinstance(p,dict) and p.get('frame')==info.get('frame')]
    if len(pairs)!=1:raise ValueError('Depth readback has no unique same-frame scene/UI pair.')
    physical=pairs[0]['composite']['physical']
    raw,pin=recover._read(case/f'on12-temporal-depth-{sequence}.private.bin',40+4096*4096*4)
    if len(raw)<40:raise ValueError('Truncated depth header.')
    magic,version,w,h,fmt,frame,scale,reserved=struct.unpack_from('<8s4IQfI',raw)
    source=physical['textures'][0]
    if (magic!=b'S7D32V1\0' or version!=1 or fmt!=114 or reserved or not 1<=w<=4096 or not 1<=h<=4096
            or frame!=info.get('frame') or len(raw)!=40+w*h*4 or info.get('width')!=w or info.get('height')!=h
            or physical.get('matrix_hr')!=0 or physical.get('viewport')!=[0,0,w,h]
            or source.get('hr')!=0 or source.get('format')!=114 or source.get('width')!=w or source.get('height')!=h
            or not math.isfinite(scale) or scale<=0 or not same_float32(scale,info.get('candidate_scale')) or not same_float32(scale,physical['candidate_c17'][3])):
        raise ValueError('Depth header, resource and same-frame camera disagree.')
    values=array('f');values.frombytes(raw[40:])
    import sys
    if sys.byteorder!='little':values.byteswap()
    finite=[z for z in values if math.isfinite(z)]
    interior=[z for z in finite if 0<z<1]
    if (len(finite)!=info.get('finite') or len(interior)!=info.get('interior') or not interior
            or not same_float32(min(finite),info.get('minimum')) or not same_float32(max(finite),info.get('maximum'))):
        raise ValueError('Captured depth pixels do not match the recorded statistics.')
    return dict(format='s7-on12-depth-analysis-v1',frame=frame,width=w,height=h,format_id=fmt,sha256=pin['sha256'],
                finite_pixels=len(finite),scene_pixels=len(interior),normalized_range=[min(interior),max(interior)],
                linear_depth_range=[min(interior)*scale,max(interior)*scale],candidate_depth_scale=scale,
                same_frame_metadata_matched=True,game_taa_enabled=False,dynamic_motion_proven=False,jitter_proven=False)


def verify_build(directory):
    directory=recover.lab(directory)
    raw,_=recover._read(directory/MANIFEST,16384)
    value=json.loads(raw.decode('utf-8-sig'))
    if (not isinstance(value,dict) or set(value)!={'format','architecture','cpu_only','parameter_bytes','build_sha256','files'}
            or value['format']!='s7-on12-temporal-build-v1' or value['architecture']not in('x86','x64')
            or value['cpu_only']is not True or value['parameter_bytes']!=64
            or not isinstance(value['files'],dict) or set(value['files'])!={*SOURCES,*ARTIFACTS}
            or value['build_sha256']!=loader.digest(ROOT/'native/on12_temporal_build.ps1')):
        raise ValueError('Unknown or stale temporal preparation manifest.')
    for name,pin in value['files'].items():
        if loader.digest(directory/name)!=pin or name in SOURCES and loader.digest(ROOT/'native'/name)!=pin:
            raise ValueError('Temporal source or artifact changed; rebuild before validation.')
    result=json.loads(recover._read(directory/'cpu-result.private.json',16384)[0].decode('utf-8-sig'))
    if (not isinstance(result,dict) or result.get('passed')is not True or result.get('cpu_only')is not True
            or result.get('gpu_device_created')is not False or result.get('abi_bytes')!=64
            or result.get('shader_compiled')is not True or result.get('sharp_edge_preserved')is not True
            or result.get('reactive_rejection')is not True):
        raise ValueError('The CPU temporal preparation was not verified.')
    camera=json.loads(recover._read(directory/'camera-result.private.json',16384)[0].decode('utf-8-sig'))
    if (not isinstance(camera,dict) or camera.get('passed')is not True or camera.get('cpu_only')is not True
            or camera.get('gpu_device_created')is not False or camera.get('camera_bytes')!=96):
        raise ValueError('The camera reprojection preparation was not verified.')
    jitter=json.loads(recover._read(directory/'jitter-result.private.json',16384)[0].decode('utf-8-sig'))
    if (not isinstance(jitter,dict) or jitter.get('passed')is not True or jitter.get('cpu_only')is not True
            or jitter.get('gpu_device_created')is not False or jitter.get('bone_palette_preserved')is not True):
        raise ValueError('The geometry jitter preparation was not verified.')
    native=json.loads(recover._read(directory/'native-jitter-result.private.json',16384)[0].decode('utf-8-sig'))
    if (not isinstance(native,dict) or native.get('passed')is not True or native.get('cpu_only')is not True
            or native.get('gpu_device_created')is not False or native.get('control_version')!=2 or native.get('control_bytes')!=40 or native.get('query_bytes')!=96):
        raise ValueError('The native jitter control preparation was not verified.')
    return directory,value,result


def gpu(build,output):
    directory,manifest,cpu=verify_build(build)
    output=recover.lab(output)
    if (output.exists() or not output.parent.is_dir() or output.is_relative_to(directory) or directory.is_relative_to(output)):
        raise ValueError('Use a fresh separate GPU-result directory below work/.')
    recover._no_games()
    output.mkdir()
    for name in ('on12_temporal_gpu.exe','on12_temporal_vs.bin','on12_temporal_ps.bin','on12_temporal_camera_vs.bin','on12_temporal_camera_ps.bin'):
        shutil.copyfile(directory/name,output/name)
        if loader.digest(output/name)!=manifest['files'][name]:
            raise RuntimeError('Temporal GPU artifact changed while copying.')
    recover._write_new(output/'source-build.private.json',json.dumps(manifest,indent=2).encode())
    recover._no_games()
    done=subprocess.run([str(output/'on12_temporal_gpu.exe'),'--gpu'],cwd=output,capture_output=True,
                        timeout=45,creationflags=subprocess.CREATE_NO_WINDOW)
    result=dict(exit_code=done.returncode,stdout=done.stdout.decode(errors='replace'),stderr=done.stderr.decode(errors='replace'),
                synthetic_only=True,cpu_reference=cpu,game_taa_enabled=False)
    recover._write_new(output/'result.private.json',json.dumps(result,indent=2).encode())
    print(json.dumps(result),flush=True)
    if done.returncode:
        raise RuntimeError('Temporal GPU conformance failed; retain the private diagnostic output.')
    parsed=json.loads(result['stdout'])
    if (parsed.get('passed')is not True or parsed.get('cpu_reference_matched')is not True
            or parsed.get('camera_reference_matched')is not True or parsed.get('synthetic_only')is not True):
        raise RuntimeError('Unexpected temporal GPU conformance result.')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path)
    parser.add_argument('--gpu',action='store_true',help='Explicit GPU validation; coordinate its slot after the KI test first.')
    parser.add_argument('--output',type=Path)
    parser.add_argument('--inputs-case',type=Path,help='Only request metadata from an already initialized scene host.')
    parser.add_argument('--depth-case',type=Path,help='Analyze one already captured R32F depth image; no GPU launch.')
    parser.add_argument('--depth-pixels',action='store_true',help='Explicit one-shot GPU depth readback; exclude this run from FPS comparisons.')
    parser.add_argument('--sequence',type=int,default=1)
    parser.add_argument('--frames',type=int,default=2)
    args=parser.parse_args()
    if args.depth_case is not None:
        if args.inputs_case is not None or args.build is not None or args.gpu or args.output is not None or args.depth_pixels:parser.error('--depth-case only analyzes existing pixels')
        print(json.dumps(analyze_depth(args.depth_case,args.sequence)))
    elif args.inputs_case is not None:
        if args.build is not None or args.gpu or args.output is not None:parser.error('--inputs-case cannot build or run GPU fixtures')
        request_inputs(args.inputs_case,args.sequence,args.frames,depth_pixels=args.depth_pixels)
        print(json.dumps(dict(requested_input_pairs=args.frames,game_taa_enabled=False)))
    elif args.depth_pixels:parser.error('--depth-pixels requires --inputs-case')
    elif args.build is None:parser.error('--build, --inputs-case or --depth-case is required')
    elif args.gpu:
        if args.output is None:parser.error('--gpu requires a fresh --output')
        gpu(args.build,args.output)
    else:
        _,manifest,cpu=verify_build(args.build)
        print(json.dumps(dict(architecture=manifest['architecture'],cpu=cpu,gpu_launched=False,game_taa_enabled=False)))


if __name__=='__main__':main()
