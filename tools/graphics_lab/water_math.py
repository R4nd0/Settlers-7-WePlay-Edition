"""Source-bound authored POW/product GPU reproduction, separate from the game."""
import argparse
import json
from pathlib import Path
import subprocess
import os
from . import dxwrapper,recover


def validate(mode,value):
    if mode not in ('dxwrapper-d3d9','dxwrapper-d3d9on12'):return False
    if (not isinstance(value,dict) or set(value)!={'synthetic_only','on12_query_hr','cases','draws_and_readbacks_passed'}
            or value['synthetic_only']is not True or value['draws_and_readbacks_passed']is not True
            or type(value['on12_query_hr'])is not int
            or value['on12_query_hr']!=(0 if mode=='dxwrapper-d3d9on12' else -2147467262)
            or not isinstance(value['cases'],list) or len(value['cases'])!=4):return False
    for i,case in enumerate(value['cases']):
        if (not isinstance(case,dict) or set(case)!={'guarded','base','rgba','bits'}
                or case['guarded']is not (i>=2) or type(case['base'])not in(int,float) or case['base']!=i%2
                or not isinstance(case['bits'],list) or len(case['bits'])!=4
                or any(type(b)is not int or not 0<=b<=0xffffffff for b in case['bits'])
                or not isinstance(case['rgba'],list) or len(case['rgba'])!=4):return False
        if mode=='dxwrapper-d3d9on12' and i==0:
            if case['rgba']!=[None,None,None,1] or case['bits'][3]!=0x3f800000:return False
            if any(b&0x7f800000!=0x7f800000 or not b&0x7fffff for b in case['bits'][:3]):return False
        else:
            expected=2.25 if i%2 else .25;bits=0x40100000 if i%2 else 0x3e800000
            if case['rgba']!=[expected,expected,expected,1] or case['bits']!=[bits,bits,bits,0x3f800000]:return False
    return True


def run(build,output,package):
    from . import game
    build,output=recover.lab(build),recover.lab(output)
    if output.exists() or not output.parent.is_dir():raise ValueError('Use a fresh private output directory.')
    raw,_=recover._read(build/'math-build.json',16384);manifest=json.loads(raw.decode('utf-8-sig'))
    paths={'source_sha256':game.ROOT/'native/water_on12_math_gpu.cpp',
           'build_sha256':game.ROOT/'native/water_on12_math_gpu_build.ps1',
           'binary_sha256':build/'water_on12_math_gpu.exe'}
    if not isinstance(manifest,dict) or set(manifest)!={'format',*paths} or manifest['format']!='s7-water-on12-math-build-v1':
        raise ValueError('Unknown water math build.')
    for key,path in paths.items():
        if game.digest(path)!=manifest[key]:raise ValueError('Water math source or executable changed; rebuild.')
    program,_=recover._read(paths['binary_sha256'],16*1024*1024)
    payloads={mode:dxwrapper.artifacts(package,mode) for mode in dxwrapper.MODES[1:]}
    recover._no_games();game.memory_guard.observe_host_memory();output.mkdir()
    results=[]
    for mode,payload in payloads.items():
        recover._no_games();game.memory_guard.observe_host_memory()
        case=output/mode;case.mkdir();exe=case/'water_on12_math_gpu.exe'
        recover._write_new(exe,program)
        for name,data in payload.items():
            if name=='dxwrapper.ini':data=data.replace(b'Settlers7R.exe',exe.name.encode())
            recover._write_new(case/name,data)
        result=subprocess.run([str(exe)],cwd=case,env=game.clean_environment(os.environ),capture_output=True,timeout=30)
        record=dict(mode=mode,exit_code=result.returncode,stdout=result.stdout.decode(errors='replace'),stderr=result.stderr.decode(errors='replace'),synthetic_only=True,passed=False)
        try:
            record['device']=json.loads(record['stdout']);record['passed']=result.returncode==0 and validate(mode,record['device'])
        except (ValueError,TypeError):pass
        recover._write_new(case/'result.private.json',json.dumps(record,indent=2).encode());results.append(record)
        print(json.dumps(record),flush=True)
    recover._write_new(output/'results.private.json',json.dumps(results,indent=2).encode())
    if not all(r['passed'] for r in results):raise RuntimeError('Water math GPU reproduction failed; retain private results.')
    return results


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for flag in ('build','output','package'):p.add_argument('--'+flag,type=Path,required=True)
    a=p.parse_args();run(a.build,a.output,a.package)


if __name__=='__main__':main()
