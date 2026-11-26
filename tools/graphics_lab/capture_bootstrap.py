"""Fixed x86 water observer/correction initializer at the original entry gate."""
import ctypes as C
from ctypes import wintypes as W
import hashlib
from pathlib import Path
import struct

from . import recover

UBI='4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f'


def digest(path):
    raw,_=recover._read(path,64*1024*1024)
    return hashlib.sha256(raw).hexdigest()


def image_slots(header,image,entry_bytes,profile):
    _,stamp,size,entry,_,expected,imports=profile
    if len(header)!=4096:
        raise RuntimeError('Incomplete original image header.')
    pe=struct.unpack_from('<I',header,0x3c)[0]
    if (header[:2]!=b'MZ' or not 64<=pe<3000 or header[pe:pe+6]!=b'PE\0\0\x4c\x01'
            or struct.unpack_from('<I',header,pe+8)[0]!=stamp
            or struct.unpack_from('<I',header,pe+0x50)[0]!=size
            or entry_bytes!=bytes.fromhex(expected)):
        raise RuntimeError('Original x86 executable/entry guards changed.')
    return [image+imports[index] for index in (0,1)]


def invoke_water(owned,dll,config,*,pow_cap=False):
    if type(pow_cap)is not bool:raise ValueError('POW cap requires explicit intent.')
    return _invoke_fixed(owned,dll,config,'water_pow_cap' if pow_cap else 'water_observe')


def _invoke_fixed(owned,dll,config,kind):
    import isolated_process
    from . import dxwrapper
    from .load_hook import worker,_payload_prefix
    protocols={'water_observe':('WePlayWaterOn12Probe.dll',1036,0,b'WePlayWaterOn12ProbeInitialize\0'),
               'water_pow_cap':('WePlayWaterOn12Probe.dll',1036,1,b'WePlayWaterOn12ProbeInitialize\0')}
    if kind not in protocols:raise ValueError('Unknown fixed laboratory initializer.')
    name,size,flags,export=protocols[kind]
    owned.owned()
    if (owned.expected!=UBI or not owned.native_initialized or owned.kernel_isolation!=7
            or Path(dll).name!=name or len(config)!=size or struct.unpack_from('<3I',config)!=(size,1,flags)):
        raise ValueError('Capture bootstrap requires its exact isolated original Ubisoft child and ABI.')
    api,k=owned.api,owned.api.k
    k.CreateRemoteThread.argtypes=[W.HANDLE,C.c_void_p,C.c_size_t,C.c_void_p,C.c_void_p,W.DWORD,C.POINTER(W.DWORD)]
    k.CreateRemoteThread.restype=W.HANDLE
    k.VirtualFreeEx.argtypes=[W.HANDLE,C.c_void_p,C.c_size_t,W.DWORD];k.VirtualFreeEx.restype=W.BOOL
    def read(address,size):
        data,count=C.create_string_buffer(size),C.c_size_t()
        api.check(k.ReadProcessMemory(owned._handle,address,data,size,C.byref(count)))
        if count.value!=size:raise RuntimeError('Incomplete owned capture-initializer read.')
        return data.raw
    def write(address,raw):
        owned.owned();data,count=C.create_string_buffer(raw),C.c_size_t()
        api.check(k.WriteProcessMemory(owned._handle,address,data,len(raw),C.byref(count)))
        if count.value!=len(raw):raise RuntimeError('Incomplete owned capture-initializer write.')
    peb,returned=C.c_size_t(),W.ULONG()
    if api.nt.NtQueryInformationProcess(owned._handle,26,C.byref(peb),C.sizeof(peb),C.byref(returned)) or not peb.value:
        raise RuntimeError('Capture initializer requires the original x86 process.')
    image=struct.unpack('<I',read(peb.value+8,4))[0]
    profile=isolated_process.PROFILES[owned.expected]
    slots=image_slots(read(image,4096),image,read(image+profile[3],5),profile)
    pointers=[struct.unpack('<I',read(slot,4))[0] for slot in slots]
    if any(not 0x10000<=pointer<0xffff0000 for pointer in pointers):
        raise RuntimeError('Original kernel import guard changed.')
    renderer=owned.executable.with_name('d3d9.dll')
    if digest(renderer)!=dxwrapper.STUB_SHA256 or digest(owned.executable.with_name('dxwrapper.dll'))!=dxwrapper.DLL_SHA256:
        raise ValueError('Capture initializer requires both exact pinned dxwrapper files.')
    payload,renderer_offset,path_offset,export_offset,config_offset=_payload_prefix(renderer,dll,export)
    payload.extend(config)
    if len(payload)>8192:raise ValueError('Capture initializer data exceeds its fixed allocation.')
    data_base=code_base=thread=None;finished=False
    try:
        data_base=api.check(k.VirtualAllocEx(owned._handle,None,8192,0x3000,4))
        code_base=api.check(k.VirtualAllocEx(owned._handle,None,4096,0x3000,4))
        if data_base>=0xffffe000 or code_base>=0xfffff000:raise RuntimeError('Capture worker exceeds x86 address range.')
        write(data_base,bytes(payload))
        code=worker(code_base,data_base,*slots,path_offset,export_offset,config_offset,renderer_offset)
        write(code_base,code)
        previous=W.DWORD();api.check(k.VirtualProtectEx(owned._handle,code_base,4096,0x20,C.byref(previous)))
        api.check(k.FlushInstructionCache(owned._handle,code_base,len(code)))
        if previous.value!=4 or [struct.unpack('<I',read(slot,4))[0] for slot in slots]!=pointers:
            raise RuntimeError('Capture worker protection or original imports changed.')
        owned.owned();thread=api.check(k.CreateRemoteThread(owned._handle,None,0,code_base,None,0,None))
        if k.WaitForSingleObject(thread,8000)!=0:raise RuntimeError('Capture initializer exceeded its eight-second bound.')
        finished=True;stage,result=struct.unpack('<2I',read(data_base,8))
        if stage!=3 or result!=1:raise RuntimeError(f'Capture initializer refused admission (stage {stage}, result {result}).')
        return dict(succeeded=True,original_entry_guarded=True,original_imports_guarded=True,
                    worker_code_rx=True,kernel_isolation=7,config_bytes=size,captured_frame=False)
    finally:
        if thread:k.CloseHandle(thread)
        # A running timed-out worker retains its code/data until the original
        # starter terminates this owned child. Never free executing memory.
        if finished or thread is None:
            for address in (code_base,data_base):
                if address:k.VirtualFreeEx(owned._handle,address,0,0x8000)
