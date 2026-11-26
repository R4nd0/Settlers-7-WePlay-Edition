"""Load only the source-bound supersampling DLL into our pre-entry S7 child.

The existing portable starter owns the child and restores its original entry
bytes before this callback. Kernel calls use that verified x86 image's actual
import slots; no address from the 64-bit controller is used in the child.
"""
from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
EXPORT = b'WePlaySupersamplingInitialize\0'
DLL_NAME = 'WePlaySupersamplingLab.dll'
from tools.supersampling.hook_probe import (SOURCES as HOOK_SOURCES, NATIVE20_SOURCES,EFFECTS_V1_SOURCES,
                                          BINARIES, NATIVE20_BINARIES, BUILD_ABI,MODERN_SOURCES,BUILD_ABI_V2,WATER_SOURCES,BUILD_ABI_WATER,MACRO_SOURCES,SHADOW_SOURCES,AMBIENT_SOURCES)
SOURCES = set(HOOK_SOURCES)
OPERATIONS = {
    'capture': (b'WePlayNativeGraphicsRequestCapture\0', 12),
    'configure': (b'WePlayNativeGraphicsConfigureObservation\0', 952),
    'effects': (b'WePlayNativeGraphicsConfigureEffects\0', 4132),
    'comparison': (b'WePlayNativeGraphicsConfigureComparison\0', 12),
    'water': (b'WePlayNativeGraphicsConfigureWater\0',1076),
    'shadow': (b'WePlayNativeGraphicsConfigureShadow\0',12),
    'ambient': (b'WePlayNativeGraphicsConfigureAmbient\0',12),
    'ambient_v2': (b'WePlayNativeGraphicsConfigureAmbient\0',12),
}


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify_build(dll):
    from .game import lab_path
    dll = lab_path(dll)
    if dll.name != DLL_NAME or not dll.is_file():
        raise ValueError('Use the named standalone supersampling laboratory DLL.')
    manifest = dll.parent / 'supersampling-hook-build.json'
    with manifest.open('rb') as stream:
        raw = stream.read(8193)
    if len(raw) > 8192:
        raise ValueError('Hook build manifest exceeds its bound.')
    value = json.loads(raw)
    current = isinstance(value,dict) and value.get('format') == 's7-native-graphics-build-v2'
    profile_b=current and value.get('source_set')=='effects-v8'
    ambient=current and value.get('source_set')in('effects-v7','effects-v8')
    shadow=current and value.get('source_set')in('effects-v6','effects-v7','effects-v8')
    macro=current and value.get('source_set')in('effects-v5','effects-v6','effects-v7','effects-v8')
    water=current and value.get('source_set')in('effects-v4','effects-v5','effects-v6','effects-v7','effects-v8')
    modern=current and value.get('source_set')in('effects-v3','effects-v4','effects-v5','effects-v6','effects-v7','effects-v8')
    comparison=current and value.get('source_set')in('effects-v2','effects-v3','effects-v4','effects-v5','effects-v6','effects-v7','effects-v8')
    sources = (set(AMBIENT_SOURCES) if ambient else set(SHADOW_SOURCES) if shadow else set(MACRO_SOURCES) if macro else set(WATER_SOURCES) if water else set(MODERN_SOURCES) if modern else SOURCES if comparison else set(EFFECTS_V1_SOURCES)) if current else set(NATIVE20_SOURCES)
    expected = sources | set(BINARIES if current else NATIVE20_BINARIES)
    keys = {'format','build_sha256','files'} | ({'source_set','compiled_capabilities','abi'} if current else set())
    if comparison:keys.add('diagnostic_abi')
    if (not isinstance(value, dict) or set(value) != keys
            or value['format'] not in ('s7-supersampling-hook-build-v1','s7-native-graphics-build-v2')
            or not isinstance(value['files'], dict) or set(value['files']) != expected
            or value['build_sha256'] != digest(ROOT / 'native/supersampling_hook_build.ps1')):
        raise ValueError('Stale or unexpected hook build manifest.')
    if current and (value['source_set']not in ('effects-v1','effects-v2','effects-v3','effects-v4','effects-v5','effects-v6','effects-v7','effects-v8') or value['compiled_capabilities']!=15
                    or value['abi']!=(BUILD_ABI_WATER if water else BUILD_ABI_V2 if modern else BUILD_ABI)):
        raise ValueError('Unexpected compiled graphics ABI or capabilities.')
    if comparison and value['diagnostic_abi']!={'comparison':12,**({'shadow':12} if shadow else {}),**({'ambient':12} if ambient else {}),**({'ambient_v2':12} if profile_b else {})}:
        raise ValueError('Unexpected diagnostic comparison ABI.')
    for name, pin in value['files'].items():
        path = ROOT / 'native' / name if name in sources else dll.parent / name
        if pin != digest(path):
            raise ValueError('Hook build/source fingerprint changed.')
    return dll, value


def configuration(render, output, report, *, observer_catalog=None, observer_sha256=None,
                  observer_report=None, material_flags=0,shadow_filter=False,ambient_occlusion=False):
    from .game import lab_path
    report = lab_path(report)
    if not report.parent.is_dir() or report.exists():
        raise ValueError('Use a new private hook report in the existing case directory.')
    values = (*render, *output)
    if (any(type(v) is not int or v <= 0 for v in values)
            or render[0] > 3840 or render[1] > 2160
            or output[0] > render[0] or output[1] > render[1]
            or output[0] < 800 or output[1] < 600
            or render[0] * output[1] != render[1] * output[0]):
        raise ValueError('Use bounded render/output dimensions with the same aspect ratio.')
    name = str(report).encode('utf-16le')
    if len(name) > 1022 or '\0' in str(report):
        raise ValueError('Hook report path exceeds the fixed UTF16 ABI.')
    if type(material_flags)is not int or material_flags<0 or material_flags&~7:
        raise ValueError('Invalid explicit material intent.')
    if type(shadow_filter)is not bool or (shadow_filter and observer_catalog is None):
        raise ValueError('Shadow filter requires explicit OBSERVE laboratory intent.')
    if (type(ambient_occlusion)is not bool or (ambient_occlusion and
            (observer_catalog is None or shadow_filter))):
        raise ValueError('Ambient occlusion requires exclusive explicit OBSERVE laboratory intent.')
    version = 2 if observer_catalog is not None or render == output or material_flags else 1
    base = struct.pack('<6I', 3132 if version == 2 else 1048, version, *values) + name.ljust(1024, b'\0')
    if version == 1:
        if observer_sha256 is not None or observer_report is not None:
            raise ValueError('Observer identity needs an explicit catalog.')
        return base
    if observer_catalog is None:
        if observer_sha256 is not None or observer_report is not None:
            raise ValueError('Observer identity needs an explicit catalog.')
        return base + struct.pack('<I',material_flags<<1) + bytes(2080)
    catalog = lab_path(observer_catalog)
    destination = lab_path(observer_report or report.with_name('material-observer.private.json'))
    if (not catalog.is_file() or catalog.suffix.lower() != '.bin' or catalog.stat().st_size > 1024*1024
            or not isinstance(observer_sha256, str) or len(observer_sha256) != 64
            or any(c not in '0123456789abcdef' for c in observer_sha256)
            or digest(catalog) != observer_sha256
            or destination == report or destination.exists() or not destination.parent.is_dir()
            or destination.suffix.lower() != '.json'):
        raise ValueError('Invalid or changed private observer identity.')
    def wide(path):
        text = str(path)
        encoded = text.encode('utf-16le')
        if '\0' in text or len(encoded) > 1022:
            raise ValueError('Observer path exceeds its fixed UTF16 ABI.')
        return encoded.ljust(1024, b'\0')
    return base + struct.pack('<I', 1|(material_flags<<1)|(16 if shadow_filter else 0)|(32 if ambient_occlusion else 0)) + wide(catalog) + bytes.fromhex(observer_sha256) + wide(destination)


def worker(code_base, data_base, load_slot, export_slot, path_offset, export_offset, config_offset,
           renderer_offset):
    """Fixed x86 stdcall worker: LoadLibraryW, exact export, explicit initializer."""
    code = bytearray(b'\x9c\x60')
    jumps = []
    def number(value):
        code.extend(struct.pack('<I', value))
    def push(value):
        code.append(0x68); number(value)
    def call(slot):
        code.extend(b'\xff\x15'); number(slot)
    def branch(stage):
        code.extend(b'\x85\xc0\x0f\x84'); jumps.append((len(code), stage)); number(0)
    push(data_base + renderer_offset); call(load_slot); branch(10)
    push(data_base + path_offset); call(load_slot); branch(1)
    push(data_base + export_offset); code.append(0x50); call(export_slot); branch(2)
    push(data_base + config_offset); code.extend(b'\xff\xd0')
    code.append(0xa3); number(data_base + 4)
    code.extend(b'\xc7\x05'); number(data_base); number(3)
    code.append(0xe9); done_jump = len(code); number(0)
    endings = []
    for position, stage in jumps:
        struct.pack_into('<i', code, position, len(code) - position - 4)
        code.extend(b'\xc7\x05'); number(data_base); number(stage)
        code.append(0xe9); endings.append(len(code)); number(0)
    done = len(code)
    for position in (*endings, done_jump):
        struct.pack_into('<i', code, position, done - position - 4)
    code.extend(b'\x61\x9d\x31\xc0\xc2\x04\x00')
    if len(code) > 512 or not 0 < code_base < 0xfffff000:
        raise ValueError('Invalid fixed x86 hook worker.')
    return bytes(code)


@contextmanager
def file_lease(path):
    """Deny writes/deletes from the verification through remote LoadLibrary."""
    k = C.WinDLL('kernel32', use_last_error=True)
    k.CreateFileW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p, W.DWORD, W.DWORD, W.HANDLE]
    k.CreateFileW.restype = W.HANDLE
    k.CloseHandle.argtypes = [W.HANDLE]
    k.CloseHandle.restype = W.BOOL
    handle = k.CreateFileW(str(path), 0x80000000, 1, None, 3, 0x80, None)
    if handle == C.c_void_p(-1).value:
        raise C.WinError(C.get_last_error())
    try:
        yield
    finally:
        k.CloseHandle(handle)


def initialize(owned, dll, *, render, output, report, observer_catalog=None, observer_sha256=None,material_flags=0,shadow_filter=False,ambient_occlusion=False):
    dll, _ = verify_build(dll)
    with file_lease(dll):
        config = configuration(render, output, report, observer_catalog=observer_catalog,
                               observer_sha256=observer_sha256,material_flags=material_flags,shadow_filter=shadow_filter,ambient_occlusion=ambient_occlusion)
        receipt = _invoke_pinned(owned, dll, config, EXPORT)
        receipt['initialized'] = True
        if observer_catalog is not None:
            receipt['observer_catalog_sha256'] = observer_sha256
        return receipt


def _payload_prefix(renderer,dll,export):
    payload=bytearray(8)
    renderer_offset=len(payload);payload.extend(str(renderer).encode('utf-16le')+b'\0\0')
    path_offset=len(payload);payload.extend(str(dll).encode('utf-16le')+b'\0\0')
    export_offset=len(payload);payload.extend(export)
    while len(payload)%4:payload.append(0)
    return payload,renderer_offset,path_offset,export_offset,len(payload)


def worker_overhead(executable,dll):
    """Exact non-config bytes from the same encoder used by the owned worker."""
    from .game import lab_path
    executable,dll=lab_path(executable),lab_path(dll)
    return len(_payload_prefix(executable.with_name('d3d9.dll'),dll,OPERATIONS['effects'][0])[0])


def invoke(owned, dll, operation, payload):
    if operation not in OPERATIONS:
        raise ValueError('Unsupported native graphics operation.')
    export, size = OPERATIONS[operation]
    modern=operation=='effects' and isinstance(payload,bytes) and len(payload)==6180
    expected_size=6180 if modern else size
    if (not isinstance(payload, bytes) or len(payload) != expected_size
            or struct.unpack_from('<2I', payload) != (expected_size, 2 if modern or operation=='ambient_v2' else 1)):
        raise ValueError('Invalid fixed native graphics command ABI.')
    if operation == 'capture' and not 1 <= struct.unpack_from('<I', payload, 8)[0] <= 1000000:
        raise ValueError('Capture sequence exceeds the bounded command range.')
    if operation == 'comparison' and struct.unpack_from('<I',payload,8)[0]not in(0,1):
        raise ValueError('Comparison diagnostic accepts only an explicit Boolean.')
    if operation=='shadow' and struct.unpack_from('<I',payload,8)[0]not in(0,1):
        raise ValueError('Shadow diagnostic accepts only an explicit Boolean.')
    if operation in('ambient','ambient_v2') and struct.unpack_from('<I',payload,8)[0]not in(0,1):
        raise ValueError('Ambient diagnostic accepts only an explicit Boolean.')
    if operation=='water' and struct.unpack_from('<I',payload,16)[0]not in(0,1):raise ValueError('WaterProgram enable must be Boolean.')
    dll, build = verify_build(dll)
    if modern and build.get('abi',{}).get('effects_v2')!=6180:
        raise ValueError('This exact source-bound build does not support material V2.')
    if operation=='water' and build.get('abi',{}).get('water')!=1076:raise ValueError('This exact build does not support WaterProgram.')
    if operation=='shadow' and build.get('diagnostic_abi',{}).get('shadow')!=12:
        raise ValueError('This exact build does not support the fixed shadow diagnostic.')
    if operation in('ambient','ambient_v2') and build.get('diagnostic_abi',{}).get(operation)!=12:
        raise ValueError('This exact build does not support the fixed ambient diagnostic.')
    with file_lease(dll):
        return _invoke_pinned(owned, dll, payload, export)


def _invoke_pinned(owned, dll, config, export):
    import isolated_process
    dll, build = verify_build(dll)
    owned.owned()
    if (not owned.native_initialized or owned.expected not in isolated_process.PROFILES
            or isolated_process.PROFILES[owned.expected][0] not in (1, 2, 3)):
        raise ValueError('The hook experiment requires an original portable Gold or History child.')
    api, k = owned.api, owned.api.k
    k.CreateRemoteThread.argtypes = [W.HANDLE, C.c_void_p, C.c_size_t, C.c_void_p,
                                     C.c_void_p, W.DWORD, C.POINTER(W.DWORD)]
    k.CreateRemoteThread.restype = W.HANDLE
    k.VirtualFreeEx.argtypes = [W.HANDLE, C.c_void_p, C.c_size_t, W.DWORD]
    k.VirtualFreeEx.restype = W.BOOL
    def read(address, size):
        data, count = C.create_string_buffer(size), C.c_size_t()
        api.check(k.ReadProcessMemory(owned._handle, address, data, size, C.byref(count)))
        if count.value != size:
            raise RuntimeError('Incomplete owned child read.')
        return data.raw
    def write(address, value):
        owned.owned()
        data, count = C.create_string_buffer(value), C.c_size_t()
        api.check(k.WriteProcessMemory(owned._handle, address, data, len(value), C.byref(count)))
        if count.value != len(value):
            raise RuntimeError('Incomplete owned child write.')
    peb, returned = C.c_size_t(), W.ULONG()
    if api.nt.NtQueryInformationProcess(owned._handle, 26, C.byref(peb), C.sizeof(peb), C.byref(returned)) or not peb.value:
        raise RuntimeError('The owned target must be x86.')
    image = struct.unpack('<I', read(peb.value + 8, 4))[0]
    header = read(image, 4096)
    pe = struct.unpack_from('<I', header, 0x3c)[0]
    edition, stamp, image_size, entry, _, entry_hex, imports = isolated_process.PROFILES[owned.expected]
    if (header[:2] != b'MZ' or not 64 <= pe < 3000 or header[pe:pe+6] != b'PE\0\0\x4c\x01'
            or struct.unpack_from('<I', header, pe + 8)[0] != stamp
            or struct.unpack_from('<I', header, pe + 0x50)[0] != image_size
            or read(image + entry, 5) != bytes.fromhex(entry_hex)):
        raise RuntimeError('The owned original image/entry guard changed.')
    slots = [image + imports[i] for i in (0, 1)]
    pointers = [struct.unpack('<I', read(slot, 4))[0] for slot in slots]
    if any(not 0x10000 <= ptr < 0xffff0000 for ptr in pointers):
        raise RuntimeError('The original kernel import pointers are invalid.')
    renderer = owned.executable.with_name('d3d9.dll')
    from .game import graphics_profile
    if digest(renderer) != graphics_profile.DXVK_SHA256:
        raise ValueError('The owned renderer changed before hook initialization.')
    payload,renderer_offset,path_offset,export_offset,config_offset=_payload_prefix(renderer,dll,export)
    payload.extend(config)
    if len(payload) > 8192:
        raise ValueError('Hook configuration exceeds the fixed bounded data allocation.')
    data_base = code_base = thread = None
    finished = False
    try:
        data_base = api.check(k.VirtualAllocEx(owned._handle, None, 8192, 0x3000, 4))
        code_base = api.check(k.VirtualAllocEx(owned._handle, None, 4096, 0x3000, 4))
        if data_base >= 0xffffe000 or code_base >= 0xfffff000:
            raise RuntimeError('The x86 allocation range was exceeded.')
        write(data_base, bytes(payload))
        code = worker(code_base, data_base, *slots, path_offset, export_offset, config_offset, renderer_offset)
        write(code_base, code)
        previous = W.DWORD()
        api.check(k.VirtualProtectEx(owned._handle, code_base, 4096, 0x20, C.byref(previous)))
        api.check(k.FlushInstructionCache(owned._handle, code_base, len(code)))
        if previous.value != 4 or [struct.unpack('<I', read(slot, 4))[0] for slot in slots] != pointers:
            raise RuntimeError('The owned worker or import guards changed.')
        owned.owned()
        thread = api.check(k.CreateRemoteThread(owned._handle, None, 0, code_base, None, 0, None))
        if k.WaitForSingleObject(thread, 10000) != 0:
            raise RuntimeError('The fixed supersampling initializer did not finish within 10 seconds.')
        finished = True
        stage, result = struct.unpack('<2I', read(data_base, 8))
        if stage != 3 or result != 1:
            raise RuntimeError(f'Supersampling initializer refused the laboratory (stage {stage}, result {result}).')
        return {'succeeded': True, 'export': export.rstrip(b'\0').decode('ascii'),
                'dll_sha256': build['files'][DLL_NAME],
                'original_entry_guarded': True, 'worker_code_rx': True}
    finally:
        if thread:
            k.CloseHandle(thread)
        # A timed-out thread can still use its data/code. The portable starter's
        # exception path terminates only this owned child; never free live code.
        if finished or thread is None:
            for address in (code_base, data_base):
                if address:
                    k.VirtualFreeEx(owned._handle, address, 0, 0x8000)
