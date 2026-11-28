"""Validate the explicit camera/depth/temporal GPU sequence; never launch a game."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

from . import on12_temporal as temporal,recover

ROOT=temporal.ROOT
SOURCES=('on12_temporal_pipeline.h','on12_temporal_pipeline.cpp','on12_temporal_pipeline_gpu.cpp')
EXECUTABLE='on12_temporal_pipeline_gpu.exe'
MANIFEST='on12-temporal-pipeline-build.json'


def verify_build(directory):
    directory=recover.lab(directory)
    raw,_=recover._read(directory/MANIFEST,32768)
    value=json.loads(raw.decode('utf-8-sig'))
    if (not isinstance(value,dict) or set(value)!={'format','architecture','build_sha256','temporal_build_sha256','files','gpu_executed'}
            or value['format']!='s7-on12-temporal-pipeline-build-v1' or value['architecture']not in('x86','x64') or value['gpu_executed']is not False
            or value['build_sha256']!=temporal.loader.digest(ROOT/'native/on12_temporal_pipeline_build.ps1')
            or not isinstance(value['files'],dict) or set(value['files'])!={*temporal.SOURCES,*temporal.ARTIFACTS,*SOURCES,EXECUTABLE}
            or not isinstance(value['temporal_build_sha256'],str) or not re.fullmatch('[0-9a-f]{64}',value['temporal_build_sha256'])):
        raise ValueError('Unknown or stale temporal pipeline preparation.')
    _,base,_=temporal.verify_build(directory)
    if base['architecture']!=value['architecture'] or temporal.loader.digest(directory/temporal.MANIFEST)!=value['temporal_build_sha256']:
        raise ValueError('Temporal pipeline base identity changed.')
    if any(value['files'][name]!=pin for name,pin in base['files'].items()):raise ValueError('Pipeline and temporal base disagree.')
    for name in (*SOURCES,EXECUTABLE):
        pin=value['files'][name]
        if temporal.loader.digest(directory/name)!=pin or name in SOURCES and temporal.loader.digest(ROOT/'native'/name)!=pin:
            raise ValueError('Temporal pipeline source or artifact changed.')
    return directory,value


def gpu(build,output):
    directory,manifest=verify_build(build)
    output=recover.lab(output)
    if output.exists() or not output.parent.is_dir() or output.is_relative_to(directory) or directory.is_relative_to(output):
        raise ValueError('Use a fresh separate pipeline GPU result below work/.')
    recover._no_games();output.mkdir()
    for name in (EXECUTABLE,'on12_temporal_vs.bin','on12_temporal_ps.bin','on12_temporal_camera_vs.bin','on12_temporal_camera_ps.bin'):
        shutil.copyfile(directory/name,output/name)
        if temporal.loader.digest(output/name)!=manifest['files'][name]:raise RuntimeError('Pipeline artifact changed during copying.')
    verify_build(directory);recover._no_games()
    done=subprocess.run([str(output/EXECUTABLE),'--gpu'],cwd=output,capture_output=True,timeout=45,creationflags=subprocess.CREATE_NO_WINDOW)
    result=dict(exit_code=done.returncode,stdout=done.stdout.decode(errors='replace'),stderr=done.stderr.decode(errors='replace'),synthetic_only=True,game_taa_enabled=False)
    recover._write_new(output/'result.private.json',json.dumps(result,indent=2).encode());print(json.dumps(result),flush=True)
    if done.returncode:raise RuntimeError('Temporal pipeline GPU conformance failed.')
    lines=[json.loads(line) for line in result['stdout'].splitlines() if line.strip()]
    if len(lines)!=2 or any(lines[0].get(key)is not True for key in ('passed','synthetic_only','temporal_graph_matched')) or any(lines[1].get(key)is not True for key in ('passed','synthetic_only','pipeline_reference_matched','geometry_motion_matched','unclassified_rejected','reactive_rejected')):
        raise RuntimeError('Unexpected temporal pipeline GPU result.')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();gpu(args.build,args.output)


if __name__=='__main__':main()
