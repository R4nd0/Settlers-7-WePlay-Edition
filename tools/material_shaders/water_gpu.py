"""Bounded owned authored-water GPU proof, never a game or quality verdict."""
from __future__ import annotations

import argparse
from contextlib import ExitStack
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time

from . import inventory, staging, water_staging

ROOT=Path(__file__).resolve().parents[2]
DXVK='265888c31ca78dffa290c39cb7e50bfb02762590e41927906e46fb32f01497fa'
SOURCES={'water_gpu_fixture.cpp','water_shader_effects.cpp','water_shader_effects.h','water_gpu_build.ps1'}


def digest(path):
    import hashlib
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def lab(path):
    path=staging.plain(path);work=staging.plain(ROOT/'work')
    if path==work or not path.is_relative_to(work):raise ValueError('Use explicit owned work paths')
    return path


def verify_build(build):
    build=lab(build)
    raw=(build/'build.private.json').read_bytes()
    if len(raw)>8192:raise ValueError('Oversized GPU build manifest')
    value=json.loads(raw)
    if (set(value)!={'format','sources','exe_sha256','executed'} or value['format']!='s7-water-gpu-build-v1'
            or set(value['sources'])!=SOURCES or value['executed'] is not False):
        raise ValueError('Unexpected standalone GPU build schema')
    for name,pin in value['sources'].items():
        if digest(ROOT/'native'/name)!=pin or digest(build/name)!=pin:
            raise ValueError('GPU build/source checkpoint changed')
    exe=build/'WaterGpuFixture.exe'
    if digest(exe)!=value['exe_sha256']:raise ValueError('Stale GPU executable')
    data=exe.read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0]
    if data[:2]!=b'MZ' or data[pe:pe+6]!=b'PE\0\0\x4c\x01':raise ValueError('Expected x86 authored fixture')
    return exe,value


def run(build,renderer,program,output):
    if os.name!='nt':raise ValueError('Windows required')
    exe,manifest=verify_build(build);renderer=lab(renderer);program=lab(program);output=lab(output)
    if output.exists() or not output.parent.is_dir() or output.is_relative_to(exe.parent):
        raise ValueError('Use a fresh separate private GPU directory')
    if digest(renderer)!=DXVK or program.stat().st_size!=3840 or digest(program)!=water_staging.PROGRAM_SHA256:
        raise ValueError('Pinned DXVK and genuine recipient-generated water PS required')
    sys.path[:0]=[str(ROOT/'tools/installer'),str(ROOT/'tools/d3d11')]
    import isolated_process,memory_guard
    if any(p.exe.casefold()=='settlers7r.exe' for p in isolated_process.Windows().processes()):
        raise RuntimeError('No concurrent game is permitted for this GPU fixture')
    memory_guard.observe_host_memory()
    from tools.graphics_lab.crash_watch import Native
    from tools.graphics_lab.load_hook import file_lease
    native=Native();output.mkdir()
    for source,name in ((renderer,'d3d9.dll'),(program,'genuine-water.ps.bin')):
        with source.open('rb') as reader,(output/name).open('xb') as writer:shutil.copyfileobj(reader,writer)
    if digest(output/'d3d9.dll')!=DXVK or digest(output/'genuine-water.ps.bin')!=water_staging.PROGRAM_SHA256:
        raise ValueError('Private GPU input copy changed')
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('DXVK_','D3D9_','VK_','RESHADE_','S7_','WEPLAY_'))}
    env.update(DXVK_LOG_PATH=str(output),VK_LOADER_LAYERS_DISABLE='~implicit~',
               DXVK_CONFIG='d3d9.floatEmulation = Strict')
    result=dict(format='s7-owned-water-gpu-run-v1',build=manifest,renderer_sha256=DXVK,
                production_ps_sha256=water_staging.PROGRAM_SHA256,game_launched=False,
                visible_game_quality_qualified=False,timeout_seconds=45,private_limit_bytes=1024**3)
    class Memory(C.Structure):
        _fields_=[('cb',W.DWORD),('faults',W.DWORD)]+[(n,C.c_size_t) for n in (
            'peak_working','working','peak_paged','paged','peak_nonpaged','nonpaged','pagefile','peak_pagefile','private')]
    psapi=C.WinDLL('psapi',use_last_error=True);psapi.GetProcessMemoryInfo.argtypes=[W.HANDLE,C.POINTER(Memory),W.DWORD];psapi.GetProcessMemoryInfo.restype=W.BOOL
    proc=None;identity=None
    with ExitStack() as leases:
        for path in (exe,output/'d3d9.dll',output/'genuine-water.ps.bin'):
            leases.enter_context(file_lease(path))
        verify_build(exe.parent)
        with (output/'stdout.private.txt').open('xb') as stdout,(output/'stderr.private.txt').open('xb') as stderr:
            proc=subprocess.Popen([str(exe),str(output)],cwd=output,env=env,stdout=stdout,stderr=stderr,creationflags=0x08000000,shell=False)
            try:
                if proc.poll() is None:
                    try:identity=native.identity(proc.pid,int(proc._handle))
                    except (OSError,RuntimeError):
                        if proc.poll() is None:raise
                if identity is not None and (identity['parent']!=os.getpid() or identity['path']!=str(exe) or identity['sha256']!=manifest['exe_sha256']):
                    raise RuntimeError('Owned authored GPU identity changed')
                deadline=time.monotonic()+45;peak=0
                while proc.poll() is None:
                    memory=Memory();memory.cb=C.sizeof(memory)
                    if psapi.GetProcessMemoryInfo(int(proc._handle),C.byref(memory),C.sizeof(memory)):
                        peak=max(peak,memory.private)
                        if peak>1024**3:raise RuntimeError('Authored GPU private-memory budget exceeded')
                    elif proc.poll() is None:raise RuntimeError('Authored GPU memory observation failed')
                    if time.monotonic()>=deadline:raise RuntimeError('Authored GPU timeout45s')
                    time.sleep(.05)
                result.update(exit_code=proc.wait(timeout=5),peak_private_bytes=peak,identity=identity)
            except BaseException as error:
                result['error_type']=type(error).__name__
                if proc.poll() is None:
                    current=native.identity(proc.pid,int(proc._handle))
                    if identity is not None and current!=identity:raise RuntimeError('Refuse stopping changed GPU identity') from error
                    if current['parent']!=os.getpid() or current['path']!=str(exe) or current['sha256']!=manifest['exe_sha256']:
                        raise RuntimeError('Refuse stopping unowned GPU process') from error
                    proc.terminate();proc.wait(timeout=10);result['owned_fixture_stopped']=True
                (output/'run.private.json').write_text(json.dumps(result,indent=2))
                raise
    (output/'run.private.json').write_text(json.dumps(result,indent=2))
    if result['exit_code']!=0:raise RuntimeError(f'Water GPU fixture failed; retained logs in {output}')
    receipt=json.loads((output/'receipt.private.json').read_text())
    if (receipt.get('passed') is not True or receipt.get('original_s7_programs_rendered') is not False
            or receipt.get('production_ps_driver_created') is not True or receipt.get('production_ps_rendered') is not False
            or receipt.get('off_repeat_rgb_absolute_difference')!=0 or receipt.get('failed_draws')!=1):
        raise RuntimeError('Incomplete authored GPU evidence')
    return dict(**result,fixture=receipt)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('build','renderer','program','output'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    print(json.dumps(run(args.build,args.renderer,args.program,args.output),indent=2))


if __name__=='__main__':main()
