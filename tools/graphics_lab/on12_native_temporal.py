"""Explicit synthetic validation of native On12 temporal controls."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

from . import on12_runtime,dxwrapper,recover


def gpu(build,package,output):
    directory,manifest,_=on12_runtime.verify_build(build)
    if manifest['variant']!='temporal-worker':raise ValueError('Use the exact native temporal runtime variant.')
    output=recover.lab(output)
    if (output.exists() or not output.parent.is_dir() or output.is_relative_to(directory) or directory.is_relative_to(output)):
        raise ValueError('Use a fresh separate native temporal result directory below work/.')
    on12_runtime.validate_runtime()
    recover._no_games()
    payload=dxwrapper.artifacts(package,'dxwrapper-d3d9on12')
    output.mkdir()
    for name,raw in payload.items():
        if name=='dxwrapper.ini':raw=raw.replace(b'IncludeProcess=Settlers7R.exe',b'IncludeProcess=on12_runtime_gpu.exe')
        recover._write_new(output/name,raw)
    for name in on12_runtime.build.FILES:
        shutil.copyfile(directory/name,output/name)
        if on12_runtime.build.digest(output/name)!=manifest['files'][name]:raise RuntimeError('Native temporal payload changed during copying.')
    recover._write_new(output/'source-runtime.private.json',json.dumps(manifest,indent=2).encode())
    on12_runtime.verify_build(directory)
    recover._no_games()
    done=subprocess.run([str(output/'on12_runtime_gpu.exe'),manifest['files']['d3d9on12.dll']],cwd=output,
                        capture_output=True,timeout=45,creationflags=subprocess.CREATE_NO_WINDOW)
    result=dict(exit_code=done.returncode,stdout=done.stdout.decode(errors='replace'),stderr=done.stderr.decode(errors='replace'),
                synthetic_only=True,game_taa_enabled=False,runtime_sha256=manifest['files']['d3d9on12.dll'])
    recover._write_new(output/'result.private.json',json.dumps(result,indent=2).encode())
    print(json.dumps(result),flush=True)
    if done.returncode:raise RuntimeError('Native temporal driver conformance failed.')
    parsed=json.loads(result['stdout'])
    if any(parsed.get(name)is not True for name in ('passed','synthetic_only','native_jitter','reference_matched','ui_unchanged','shadow_unchanged','app_constants_unchanged')):
        raise RuntimeError('Unexpected native temporal driver result.')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--package',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    gpu(args.build,args.package,args.output)


if __name__=='__main__':main()
