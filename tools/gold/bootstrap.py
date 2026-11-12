"""Owned Win64 launcher for the exact x86 Gold image and explicit initializer."""
import ctypes as C
from ctypes import wintypes as W
import os
from pathlib import Path
import struct
import time

from entry_bridge import bridge
from installer import gold


class ProcessEntry(C.Structure):
    _fields_ = [('size', W.DWORD), ('usage', W.DWORD), ('pid', W.DWORD),
                ('heap', C.c_size_t), ('module', W.DWORD), ('threads', W.DWORD),
                ('parent', W.DWORD), ('priority', W.LONG), ('flags', W.DWORD), ('exe', W.WCHAR * 260)]


class Startup(C.Structure):
    _fields_ = [('cb', W.DWORD), ('reserved', W.LPWSTR), ('desktop', W.LPWSTR), ('title', W.LPWSTR),
                ('x', W.DWORD), ('y', W.DWORD), ('width', W.DWORD), ('height', W.DWORD),
                ('chars_x', W.DWORD), ('chars_y', W.DWORD), ('fill', W.DWORD),
                ('flags', W.DWORD), ('show', W.WORD), ('reserved_size', W.WORD),
                ('reserved_data', C.c_void_p), ('input', W.HANDLE), ('output', W.HANDLE), ('error', W.HANDLE)]


class Process(C.Structure):
    _fields_ = [('process', W.HANDLE), ('thread', W.HANDLE), ('pid', W.DWORD), ('tid', W.DWORD)]


class Memory(C.Structure):
    _fields_ = [('length', W.DWORD), ('load', W.DWORD)] + [
        (key, C.c_ulonglong) for key in ('total', 'available', 'total_page', 'available_page',
                                       'total_virtual', 'available_virtual', 'extended')]


class Windows:
    def __init__(self):
        if os.name != 'nt' or C.sizeof(C.c_void_p) != 8:
            raise RuntimeError('Der Gold-Starter benötigt Windows x64.')
        self.k = C.WinDLL('kernel32', use_last_error=True)
        self.nt = C.WinDLL('ntdll')
        signatures = {
            'CreateToolhelp32Snapshot': ([W.DWORD, W.DWORD], W.HANDLE),
            'Process32FirstW': ([W.HANDLE, C.POINTER(ProcessEntry)], W.BOOL),
            'Process32NextW': ([W.HANDLE, C.POINTER(ProcessEntry)], W.BOOL),
            'CloseHandle': ([W.HANDLE], W.BOOL),
            'OpenProcess': ([W.DWORD, W.BOOL, W.DWORD], W.HANDLE),
            'CreateMutexW': ([C.c_void_p, W.BOOL, W.LPCWSTR], W.HANDLE),
            'ReleaseMutex': ([W.HANDLE], W.BOOL),
            'GlobalMemoryStatusEx': ([C.POINTER(Memory)], W.BOOL),
            'GetLogicalProcessorInformationEx': ([W.DWORD, C.c_void_p, C.POINTER(W.DWORD)], W.BOOL),
            'GetProcessAffinityMask': ([W.HANDLE, C.POINTER(C.c_size_t), C.POINTER(C.c_size_t)], W.BOOL),
            'SetProcessAffinityMask': ([W.HANDLE, C.c_size_t], W.BOOL),
            'QueryFullProcessImageNameW': ([W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)], W.BOOL),
            'CreateProcessW': ([W.LPCWSTR, W.LPWSTR, C.c_void_p, C.c_void_p, W.BOOL, W.DWORD,
                                C.c_void_p, W.LPCWSTR, C.POINTER(Startup), C.POINTER(Process)], W.BOOL),
            'ReadProcessMemory': ([W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)], W.BOOL),
            'WriteProcessMemory': ([W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)], W.BOOL),
            'VirtualAllocEx': ([W.HANDLE, C.c_void_p, C.c_size_t, W.DWORD, W.DWORD], C.c_void_p),
            'VirtualProtectEx': ([W.HANDLE, C.c_void_p, C.c_size_t, W.DWORD, C.POINTER(W.DWORD)], W.BOOL),
            'FlushInstructionCache': ([W.HANDLE, C.c_void_p, C.c_size_t], W.BOOL),
            'ResumeThread': ([W.HANDLE], W.DWORD),
            'WaitForSingleObject': ([W.HANDLE, W.DWORD], W.DWORD),
            'GetExitCodeProcess': ([W.HANDLE, C.POINTER(W.DWORD)], W.BOOL),
            'TerminateProcess': ([W.HANDLE, W.UINT], W.BOOL),
        }
        for name, (args, returns) in signatures.items():
            function = getattr(self.k, name)
            function.argtypes, function.restype = args, returns
        self.nt.NtQueryInformationProcess.argtypes = [W.HANDLE, C.c_int, C.c_void_p, W.ULONG, C.POINTER(W.ULONG)]
        self.nt.NtQueryInformationProcess.restype = C.c_long

    @staticmethod
    def check(value):
        if not value:
            raise OSError(C.get_last_error(), 'Gold Windows-Aufruf fehlgeschlagen.')
        return value

    def processes(self):
        snapshot = self.k.CreateToolhelp32Snapshot(2, 0)
        if snapshot == C.c_void_p(-1).value:
            self.check(False)
        try:
            item = ProcessEntry(); item.size = C.sizeof(item)
            found = self.k.Process32FirstW(snapshot, C.byref(item))
            while found:
                yield item
                item = ProcessEntry(); item.size = C.sizeof(item)
                found = self.k.Process32NextW(snapshot, C.byref(item))
            if C.get_last_error() != 18:
                self.check(False)
        finally:
            self.k.CloseHandle(snapshot)

    def process_path(self, pid):
        handle = self.check(self.k.OpenProcess(0x1000, False, pid))
        try:
            path = C.create_unicode_buffer(32768); length = W.DWORD(len(path))
            self.check(self.k.QueryFullProcessImageNameW(handle, 0, path, C.byref(length)))
            return Path(path.value).resolve()
        finally:
            self.k.CloseHandle(handle)

    def closed(self, root=None):
        for process in self.processes():
            if process.exe.casefold() != 'settlers7r.exe':
                continue
            if root is None or self.process_path(process.pid).is_relative_to(gold.plain(root)):
                raise RuntimeError('Bitte diese Siedler-7-Kopie vor der Vorbereitung oder einem weiteren Start beenden.')

    def owned(self, child, executable, expected):
        rows = [p for p in self.processes() if p.pid == child.pid]
        path = C.create_unicode_buffer(32768); length = W.DWORD(len(path))
        self.check(self.k.QueryFullProcessImageNameW(child.process, 0, path, C.byref(length)))
        if (len(rows) != 1 or rows[0].parent != os.getpid() or Path(path.value).resolve() != executable
                or gold.digest(executable) != expected):
            raise RuntimeError('Gold-Prozessbesitz konnte nicht bestätigt werden.')


def ensure_closed(root=None):
    Windows().closed(root)


def core_mask(data, allowed):
    """At most two physical cores in processor group 0, respecting process mask."""
    at, selected = 0, []
    while at < len(data):
        if at + 8 > len(data):
            raise ValueError('Ungültige CPU-Topologie.')
        relation, size = struct.unpack_from('<II', data, at)
        if size < 8 or at + size > len(data):
            raise ValueError('Ungültige CPU-Topologie.')
        if relation == 0:
            if size < 48 or struct.unpack_from('<H', data, at + 30)[0] != 1:
                raise ValueError('Nicht unterstützte CPU-Gruppe.')
            mask, group = struct.unpack_from('<QH', data, at + 32)
            usable = mask & allowed if group == 0 else 0
            if usable:
                selected.append(usable & -usable)
        at += size
    if not selected:
        raise ValueError('Keine zulässigen physischen CPU-Kerne gefunden.')
    return sum(selected[:2])


def launch(executable, dll, *, environment, on_ready=None):
    from installer import EXE_SHA256, ADAPTER_SHA256
    executable, dll = gold.plain(executable), gold.plain(dll)
    if gold.digest(executable) != EXE_SHA256 or gold.digest(dll) != ADAPTER_SHA256:
        raise ValueError('Ungeprüfter Gold-Programmstand.')
    api = Windows(); k = api.k; check = api.check
    mutex = check(k.CreateMutexW(None, True, 'WePlayGoldLauncher'))
    if C.get_last_error() == 183:
        k.CloseHandle(mutex)
        raise RuntimeError('Der Gold-Starter läuft bereits.')
    child = Process(); ready = False
    try:
        api.closed()
        memory = Memory(); memory.length = C.sizeof(memory)
        check(k.GlobalMemoryStatusEx(C.byref(memory)))
        if memory.available < 4 * 1024**3:
            raise RuntimeError('Für den Gold-Start werden mindestens 4 GiB freier Arbeitsspeicher benötigt.')
        startup = Startup(); startup.cb = C.sizeof(startup)
        command = C.create_unicode_buffer('"' + str(executable) + '" -vSynch')
        block = C.create_unicode_buffer('\0'.join(key + '=' + value for key, value in sorted(environment.items())) + '\0\0')
        check(k.CreateProcessW(str(executable), command, None, None, False, 4 | 0x400,
                              block, str(executable.parent), C.byref(startup), C.byref(child)))
        api.owned(child, executable, EXE_SHA256)
        allowed, system = C.c_size_t(), C.c_size_t()
        check(k.GetProcessAffinityMask(child.process, C.byref(allowed), C.byref(system)))
        length = W.DWORD()
        k.GetLogicalProcessorInformationEx(0, None, C.byref(length))
        if not 48 <= length.value <= 1024 * 1024:
            raise ValueError('CPU-Topologie nicht verfügbar.')
        topology = C.create_string_buffer(length.value)
        check(k.GetLogicalProcessorInformationEx(0, topology, C.byref(length)))
        check(k.SetProcessAffinityMask(child.process, core_mask(topology.raw[:length.value], allowed.value)))

        def read(address, size):
            buffer = C.create_string_buffer(size); done = C.c_size_t()
            check(k.ReadProcessMemory(child.process, address, buffer, size, C.byref(done)))
            if done.value != size: raise RuntimeError('Unvollständiger Gold-Speicherzugriff.')
            return buffer.raw

        def write(address, data):
            buffer = C.create_string_buffer(data); done = C.c_size_t()
            check(k.WriteProcessMemory(child.process, address, buffer, len(data), C.byref(done)))
            if done.value != len(data): raise RuntimeError('Unvollständiger Gold-Speicherzugriff.')

        def protect(address, size, mode):
            old = W.DWORD()
            check(k.VirtualProtectEx(child.process, address, size, mode, C.byref(old)))
            return old.value

        peb, returned = C.c_size_t(), W.ULONG()
        if api.nt.NtQueryInformationProcess(child.process, 26, C.byref(peb), C.sizeof(peb), C.byref(returned)) != 0 or not peb.value:
            raise RuntimeError('Gold-Prozessarchitektur nicht bestätigt.')
        image = struct.unpack('<I', read(peb.value + 8, 4))[0]
        header = read(image, 4096); pe = struct.unpack_from('<I', header, 0x3c)[0]
        if (header[:2] != b'MZ' or not 64 <= pe < 3000 or header[pe:pe+6] != b'PE\0\0\x4c\x01'
                or struct.unpack_from('<I', header, pe+8)[0] != 0x4d8744d4
                or struct.unpack_from('<I', header, pe+0x50)[0] != 0xdc4000):
            raise RuntimeError('Gold-Speicherabbild stimmt nicht überein.')
        entry = image + 0x7aee5b; original = bytes.fromhex('e8de090000')
        if read(entry, 5) != original: raise RuntimeError('Gold-Einsprungpunkt verändert.')
        remote = check(k.VirtualAllocEx(child.process, None, 4096, 0x3000, 4))
        status = check(k.VirtualAllocEx(child.process, None, 4096, 0x3000, 4))
        if max(remote, status) > 0xfffff000: raise RuntimeError('Gold-x86-Adressbereich verletzt.')
        blob, _ = bridge(remote, image, status, str(dll))
        write(remote, blob)
        if protect(remote, 4096, 0x20) != 4: raise RuntimeError('Gold-Codebereich verändert.')
        check(k.FlushInstructionCache(child.process, remote, 4096))
        jump = b'\xe9' + struct.pack('<I', (remote-entry-5) & 0xffffffff)
        previous = protect(entry, 5, 0x40); write(entry, jump); protect(entry, 5, previous)
        check(k.FlushInstructionCache(child.process, entry, 5))
        if k.ResumeThread(child.thread) != 1: raise RuntimeError('Gold-Startthread verändert.')
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if k.WaitForSingleObject(child.process, 0) == 0: break
            state = struct.unpack('<I', read(status, 4))[0]
            if state == 1:
                ready = True; break
            if state == 2: break
            time.sleep(.05)
        if not ready: raise RuntimeError('Der Gold-Adapter hat die Initialisierung abgelehnt.')
        if read(entry, 5) != jump: raise RuntimeError('Gold-Einsprungpunkt unerwartet verändert.')
        previous = protect(entry, 5, 0x40); write(entry, original); protect(entry, 5, previous)
        check(k.FlushInstructionCache(child.process, entry, 5))
        if read(entry, 5) != original or read(image+0xdef8, 5) != bytes.fromhex('32c083c404'):
            raise RuntimeError('Gold-Startkorrektur konnte nicht bestätigt werden.')
        if on_ready: on_ready(child.pid)
        while k.WaitForSingleObject(child.process, 500) == 258:
            pass
        code = W.DWORD(); check(k.GetExitCodeProcess(child.process, C.byref(code)))
        return {'native_initialized': True, 'startup_no_verified': True, 'exit_code': code.value}
    finally:
        if child.process:
            # A failed pre-entry initialization may leave our own suspended child.
            # No already-running game or unrelated process is ever terminated.
            if not ready and k.WaitForSingleObject(child.process, 0) == 258:
                api.owned(child, executable, EXE_SHA256)
                check(k.TerminateProcess(child.process, 0xe05370a4))
                k.WaitForSingleObject(child.process, 5000)
            k.CloseHandle(child.thread); k.CloseHandle(child.process)
        k.ReleaseMutex(mutex); k.CloseHandle(mutex)
