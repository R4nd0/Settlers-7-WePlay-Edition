"""Fixed x86 native-graphics startup for an owned original portable S7 child.

The caller admits the DLL using the packaged payload's qualification pin. This
module has no lab/source-tree dependency, arbitrary export or attach-by-PID API.
It preserves the original entry, kernel-IAT, ownership, W^X and renderer guards.
"""
from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
from contextlib import contextmanager
import hashlib
from pathlib import Path
import re
import struct

DXVK_SHA256 = '265888c31ca78dffa290c39cb7e50bfb02762590e41927906e46fb32f01497fa'
WATER_B_SHA256 = 'a8a591b346911622bfc9899fa28cbb022a42152df41ed822ef3cfff7c254e2f4'
WATER_B_RECIPE = 'river-depth-fresnel-v2-balanced'
OPERATIONS = {
    'initialize': (b'WePlaySupersamplingInitialize\0', (1048, 3132)),
    'effects': (b'WePlayNativeGraphicsConfigureEffects\0', (4132,)),
    'water': (b'WePlayNativeGraphicsConfigureWater\0', (1076,)),
}


def plain(path):
    value = Path(path).absolute()
    for part in (value, *value.parents):
        if part.is_symlink() or (hasattr(part, 'is_junction') and part.is_junction()):
            raise ValueError('native_graphics_path_invalid')
    return value.resolve()


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def validate_command(operation, payload):
    if operation not in OPERATIONS or not isinstance(payload, bytes):
        raise ValueError('native_graphics_command_invalid')
    export, sizes = OPERATIONS[operation]
    if len(payload) not in sizes:
        raise ValueError('native_graphics_command_invalid')
    size, version = struct.unpack_from('<2I', payload)
    expected_version = 2 if operation == 'initialize' and size == 3132 else 1
    if size != len(payload) or version != expected_version:
        raise ValueError('native_graphics_command_invalid')
    if operation == 'water':
        width, height, enabled = struct.unpack_from('<3I', payload, 8)
        try:
            wide = payload[20:1044].decode('utf-16le')
        except UnicodeDecodeError:
            raise ValueError('native_graphics_command_invalid') from None
        path, separator, padding = wide.partition('\0')
        normalized = path.replace('/', '\\')
        ending = '\\MaterialShaders\\' + WATER_B_RECIPE + '\\' + WATER_B_SHA256 + '.ps.bin'
        if (not 800 <= width <= 3840 or not 600 <= height <= 2160 or enabled not in (0, 1)
                or not separator or padding.strip('\0') or not normalized.endswith(ending)
                or len(normalized) < len(ending) + 3 or normalized[1:3] != ':\\'
                or not normalized[0].isascii() or not normalized[0].isalpha()
                or any(character < ' ' or character in '<>:"|?*' for character in normalized[3:])
                or any(part in ('', '.', '..') or part.endswith((' ', '.')) for part in normalized[3:].split('\\'))
                or payload[1044:] != bytes.fromhex(WATER_B_SHA256)):
            raise ValueError('native_graphics_command_invalid')
    return export


def invoke(owned, dll, expected_sha256, operation, payload):
    export = validate_command(operation, payload)
    if not isinstance(expected_sha256, str) or not re.fullmatch('[0-9a-f]{64}', expected_sha256):
        raise ValueError('native_graphics_payload_invalid')
    dll = plain(dll)
    if dll.name != 'WePlayNativeGraphics.dll' or dll.stat().st_size > 4 * 1024 * 1024:
        raise ValueError('native_graphics_payload_invalid')
    with file_lease(dll):
        if digest(dll) != expected_sha256:
            raise ValueError('native_graphics_payload_changed')
        return _invoke(owned, dll, expected_sha256, payload, export)


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


def _invoke(owned, dll, expected_sha256, config, export):
    import isolated_process
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
    payload = bytearray(8)
    renderer = owned.executable.with_name('d3d9.dll')
    if digest(plain(renderer)) != DXVK_SHA256:
        raise ValueError('The owned renderer changed before hook initialization.')
    renderer_offset = len(payload); payload.extend(str(renderer).encode('utf-16le') + b'\0\0')
    path_offset = len(payload); payload.extend(str(dll).encode('utf-16le') + b'\0\0')
    export_offset = len(payload); payload.extend(export)
    while len(payload) % 4:
        payload.append(0)
    config_offset = len(payload); payload.extend(config)
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
            raise RuntimeError(f'Supersampling initializer refused startup (stage {stage}, result {result}).')
        return {'succeeded': True, 'export': export.rstrip(b'\0').decode('ascii'),
                'dll_sha256': expected_sha256,
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
