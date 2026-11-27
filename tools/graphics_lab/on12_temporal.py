"""Source-bound CPU preparation and explicitly separate temporal GPU validation."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

from . import capture_bootstrap as loader, recover

ROOT=Path(__file__).resolve().parents[2]
SOURCES=('on12_temporal.h','on12_temporal.cpp','on12_temporal.hlsl','on12_temporal_pass.h',
         'on12_temporal_pass.cpp','on12_temporal_fixture.cpp','on12_temporal_gpu.cpp')
ARTIFACTS=('on12_temporal_fixture.exe','on12_temporal_gpu.exe','on12_temporal_vs.bin',
           'on12_temporal_ps.bin','cpu-result.private.json')
MANIFEST='on12-temporal-build.json'


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
    return directory,value,result


def gpu(build,output):
    directory,manifest,cpu=verify_build(build)
    output=recover.lab(output)
    if (output.exists() or not output.parent.is_dir() or output.is_relative_to(directory) or directory.is_relative_to(output)):
        raise ValueError('Use a fresh separate GPU-result directory below work/.')
    recover._no_games()
    output.mkdir()
    for name in ('on12_temporal_gpu.exe','on12_temporal_vs.bin','on12_temporal_ps.bin'):
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
    if parsed.get('passed')is not True or parsed.get('cpu_reference_matched')is not True or parsed.get('synthetic_only')is not True:
        raise RuntimeError('Unexpected temporal GPU conformance result.')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--gpu',action='store_true',help='Explicit GPU validation; coordinate its slot after the KI test first.')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    if args.gpu:
        if args.output is None:parser.error('--gpu requires a fresh --output')
        gpu(args.build,args.output)
    else:
        _,manifest,cpu=verify_build(args.build)
        print(json.dumps(dict(architecture=manifest['architecture'],cpu=cpu,gpu_launched=False,game_taa_enabled=False)))


if __name__=='__main__':main()
