"""Request Windows elevation only for the pinned, per-copy WFP helper."""
import ctypes as C
from ctypes import wintypes as W
import subprocess
import time

import isolated_process


class ShellExecute(C.Structure):
    _fields_ = [('size', W.DWORD), ('mask', W.ULONG), ('window', W.HWND), ('verb', W.LPCWSTR),
                ('file', W.LPCWSTR), ('parameters', W.LPCWSTR), ('directory', W.LPCWSTR),
                ('show', C.c_int), ('instance', W.HINSTANCE), ('id_list', C.c_void_p),
                ('class_name', W.LPCWSTR), ('class_key', W.HKEY), ('hot_key', W.DWORD),
                ('icon', W.HANDLE), ('process', W.HANDLE)]


class ElevatedProcess:
    def __init__(self,api,process,lock):
        self.api,self.process,self.lock=api,process,lock

    def poll(self):
        result=self.api.k.WaitForSingleObject(self.process,0)
        if result==258:return None
        if result!=0:self.api.check(False)
        code=W.DWORD();self.api.check(self.api.k.GetExitCodeProcess(self.process,C.byref(code)))
        return code.value

    def wait(self,timeout=180):
        deadline=time.monotonic()+timeout
        while self.poll() is None:
            if time.monotonic()>=deadline:raise TimeoutError('Der Windows-Netzhelfer ist noch aktiv.')
            self.api.k.WaitForSingleObject(self.process,100)
        return self.poll()

    def close(self):
        if self.process:
            self.api.k.CloseHandle(self.process);self.process=0
        if self.lock:
            self.api.k.CloseHandle(self.lock);self.lock=0


def start(helper, executable, action):
    from platform_standalone import POLICY
    helper, executable = map(isolated_process.plain, (helper, executable))
    if action not in ('install', 'remove','serve-lan') or isolated_process.digest(helper) != POLICY:
        raise ValueError('Ungeprüfte Aktion oder ungeprüfter Windows-Netzhelfer.')
    api = isolated_process.Windows(); isolated_process.bind_extra(api)
    k, check = api.k, api.check
    lock = k.CreateFileW(str(helper), 0x80000000, 1, None, 3, 0x00200000, None)
    if lock == C.c_void_p(-1).value:
        check(False)
    request = ShellExecute(); request.size = C.sizeof(request)
    request.mask, request.verb, request.file, request.show = 0x40 | 0x100, 'runas', str(helper), 0
    request.parameters = subprocess.list2cmdline([action, str(executable)])
    request.directory = str(helper.parent)
    shell = C.WinDLL('shell32', use_last_error=True).ShellExecuteExW
    shell.argtypes, shell.restype = [C.POINTER(ShellExecute)], W.BOOL
    try:
        if isolated_process.digest(helper) != POLICY:
            raise ValueError('Der Windows-Netzhelfer wurde verändert.')
        check(shell(C.byref(request)))
        return ElevatedProcess(api,request.process,lock)
    except BaseException:
        if request.process:k.CloseHandle(request.process)
        k.CloseHandle(lock)
        raise


def run(helper,executable,action,*,timeout=180):
    if action not in ('install','remove'):raise ValueError('Unbekannte Installationsaktion.')
    process=start(helper,executable,action)
    try:
        code=process.wait(timeout)
        if code:raise RuntimeError('Windows-Netzfilter: Rückgabecode '+str(code))
    finally:
        # Closing a handle never terminates a security dialog/elevated process.
        process.close()
