"""Fixed Windows API helper operations. No shell, PowerShell or policy changes."""
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess

import compatibility

SYSTEM_FILES=('WePlay.WindowsHelper.exe','WePlay.WindowsHelper.dll',
              'WePlay.WindowsHelper.deps.json','WePlay.WindowsHelper.runtimeconfig.json')

def helper_directory(root=None):
    if root is None:
        module=Path(__file__).resolve()
        root=module.parents[2] if module.parent.name=='installer' else module.parent.parent
    return compatibility.plain(Path(root)/'Runtime/dotnet')

def command(action,*arguments,root=None,timeout=30):
    if action not in ('interfaces','processes','lan-firewall','internet-firewall','inspect-firewall','adapter-cleanup'):
        raise ValueError('helper_invalid')
    executable=compatibility.plain(helper_directory(root)/'WePlay.WindowsHelper.exe')
    environment={key:value for key,value in os.environ.items()
                 if not key.upper().startswith(('DOTNET_','CORECLR_','COR_','COMPLUS_'))}
    environment['PATH']=str(Path(os.environ.get('SystemRoot',r'C:\Windows'))/'System32')
    result=subprocess.run([str(executable),action,*map(str,arguments)],capture_output=True,
        timeout=timeout,env=environment,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    if result.returncode or len(result.stdout)>1024*1024:
        raise ValueError('helper_failed')
    return result.stdout

def process_names():
    """Read process names directly before installation, without loading an installed helper."""
    if os.name!='nt':raise ValueError('Process inspection requires Windows')
    class Entry(ctypes.Structure):
        _fields_=[('size',wintypes.DWORD),('usage',wintypes.DWORD),('pid',wintypes.DWORD),
            ('heap',ctypes.c_size_t),('module',wintypes.DWORD),('threads',wintypes.DWORD),
            ('parent',wintypes.DWORD),('priority',wintypes.LONG),('flags',wintypes.DWORD),
            ('name',wintypes.WCHAR*260)]
    api=ctypes.WinDLL('kernel32',use_last_error=True)
    api.CreateToolhelp32Snapshot.argtypes=[wintypes.DWORD,wintypes.DWORD];api.CreateToolhelp32Snapshot.restype=wintypes.HANDLE
    api.Process32FirstW.argtypes=[wintypes.HANDLE,ctypes.POINTER(Entry)];api.Process32FirstW.restype=wintypes.BOOL
    api.Process32NextW.argtypes=[wintypes.HANDLE,ctypes.POINTER(Entry)];api.Process32NextW.restype=wintypes.BOOL
    api.CloseHandle.argtypes=[wintypes.HANDLE];api.CloseHandle.restype=wintypes.BOOL
    handle=api.CreateToolhelp32Snapshot(2,0)
    if handle in (None,ctypes.c_void_p(-1).value):raise ValueError('Process inspection failed')
    try:
        entry=Entry();entry.size=ctypes.sizeof(entry);found=[]
        present=api.Process32FirstW(handle,ctypes.byref(entry))
        if not present:raise ValueError('Process inspection failed')
        while present:
            found.append(entry.name.casefold())
            if len(found)>32768:raise ValueError('Process inspection overflow')
            present=api.Process32NextW(handle,ctypes.byref(entry))
        if ctypes.get_last_error()!=18:raise ValueError('Process inspection failed')
        return found
    finally:api.CloseHandle(handle)

def process_paths():
    rows=json.loads(command('processes').decode('utf-8-sig'))
    if not isinstance(rows,list) or len(rows)>4096:raise ValueError('managed_copy_busy')
    paths=[]
    for row in rows:
        if not isinstance(row,dict) or set(row)!={'pid','name','path','s7_service'}:
            raise ValueError('managed_copy_busy')
        path=row['path']
        if not isinstance(path,str) or not Path(path).is_absolute():raise ValueError('managed_copy_busy')
        paths.append(path)
    return paths

def lan_firewall(root,*,remove=False):
    root=compatibility.plain(root)
    return command('lan-firewall',root,'remove' if remove else 'apply',timeout=180)

def internet_firewall(root,key,interface=None):
    args=(root,key,'remove') if interface is None else (root,key,'apply',interface.address,interface.name)
    return command('internet-firewall',*args,root=root)

def remove_adapter(root,name,address):
    return command('adapter-cleanup',root,name,address,root=root)
