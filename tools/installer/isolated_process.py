import ctypes as C
from ctypes import wintypes as W
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import time

from standalone_entry import bridge


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
            raise RuntimeError('Der Standalone-Starter benÃ¶tigt Windows x64.')
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
            raise OSError(C.get_last_error(), 'Standalone Windows-Aufruf fehlgeschlagen.')
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


PROFILES = {
    '4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f':
        (1, 0x5d1b15c1, 0x10e7000, 0x622fb7, 0x622fb7, 'e8d2060000',
         (0xc51218, 0xc51194, 0xc51288, 0xc51290, 0xc51098)),
    '8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410':
        (2, 0x5d305b24, 0x1106000, 0x10e3310, 0x622537, 'e800000000',
         (0xc4f208, 0xc4f0b4, 0xc4f278, 0xc4f280, 0xc4f164)),
    '76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab':
        (3, 0x4d8744d4, 0xdc4000, 0x7aee5b, 0x7aee5b, 'e8de090000',
         (0xabf14c, 0xabf124, 0xabf06c, 0xabf088, 0xabf240)),
}
IMPORTS = ('LoadLibraryW', 'GetProcAddress', 'Sleep', 'GetTickCount', 'TerminateProcess')


class StartupEx(C.Structure):
    _fields_ = [('startup', Startup), ('attributes', C.c_void_p)]


class JobBasic(C.Structure):
    _fields_ = [('process_time', C.c_longlong), ('job_time', C.c_longlong), ('flags', W.DWORD),
                ('minimum', C.c_size_t), ('maximum', C.c_size_t), ('active', W.DWORD),
                ('affinity', C.c_size_t), ('priority', W.DWORD), ('scheduling', W.DWORD)]


class JobLimits(C.Structure):
    _fields_ = [('basic', JobBasic), ('io', C.c_ulonglong * 6),
                ('process_memory', C.c_size_t), ('job_memory', C.c_size_t),
                ('peak_process', C.c_size_t), ('peak_job', C.c_size_t)]


def digest(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def plain(path):
    path = Path(path).absolute()
    for part in (path, *path.parents):
        if part.is_symlink() or part.is_junction():
            raise ValueError('Verknüpfte Standalone-Pfade sind nicht zulässig.')
    return path.resolve(strict=True)


def child_environment(environment):
    return {key: value for key, value in environment.items()
            if not key.casefold().startswith(('steam', 'uplay', 'ubisoft'))}


def bind_extra(api):
    signatures = {
        'InitializeProcThreadAttributeList': ([C.c_void_p, W.DWORD, W.DWORD, C.POINTER(C.c_size_t)], W.BOOL),
        'UpdateProcThreadAttribute': ([C.c_void_p, W.DWORD, C.c_size_t, C.c_void_p, C.c_size_t, C.c_void_p, C.c_void_p], W.BOOL),
        'DeleteProcThreadAttributeList': ([C.c_void_p], None),
        'GetProcessMitigationPolicy': ([W.HANDLE, C.c_int, C.c_void_p, C.c_size_t], W.BOOL),
        'CreateJobObjectW': ([C.c_void_p, W.LPCWSTR], W.HANDLE),
        'SetInformationJobObject': ([W.HANDLE, C.c_int, C.c_void_p, W.DWORD], W.BOOL),
        'AssignProcessToJobObject': ([W.HANDLE, W.HANDLE], W.BOOL),
        'CreateFileW': ([W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p, W.DWORD, W.DWORD, W.HANDLE], W.HANDLE),
    }
    for name, (arguments, returns) in signatures.items():
        function = getattr(api.k, name)
        function.argtypes, function.restype = arguments, returns


def core_mask(data, allowed):
    """Two physical cores in the current processor group, before any game code."""
    at, selected = 0, []
    while at < len(data):
        if at+8 > len(data):raise ValueError('Ungültige CPU-Topologie.')
        relation, size = struct.unpack_from('<II', data, at)
        if size < 8 or at+size > len(data):raise ValueError('Ungültige CPU-Topologie.')
        if relation == 0:
            if size < 48 or struct.unpack_from('<H',data,at+30)[0] != 1:
                raise ValueError('Nicht unterstützte CPU-Gruppe.')
            mask, group = struct.unpack_from('<QH',data,at+32)
            usable = mask & allowed if group == 0 else 0
            if usable:selected.append(usable & -usable)
        at += size
    if not selected:raise ValueError('Keine zulässigen physischen CPU-Kerne gefunden.')
    return sum(selected[:2])


class OwnedProcess:
    """Popen-compatible wait/poll handle; the kill-on-close job owns only this game."""
    def __init__(self, api, child, job, executable, expected, locks):
        self.api, self.child, self.job = api, child, job
        self.executable, self.expected, self.locks = executable, expected, locks
        self.pid, self._handle, self.returncode = child.pid, child.process, None
        self.native_initialized = False
        self._weplay_owned=True

    def owned(self):
        rows = [item for item in self.api.processes() if item.pid == self.pid]
        if (len(rows) != 1 or rows[0].parent != os.getpid()
                or self.api.process_path(self.pid) != self.executable or digest(self.executable) != self.expected):
            raise RuntimeError('Der Besitz des Testprozesses ist nicht bestätigt.')

    def poll(self):
        if self.returncode is not None:
            return self.returncode
        state = self.api.k.WaitForSingleObject(self._handle, 0)
        if state == 258:
            return None
        if state != 0:
            self.api.check(False)
        code = W.DWORD()
        self.api.check(self.api.k.GetExitCodeProcess(self._handle, C.byref(code)))
        self.returncode = code.value
        return self.returncode

    def wait(self, timeout=None,*,close=True):
        deadline = None if timeout is None else time.monotonic() + timeout
        while self.poll() is None:
            if deadline is not None and time.monotonic() >= deadline:
                raise subprocess.TimeoutExpired(str(self.executable), timeout)
            self.api.k.WaitForSingleObject(self._handle, 100)
        if close:self.close()
        return self.returncode

    def terminate(self):
        if self.poll() is None:
            self.owned()
            self.api.check(self.api.k.TerminateProcess(self._handle, 0xe0537021))

    kill = terminate

    def close(self):
        if self.poll() is None:
            raise RuntimeError('Der laufende Spielprozess muss vor dem Schließen beendet werden.')
        for handle in (self.child.thread, self.child.process, self.job, *self.locks):
            if handle:
                self.api.k.CloseHandle(handle)
        self.child.thread = self.child.process = self.job = 0
        self.locks = []
        scope=getattr(self,'network_policy',None)
        if scope:
            scope.close();self.network_policy=None


def launch(executable, dll, *, dll_sha256, policy, policy_sha256, environment=None, arguments=(), on_initialized=None):
    executable, dll, policy = plain(executable), plain(dll), plain(policy)
    expected = digest(executable)
    profile = PROFILES.get(expected)
    if not profile or digest(dll) != dll_sha256 or digest(policy) != policy_sha256:
        raise ValueError('Ungeprüfter Standalone-Programmstand.')
    # Persistent, application-scoped Windows filtering must already be present.
    result = subprocess.run([str(policy), 'check', str(executable)], capture_output=True, text=True,
                            timeout=15, creationflags=0x08000000)
    if result.returncode:
        raise RuntimeError('Die Windows-Netzisolation fehlt oder stimmt nicht mit dieser Kopie überein.')
    return _launch(executable, dll, expected, profile, environment or os.environ, arguments,on_initialized=on_initialized)


def _launch(executable, dll, expected, profile, environment, arguments,*,on_initialized=None):
    """Internal launch engine, also exercised with a non-game native fixture."""
    api = Windows(); bind_extra(api); k, check = api.k, api.check
    edition, stamp, image_size, entry_rva, target_rva, entry_hex, imports = profile
    memory = Memory(); memory.length = C.sizeof(memory)
    check(k.GlobalMemoryStatusEx(C.byref(memory)))
    if memory.available < 4 * 1024**3:
        raise RuntimeError('Für den Spielstart werden mindestens 4 GiB freier Arbeitsspeicher benötigt.')
    locks, child, owned = [], Process(), None
    job, attribute_list, attributes_initialized = 0, None, False
    try:
        for path in (executable, dll):
            handle = k.CreateFileW(str(path), 0x80000000, 1, None, 3, 0x00200000, None)
            if handle == C.c_void_p(-1).value:
                check(False)
            locks.append(handle)
        if digest(executable) != expected:
            raise ValueError('Die EXE wurde während der Vorbereitung verändert.')
        length = C.c_size_t()
        k.InitializeProcThreadAttributeList(None, 1, 0, C.byref(length))
        if not 1 <= length.value <= 65536:
            raise RuntimeError('Windows-Prozessrichtlinien sind nicht verfügbar.')
        attribute_list = C.create_string_buffer(length.value)
        check(k.InitializeProcThreadAttributeList(attribute_list, 1, 0, C.byref(length)))
        attributes_initialized = True
        restricted = W.DWORD(1)
        check(k.UpdateProcThreadAttribute(attribute_list, 0, 0x2000e, C.byref(restricted), C.sizeof(restricted), None, None))
        startup = StartupEx(); startup.startup.cb = C.sizeof(startup)
        startup.attributes = C.cast(attribute_list, C.c_void_p)
        job = check(k.CreateJobObjectW(None, None))
        limits = JobLimits(); limits.basic.flags = 0x2000 | 8; limits.basic.active = 1
        check(k.SetInformationJobObject(job, 9, C.byref(limits), C.sizeof(limits)))
        command = C.create_unicode_buffer(subprocess.list2cmdline([str(executable), *arguments]))
        clean = child_environment(environment)
        if any('\0' in key + value or '=' in key for key, value in clean.items()):
            raise ValueError('Ungültige Prozessumgebung.')
        block = C.create_unicode_buffer('\0'.join(key + '=' + value for key, value in sorted(clean.items(), key=lambda row: row[0].casefold())) + '\0\0')
        check(k.CreateProcessW(str(executable), command, None, None, False, 4 | 0x400 | 0x80000,
                              block, str(executable.parent), C.byref(startup.startup), C.byref(child)))
        owned = OwnedProcess(api, child, job, executable, expected, locks)
        owned.owned()
        check(k.AssignProcessToJobObject(job, child.process))
        allowed, system = C.c_size_t(), C.c_size_t()
        check(k.GetProcessAffinityMask(child.process,C.byref(allowed),C.byref(system)))
        topology_size=W.DWORD()
        k.GetLogicalProcessorInformationEx(0,None,C.byref(topology_size))
        if not 48 <= topology_size.value <= 1048576:raise ValueError('CPU-Topologie nicht verfügbar.')
        topology=C.create_string_buffer(topology_size.value)
        check(k.GetLogicalProcessorInformationEx(0,topology,C.byref(topology_size)))
        check(k.SetProcessAffinityMask(child.process,core_mask(topology.raw[:topology_size.value],allowed.value)))
        mitigation = W.DWORD()
        check(k.GetProcessMitigationPolicy(child.process, 13, C.byref(mitigation), C.sizeof(mitigation)))
        if mitigation.value != 1:
            raise RuntimeError('Windows hat die Unterprozesssperre nicht übernommen.')

        def read(address, size):
            buffer, done = C.create_string_buffer(size), C.c_size_t()
            check(k.ReadProcessMemory(child.process, address, buffer, size, C.byref(done)))
            if done.value != size: raise RuntimeError('Unvollständiger Speicherzugriff.')
            return buffer.raw

        def write(address, data):
            buffer, done = C.create_string_buffer(data), C.c_size_t()
            check(k.WriteProcessMemory(child.process, address, buffer, len(data), C.byref(done)))
            if done.value != len(data): raise RuntimeError('Unvollständiger Speicherzugriff.')

        def protect(address, size, mode):
            previous = W.DWORD()
            check(k.VirtualProtectEx(child.process, address, size, mode, C.byref(previous)))
            return previous.value

        peb, returned = C.c_size_t(), W.ULONG()
        if api.nt.NtQueryInformationProcess(child.process, 26, C.byref(peb), C.sizeof(peb), C.byref(returned)) != 0 or not peb.value:
            raise RuntimeError('Der Starter benötigt einen x86-Spielprozess.')
        image = struct.unpack('<I', read(peb.value + 8, 4))[0]
        header = read(image, 4096); pe = struct.unpack_from('<I', header, 0x3c)[0]
        if (header[:2] != b'MZ' or not 64 <= pe < 3000 or header[pe:pe+6] != b'PE\0\0\x4c\x01'
                or struct.unpack_from('<I', header, pe+8)[0] != stamp
                or struct.unpack_from('<I', header, pe+0x50)[0] != image_size
                or struct.unpack_from('<I', header, pe+0x28)[0] != entry_rva):
            raise RuntimeError('Das Speicherabbild entspricht nicht dem geprüften Spiel.')
        entry, original = image + entry_rva, bytes.fromhex(entry_hex)
        if read(entry, 5) != original:
            raise RuntimeError('Der Einsprungpunkt wurde verändert.')
        remote = check(k.VirtualAllocEx(child.process, None, 4096, 0x3000, 4))
        status = check(k.VirtualAllocEx(child.process, None, 4096, 0x3000, 4))
        if max(remote, status) > 0xfffff000:
            raise RuntimeError('Der x86-Adressbereich wurde überschritten.')
        write(status + 32, struct.pack('<8I', 0x37535057, 1, 32, child.pid, child.tid, image, entry_rva, edition))
        write(remote, bridge(remote, image, status, dll, dict(zip(IMPORTS, imports))))
        if protect(remote, 4096, 0x20) != 4:
            raise RuntimeError('Der Startbereich wurde verändert.')
        check(k.FlushInstructionCache(child.process, remote, 4096))
        jump = b'\xe9' + struct.pack('<I', (remote-entry-5) & 0xffffffff)
        previous = protect(entry, 5, 0x40); write(entry, jump); protect(entry, 5, previous)
        check(k.FlushInstructionCache(child.process, entry, 5))
        if k.ResumeThread(child.thread) != 1:
            raise RuntimeError('Der primäre Startthread wurde verändert.')
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline and owned.poll() is None:
            state = struct.unpack('<I', read(status, 4))[0]
            if state in (1, 2): break
            time.sleep(.02)
        else:
            raise RuntimeError('Der native Starter wurde nicht rechtzeitig erreicht.')
        if state != 1 or struct.unpack('<I', read(status+8, 4))[0] != image + target_rva or read(entry, 5) != jump:
            raise RuntimeError('Der native Adapter hat die Initialisierung abgelehnt.')
        kernel_probe=struct.unpack('<I',read(status+20,4))[0]
        if edition!=99 and (not kernel_probe&1 or kernel_probe&~7):
            raise RuntimeError('Die tatsächliche Windows-Netzsperre wurde im Spielprozess nicht bestätigt.')
        previous = protect(entry, 5, 0x40); write(entry, original); protect(entry, 5, previous)
        check(k.FlushInstructionCache(child.process, entry, 5))
        if read(entry, 5) != original:
            raise RuntimeError('Der Einsprungpunkt konnte nicht wiederhergestellt werden.')
        owned.native_initialized = True
        owned.kernel_isolation=kernel_probe
        # The original entry bytes are restored, while the main thread still
        # waits in our bounded bridge. Debugger hooks can validate decoded code
        # before any game Lua state or platform startup executes.
        if on_initialized:on_initialized(owned)
        write(status+12, struct.pack('<I', 1))
        return owned
    except BaseException:
        if owned:
            owned.terminate(); owned.wait(timeout=10)
        else:
            for handle in (child.thread, child.process, job, *locks):
                if handle: k.CloseHandle(handle)
        raise
    finally:
        if attributes_initialized:
            k.DeleteProcThreadAttributeList(attribute_list)
