"""Narrow Windows UAC worker. Only the VPN child is elevated, never the game."""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import secrets
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compatibility
from internet_room import InternetError, parse, select_interface
from lan_interfaces import enumerate_interfaces

FORMAT = 'weplay-easytier-ipc-v1'


def inherit_private_access(directory):
    """Let files created by the elevated token retain the normal user's access.

    CPython's protected 0700 directory alone does not grant inheritable ACEs.
    Use the invoking token's user SID explicitly, with no Everyone/Users ACE.
    """
    api = winapi()
    api.GetCurrentProcess.restype = wintypes.HANDLE
    api.LocalFree.argtypes = [ctypes.c_void_p]; api.LocalFree.restype = ctypes.c_void_p
    security = ctypes.WinDLL('advapi32', use_last_error=True)
    for name,args in (
        ('OpenProcessToken',[wintypes.HANDLE,wintypes.DWORD,ctypes.POINTER(wintypes.HANDLE)]),
        ('GetTokenInformation',[wintypes.HANDLE,ctypes.c_int,ctypes.c_void_p,wintypes.DWORD,ctypes.POINTER(wintypes.DWORD)]),
        ('ConvertSidToStringSidW',[ctypes.c_void_p,ctypes.POINTER(wintypes.LPWSTR)]),
        ('ConvertStringSecurityDescriptorToSecurityDescriptorW',[wintypes.LPCWSTR,wintypes.DWORD,ctypes.POINTER(ctypes.c_void_p),ctypes.c_void_p]),
        ('SetFileSecurityW',[wintypes.LPCWSTR,wintypes.DWORD,ctypes.c_void_p])):
        function=getattr(security,name);function.argtypes=args;function.restype=wintypes.BOOL
    token=wintypes.HANDLE();sid_text=wintypes.LPWSTR();descriptor=ctypes.c_void_p()
    try:
        if not security.OpenProcessToken(api.GetCurrentProcess(),8,ctypes.byref(token)):raise OSError()
        size=wintypes.DWORD()
        security.GetTokenInformation(token,1,None,0,ctypes.byref(size))
        buffer=ctypes.create_string_buffer(size.value)
        if not security.GetTokenInformation(token,1,buffer,size.value,ctypes.byref(size)):raise OSError()
        sid=ctypes.cast(buffer,ctypes.POINTER(ctypes.c_void_p))[0]
        if not security.ConvertSidToStringSidW(sid,ctypes.byref(sid_text)):raise OSError()
        sddl='D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;'+sid_text.value+')'
        if not security.ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,1,ctypes.byref(descriptor),None):raise OSError()
        if not security.SetFileSecurityW(str(directory),0x80000004,descriptor):raise OSError()
    except OSError:
        raise InternetError('internet_runtime_invalid') from None
    finally:
        if descriptor:api.LocalFree(descriptor)
        if sid_text:api.LocalFree(ctypes.cast(sid_text,ctypes.c_void_p))
        if token:api.CloseHandle(token)


def winapi():
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    for name, args, result in (
        ('OpenProcess', [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD], wintypes.HANDLE),
        ('GetProcessTimes', [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4, wintypes.BOOL),
        ('QueryFullProcessImageNameW', [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR,
                                        ctypes.POINTER(wintypes.DWORD)], wintypes.BOOL),
        ('WaitForSingleObject', [wintypes.HANDLE, wintypes.DWORD], wintypes.DWORD),
        ('CloseHandle', [wintypes.HANDLE], wintypes.BOOL)):
        function = getattr(api, name); function.argtypes = args; function.restype = result
    return api


def parent_process(pid):
    api = winapi()
    handle = api.OpenProcess(0x1000 | 0x100000, False, pid)  # query limited + synchronize
    if not handle:
        raise InternetError('internet_parent_invalid')
    try:
        times = [wintypes.FILETIME() for _ in range(4)]
        if not api.GetProcessTimes(handle, *(ctypes.byref(x) for x in times)):
            raise InternetError('internet_parent_invalid')
        capacity = wintypes.DWORD(32768); image = ctypes.create_unicode_buffer(capacity.value)
        if not api.QueryFullProcessImageNameW(handle, 0, image, ctypes.byref(capacity)):
            raise InternetError('internet_parent_invalid')
        stamp = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
        return api, handle, stamp, Path(image.value)
    except BaseException:
        api.CloseHandle(handle)
        raise


class ShellExecuteInfo(ctypes.Structure):
    _fields_ = [('cbSize', wintypes.DWORD), ('fMask', wintypes.ULONG), ('hwnd', wintypes.HWND),
                ('lpVerb', wintypes.LPCWSTR), ('lpFile', wintypes.LPCWSTR),
                ('lpParameters', wintypes.LPCWSTR), ('lpDirectory', wintypes.LPCWSTR),
                ('nShow', ctypes.c_int), ('hInstApp', wintypes.HINSTANCE), ('lpIDList', ctypes.c_void_p),
                ('lpClass', wintypes.LPCWSTR), ('hkeyClass', wintypes.HKEY), ('dwHotKey', wintypes.DWORD),
                ('hIcon', wintypes.HANDLE), ('hProcess', wintypes.HANDLE)]


def elevate(python, script, request):
    function = ctypes.WinDLL('shell32', use_last_error=True).ShellExecuteExW
    function.argtypes = [ctypes.POINTER(ShellExecuteInfo)]; function.restype = wintypes.BOOL
    arguments = subprocess.list2cmdline(['-I', '-B', str(script), '--request', str(request)])
    info = ShellExecuteInfo(); info.cbSize = ctypes.sizeof(info)
    info.fMask = 0x40 | 0x100 | 0x400  # retain process handle, synchronous activation, no extra error UI
    info.lpVerb, info.lpFile, info.lpParameters = 'runas', str(python), arguments
    info.lpDirectory, info.nShow = str(python.parent), 0
    if not function(ctypes.byref(info)):
        code = ctypes.get_last_error()
        raise InternetError('internet_elevation_cancelled' if code == 1223 else 'internet_elevation_failed')
    if not info.hProcess:
        raise InternetError('internet_elevation_failed')
    return info.hProcess


def write_state(directory, value):
    temporary = compatibility.plain(directory / 'state.new')
    with temporary.open('w', encoding='utf-8') as stream:
        json.dump(value, stream, separators=(',', ':'))
    # Windows file scanners/readers can briefly retain a non-delete-sharing
    # handle. Retrying the atomic replace keeps the previous complete state.
    deadline = time.monotonic() + 1
    while True:
        try:
            os.replace(temporary, directory / 'state.json')
            return
        except PermissionError:
            if time.monotonic() >= deadline: raise
            time.sleep(.02)


def firewall(root, key, interface=None):
    script = compatibility.plain(root / 'Launcher/Configure-Internet-Firewall.ps1')
    arguments = ['powershell.exe', '-NoProfile', '-NonInteractive', '-File', str(script),
                 '-Bundle', str(root), '-RuleKey', key]
    if interface is None:
        arguments += ['-Remove']
    else:
        arguments += ['-Address', interface.address, '-InterfaceName', interface.name]
    result = subprocess.run(arguments, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, timeout=30,
                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    if result.returncode:
        raise InternetError('internet_firewall_failed')


def verify_worker(root):
    """Check every elevated import/interpreter against the shipped app package.

    The installed content manifest alone is insufficient for elevation because
    it could be changed together with a helper. No unverified installed script,
    DLL, embeddable-Python search file or bytecode cache enters the UAC worker.
    """
    import install
    module = Path(__file__).resolve()
    package = module.parents[2]
    if module.parent != package / 'Tools/installer':
        raise InternetError('internet_runtime_invalid')
    manifest = install.package_manifest(package)
    expected = {('launcher/'+target).lower():manifest['files'][('tools/'+original).lower()]
                for original,target in install.launcher_sources(manifest)}
    expected['launcher/license.txt'] = manifest['files']['tools/license.txt']
    python = {key:digest for key,digest in manifest['files'].items() if key.startswith('runtime/python/')}
    if (not python or compatibility.inventory(root, ('Launcher',)) != expected or
            compatibility.inventory(root, ('Runtime/python',)) != python):
        raise InternetError('internet_runtime_invalid')


class ElevatedEasyTier:
    def __init__(self, root, room, *, host, cancel_check=None, progress=None):
        self.root, self.room, self.host = Path(root), room, host
        self.cancel_check, self.progress = cancel_check, progress
        self.private = self.handle = None
        self.nonce = secrets.token_hex(16)

    def checkpoint(self):
        if self.cancel_check: self.cancel_check()
        if self.progress: self.progress('internet_connect')

    def start(self):
        from easytier_runtime import verify_runtime
        verify_runtime(self.root)
        verify_worker(self.root)
        python = compatibility.plain(self.root / 'Runtime/python/python.exe')
        script = compatibility.plain(self.root / 'Launcher/easytier_elevation.py')
        if not python.is_file() or not script.is_file():
            raise InternetError('internet_runtime_invalid')
        api, handle, stamp, _ = parent_process(os.getpid())
        api.CloseHandle(handle)
        self.checkpoint()
        data = compatibility.plain(self.root / 'LocalData'); data.mkdir(exist_ok=True)
        self.private = Path(tempfile.mkdtemp(prefix='easytier-uac-', dir=data))
        request = self.private / 'request.json'
        value = dict(format=FORMAT, root=str(self.root), room=self.room.token(), host=self.host,
                     parent_pid=os.getpid(), parent_start=stamp, nonce=self.nonce)
        try:
            inherit_private_access(self.private)
            with request.open('x', encoding='utf-8') as stream: json.dump(value, stream)
            self.handle = elevate(python, script, request)
            deadline = time.monotonic() + 65
            while time.monotonic() < deadline:
                self.checkpoint()
                try:
                    interface, _ = self.snapshot()
                    if self.progress: self.progress('internet_ready')
                    return interface
                except FileNotFoundError:
                    pass
                if winapi().WaitForSingleObject(self.handle, 0) != 0x102:
                    raise InternetError('internet_elevation_failed')
                time.sleep(.2)
            raise InternetError('internet_connect_timeout')
        except BaseException:
            self.close()
            raise

    def snapshot(self):
        path = compatibility.plain(self.private / 'state.json')
        if path.stat().st_size > 16384 or time.time() - path.stat().st_mtime > 10:
            raise InternetError('internet_disconnected')
        try:
            deadline = time.monotonic() + 1
            while True:
                try:
                    raw = path.read_text(encoding='utf-8')
                    break
                except PermissionError:
                    if time.monotonic() >= deadline:
                        raise InternetError('internet_status_invalid') from None
                    time.sleep(.02)
            value = json.loads(raw)
            if value.get('format') != FORMAT or value.get('nonce') != self.nonce:
                raise ValueError()
            if value.get('phase') == 'error':
                code = value.get('code')
                if not isinstance(code, str) or not code.startswith('internet_') or len(code) > 64:
                    raise ValueError()
                raise InternetError(code)
            if value.get('phase') != 'ready': raise ValueError()
            interface = select_interface(self.room, value['address'], value['device'], enumerate_interfaces())
            peers = value['peers']
            if not isinstance(peers, dict) or len(peers) > 255:
                raise ValueError()
            for address, cost in peers.items():
                if not interface.contains(address) or address == interface.address or cost not in ('p2p', 'relay'):
                    raise ValueError()
                if self.room.mode == 'direct' and cost != 'p2p': raise ValueError()
            return interface, peers
        except InternetError:
            raise
        except (ValueError, TypeError, KeyError):
            raise InternetError('internet_status_invalid') from None

    def close(self):
        if self.private is None: return
        directory = compatibility.plain(self.private)
        if directory.parent != compatibility.plain(self.root / 'LocalData') or not directory.name.startswith('easytier-uac-'):
            raise InternetError('internet_cleanup_failed')
        (directory / 'stop').touch()
        if self.handle:
            api = winapi()
            if api.WaitForSingleObject(self.handle, 45000) != 0:
                raise InternetError('internet_cleanup_failed')
            api.CloseHandle(self.handle); self.handle = None
        shutil.rmtree(directory)
        self.private = None


def open_overlay(*args, **kwargs):
    if os.name != 'nt':
        raise InternetError('internet_windows_required')
    # Use the same scoped firewall and parent-lifetime worker even when the
    # invoking console is already elevated; runas adds no second UAC prompt then.
    return ElevatedEasyTier(*args, **kwargs)


def worker(request):
    request = compatibility.plain(request)
    directory = compatibility.plain(request.parent)
    if request.name != 'request.json' or request.stat().st_size > 8192:
        raise InternetError('internet_request_invalid')
    value = json.loads(request.read_text(encoding='utf-8'))
    if (not isinstance(value, dict) or set(value) != {'format', 'root', 'room', 'host', 'parent_pid', 'parent_start', 'nonce'} or
            value['format'] != FORMAT or type(value['host']) is not bool or
            type(value['parent_pid']) is not int or type(value['parent_start']) is not int or
            not isinstance(value['nonce'], str) or len(value['nonce']) != 32):
        raise InternetError('internet_request_invalid')
    root = compatibility.plain(value['root'])
    if (directory.parent != compatibility.plain(root / 'LocalData') or not directory.name.startswith('easytier-uac-') or
            Path(__file__).resolve() != root / 'Launcher/easytier_elevation.py'):
        raise InternetError('internet_request_invalid')
    api, parent, stamp, image = parent_process(value['parent_pid'])
    overlay = None
    nonce = value['nonce']
    try:
        # The desktop's backend may run from the app package's identical Python
        # runtime, rather than from the game copy. Bind to the exact process
        # creation and verified interpreter bytes, not merely a reused PID/name.
        if (stamp != value['parent_start'] or image.name.lower() != 'python.exe' or
                compatibility.digest(compatibility.plain(image)) !=
                compatibility.digest(compatibility.plain(root / 'Runtime/python/python.exe'))):
            raise InternetError('internet_parent_invalid')
        from easytier_runtime import EasyTier
        import launch_local
        launch_local.verify_bundle(root)
        def cancel():
            if (directory / 'stop').exists() or api.WaitForSingleObject(parent, 0) != 0x102:
                raise InternetError('internet_cancelled')
        overlay = EasyTier(root, parse(value['room']), host=value['host'], cancel_check=cancel)
        interface = overlay.start()
        firewall(root, nonce, interface)
        request.unlink()  # The private core config is now the only copy on disk.
        deadline = time.monotonic() + 86400
        while time.monotonic() < deadline:
            cancel()
            interface, peers = overlay.snapshot()
            write_state(directory, dict(format=FORMAT, nonce=nonce, phase='ready',
                address=interface.address, device=interface.name, peers=peers))
            time.sleep(.5)
    except BaseException as error:
        code = getattr(error, 'code', 'internet_failed')
        write_state(directory, dict(format=FORMAT, nonce=nonce, phase='error', code=code))
    finally:
        try:
            if overlay:
                try:
                    firewall(root, nonce)
                finally:
                    overlay.close()
        finally:
            try:
                request.unlink(missing_ok=True)
                # A terminated normal launcher cannot reap its IPC directory.
                # Leave a live parent its final state; reap it ourselves only
                # after the exact parent handle is signalled.
                if api.WaitForSingleObject(parent,0) == 0:
                    shutil.rmtree(compatibility.plain(directory))
            finally:
                api.CloseHandle(parent)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--request', type=Path, required=True)
    try:
        worker(parser.parse_args().request)
    except BaseException:
        # The parent receives fixed state categories; never emit request secrets.
        sys.exit(2)
