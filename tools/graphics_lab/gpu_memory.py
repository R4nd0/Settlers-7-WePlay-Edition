"""Read Windows GPU process-memory counters for one admitted owned lab case.

No input, process-memory writes, native calls, priorities or global settings.
Counter absence stays unavailable; dedicated/shared/committed are separate and
must not be added together as if committed memory were another allocation.
"""
from __future__ import annotations

import argparse
import base64
import json
from pathlib import Path
import re
import shutil
import subprocess
import time

from . import reconfigure as scope


def normalize(value,pid):
    if not isinstance(value,list) or len(value)>32:
        raise ValueError('Unexpected GPU counter collection.')
    fields=('DedicatedUsage','SharedUsage','TotalCommitted','LocalUsage','NonLocalUsage')
    rows=[];seen=set()
    for item in value:
        if not isinstance(item,dict) or set(item)!={'Name',*fields}:
            raise ValueError('Unexpected GPU memory counter schema.')
        if not isinstance(item['Name'],str) or not item['Name'].startswith(f'pid_{pid}_'):
            raise ValueError('The GPU counter belongs to another process.')
        if not re.fullmatch(r'pid_[0-9]+_luid_0x[0-9a-fA-F]+_0x[0-9a-fA-F]+_phys_[0-9]+',item['Name']):
            raise ValueError('Unexpected GPU process-counter instance.')
        if item['Name']in seen:raise ValueError('Duplicate GPU process-counter instance.')
        seen.add(item['Name'])
        if any(type(item[key])is not int or not 0<=item[key]<2**63 for key in fields):
            raise ValueError('Invalid GPU memory counter value.')
        rows.append(dict(zip(('dedicated_bytes','shared_bytes','committed_bytes','local_bytes','nonlocal_bytes'),
                             (item[key] for key in fields))))
    return dict(available=bool(rows),adapter_instances=rows,
                totals={key:sum(row[key]for row in rows) for key in rows[0]} if rows else None)


def snapshot(case,dll,output):
    files=scope._case_files(case,dll)
    output=scope._lab(output)
    if output.exists() or not output.parent.is_dir():raise ValueError('Use a fresh private GPU memory receipt.')
    api=scope.isolated_process.Windows();scope._bind(api)
    handle=api.check(api.k.OpenProcess(0x410,False,files['owned']['pid']))
    def owned():
        return scope.validate_live_state(scope._live_state(api,handle,files['owned']['pid'],files['owned']['parent']),files)
    try:
        creation=owned();pid=files['owned']['pid']
        shell=shutil.which('pwsh.exe') or shutil.which('powershell.exe')
        if not shell:raise RuntimeError('Windows GPU memory counter reader unavailable.')
        script=(f"$rows=@(Get-CimInstance -Namespace root/cimv2 -ClassName Win32_PerfRawData_GPUPerformanceCounters_GPUProcessMemory "
                f"-Filter \"Name LIKE 'pid[_]{pid}[_]%'\" -ErrorAction Stop | "
                "Select-Object Name,DedicatedUsage,SharedUsage,TotalCommitted,LocalUsage,NonLocalUsage); "
                "ConvertTo-Json -InputObject $rows -Compress")
        started=time.monotonic_ns()
        result=subprocess.run([shell,'-NoProfile','-NonInteractive','-EncodedCommand',
            base64.b64encode(script.encode('utf-16le')).decode('ascii')],capture_output=True,timeout=20,
            creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode:raise RuntimeError('GPU process memory counters unavailable.')
        if len(result.stdout)>65536:raise ValueError('GPU counter response exceeds bound.')
        counters=normalize(json.loads(result.stdout.decode('utf-8-sig')),pid)
        if owned()!=creation:raise RuntimeError('The original process changed during GPU memory sampling.')
        value=dict(format='s7-owned-gpu-memory-v1',read_only=True,identity=files['owned'],
                   source='Windows GPU Process Memory performance counters',
                   started_monotonic_ns=started,finished_monotonic_ns=time.monotonic_ns(),**counters)
        with output.open('x',encoding='utf-8')as stream:json.dump(value,stream,indent=2)
        return value
    finally:api.k.CloseHandle(handle)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('case','dll','output'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();value=snapshot(args.case,args.dll,args.output)
    print(json.dumps(dict(available=value['available'],totals=value['totals'])))


if __name__=='__main__':main()
