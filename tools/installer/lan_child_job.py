"""Windows lifetime boundary for the hub's own service child only."""
import ctypes
from ctypes import wintypes
import subprocess
import os
from pathlib import Path
import signal
import sys


class BasicLimits(ctypes.Structure):
    _fields_ = [('PerProcessUserTimeLimit', ctypes.c_int64),
                ('PerJobUserTimeLimit', ctypes.c_int64), ('LimitFlags', wintypes.DWORD),
                ('MinimumWorkingSetSize', ctypes.c_size_t), ('MaximumWorkingSetSize', ctypes.c_size_t),
                ('ActiveProcessLimit', wintypes.DWORD), ('Affinity', ctypes.c_size_t),
                ('PriorityClass', wintypes.DWORD), ('SchedulingClass', wintypes.DWORD)]


class IoCounters(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint64) for name in (
        'ReadOperationCount', 'WriteOperationCount', 'OtherOperationCount',
        'ReadTransferCount', 'WriteTransferCount', 'OtherTransferCount')]


class ExtendedLimits(ctypes.Structure):
    _fields_ = [('BasicLimitInformation', BasicLimits), ('IoInfo', IoCounters),
                ('ProcessMemoryLimit', ctypes.c_size_t), ('JobMemoryLimit', ctypes.c_size_t),
                ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]


def kernel_api():
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    for name, arguments, result in (
            ('CreateJobObjectW', [ctypes.c_void_p, wintypes.LPCWSTR], wintypes.HANDLE),
            ('SetInformationJobObject', [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD], wintypes.BOOL),
            ('AssignProcessToJobObject', [wintypes.HANDLE, wintypes.HANDLE], wintypes.BOOL),
            ('CloseHandle', [wintypes.HANDLE], wintypes.BOOL)):
        function = getattr(api, name)
        function.argtypes, function.restype = arguments, result
    return api


class ServiceJob:
    """Noninheritable unnamed job. Never assign the game or an existing service."""
    def __init__(self, *, api=None):
        self.posix = os.name == 'posix' and api is None
        if self.posix:
            self.handle, self.child = True, None
            return
        self.api = kernel_api() if api is None else api
        self.handle = self.api.CreateJobObjectW(None, None)
        self.child = None
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        limits = ExtendedLimits()
        limits.BasicLimitInformation.LimitFlags = 0x2000  # KILL_ON_JOB_CLOSE
        if not self.api.SetInformationJobObject(self.handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)):
            error = ctypes.get_last_error()
            self.close()
            raise ctypes.WinError(error)

    def start(self, arguments, **options):
        if not self.handle or self.child is not None:
            raise RuntimeError('Der Lobby-Dienst wurde bereits gestartet oder beendet.')
        if self.posix:
            child = subprocess.Popen([sys.executable, '-I', '-B', str(Path(__file__).resolve()),
                                      '--parent', str(os.getpid()), *map(str, arguments)], **options)
            self.child = child
            return child
        child = subprocess.Popen(arguments, **options)
        self.child = child
        try:
            if not self.api.AssignProcessToJobObject(self.handle, int(child._handle)):
                raise ctypes.WinError(ctypes.get_last_error())
        except BaseException:
            # Assignment can fail under an incompatible enclosing job. Fail
            # closed and reap only the process this call just created.
            if child.poll() is None:
                child.terminate()
            child.wait(timeout=10)
            raise
        return child

    def close(self):
        if self.posix:
            self.handle = None
            if self.child is not None:
                if self.child.poll() is None:
                    self.child.terminate()
                self.child.wait(timeout=10)
            return
        if self.handle:
            handle, self.handle = self.handle, None
            if not self.api.CloseHandle(handle):
                raise ctypes.WinError(ctypes.get_last_error())


if __name__ == '__main__' and sys.platform == 'linux':
    # A separate, single-threaded exec wrapper avoids preexec_fn in a GUI/hub.
    if len(sys.argv) < 4 or sys.argv[1] != '--parent':
        raise SystemExit(2)
    parent = int(sys.argv[2])
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(1, signal.SIGTERM, 0, 0, 0) != 0 or os.getppid() != parent:
        raise SystemExit(2)
    os.execv(sys.argv[3], sys.argv[3:])
