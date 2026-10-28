"""Borderless placement for one verified, launcher-owned S7 child.

No injection, hooks, display-mode changes, process searches or focus changes.
Only an eligible top-level window belonging to the original Popen handle is
adjusted. The worker stops on exit, topology change or an API error.
"""
import ctypes as C
from ctypes import wintypes as W
import os
from pathlib import Path
import threading

import display_settings as display
import graphics_profile as graphics

OVERLAPPED = 0x00cf0000
EX_EDGES = 0x00000300


def bounds(screen):
    x, y, width, height = (screen[key] for key in ('x', 'y', 'width', 'height'))
    if (any(type(n) is not int for n in (x, y, width, height))
            or not -65536 <= x <= 65536 or not -65536 <= y <= 65536
            or not display._resolution(width, height)):
        raise ValueError('Unsupported borderless desktop bounds.')
    return x, y, width, height


class Windows:
    def __init__(self, game, executable):
        if os.name != 'nt' or type(game.pid) is not int or game.pid <= 0 or game.poll() is not None:
            raise ValueError('An active owned Windows child is required.')
        self.game, self.pid = game, game.pid
        self.handle = W.HANDLE(int(game._handle))  # Original Popen handle, never OpenProcess by PID.
        self.kernel = C.WinDLL('kernel32', use_last_error=True)
        self.api = C.WinDLL('user32', use_last_error=True)
        self.kernel.GetProcessId.argtypes = [W.HANDLE]; self.kernel.GetProcessId.restype = W.DWORD
        self.kernel.QueryFullProcessImageNameW.argtypes = [W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)]
        self.kernel.QueryFullProcessImageNameW.restype = W.BOOL
        size = W.DWORD(32768); name = C.create_unicode_buffer(size.value)
        if (self.kernel.GetProcessId(self.handle) != self.pid
                or not self.kernel.QueryFullProcessImageNameW(self.handle, 0, name, C.byref(size))
                or Path(name.value).resolve(strict=True) != executable):
            raise ValueError('The game handle does not belong to this bundle.')
        self.callback = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
        signatures = {
            'EnumWindows': ([self.callback, W.LPARAM], W.BOOL),
            'GetWindowThreadProcessId': ([W.HWND, C.POINTER(W.DWORD)], W.DWORD),
            'IsWindowVisible': ([W.HWND], W.BOOL), 'IsIconic': ([W.HWND], W.BOOL),
            'GetWindow': ([W.HWND, W.UINT], W.HWND),
            'GetClassNameW': ([W.HWND, W.LPWSTR, C.c_int], C.c_int),
            'GetClientRect': ([W.HWND, C.POINTER(W.RECT)], W.BOOL),
            'GetWindowRect': ([W.HWND, C.POINTER(W.RECT)], W.BOOL),
            'SetWindowPos': ([W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT], W.BOOL),
            'SetThreadDpiAwarenessContext': ([W.HANDLE], W.HANDLE),
        }
        for key, (arguments, result) in signatures.items():
            function = getattr(self.api, key); function.argtypes = arguments; function.restype = result
        suffix = 'PtrW' if C.sizeof(C.c_void_p) == 8 else 'W'
        self.get_style = getattr(self.api, 'GetWindowLong' + suffix)
        self.set_style = getattr(self.api, 'SetWindowLong' + suffix)
        self.get_style.argtypes = [W.HWND, C.c_int]; self.get_style.restype = C.c_ssize_t
        self.set_style.argtypes = [W.HWND, C.c_int, C.c_ssize_t]; self.set_style.restype = C.c_ssize_t

    def alive(self):
        return self.game.poll() is None and self.kernel.GetProcessId(self.handle) == self.pid

    def owned(self, window):
        pid = W.DWORD()
        self.api.GetWindowThreadProcessId(window, C.byref(pid))
        return pid.value == self.pid and self.alive()

    def candidates(self):
        found = []
        @self.callback
        def collect(window, unused):
            if (self.owned(window) and self.api.IsWindowVisible(window)
                    and not self.api.IsIconic(window) and not self.api.GetWindow(window, 4)):
                name = C.create_unicode_buffer(256); rect = W.RECT()
                if (self.api.GetClassNameW(window, name, len(name)) and name.value != '#32770'
                        and self.api.GetClientRect(window, C.byref(rect))
                        and rect.right >= 800 and rect.bottom >= 600):
                    found.append(window)
            return True
        if not self.api.EnumWindows(collect, 0):
            raise C.WinError(C.get_last_error())
        return found

    def place(self, window, rectangle):
        if not self.owned(window):
            return False
        rect = W.RECT()
        if not self.api.GetWindowRect(window, C.byref(rect)):
            raise C.WinError(C.get_last_error())
        style, extended = self.get_style(window, -16), self.get_style(window, -20)
        actual = (rect.left, rect.top, rect.right-rect.left, rect.bottom-rect.top)
        if not (style & OVERLAPPED or extended & EX_EDGES) and actual == rectangle:
            return False
        # HWND ownership is checked again directly before every mutation.
        for slot, old, mask in ((-16, style, OVERLAPPED), (-20, extended, EX_EDGES)):
            if old & mask:
                if not self.owned(window): return False
                C.set_last_error(0)
                if not self.set_style(window, slot, old & ~mask) and C.get_last_error():
                    raise C.WinError(C.get_last_error())
        if not self.owned(window): return False
        # FRAMECHANGED | NOACTIVATE | NOZORDER | NOOWNERZORDER | ASYNCWINDOWPOS.
        if not self.api.SetWindowPos(window, None, *rectangle, 0x4234):
            raise C.WinError(C.get_last_error())
        return True


class Controller:
    def __init__(self, api, screen):
        self.api, self.screen, self.rectangle = api, dict(screen), bounds(screen)
        self.stop = threading.Event()
        self.applied = 0
        self.failure = None
        self.thread = threading.Thread(target=self.run, name='s7-borderless', daemon=True)

    def tick(self, displays):
        current = [screen for screen in displays if screen['device'] == self.screen['device']]
        if len(current) != 1 or bounds(current[0]) != self.rectangle:
            raise ValueError('The target desktop changed.')
        windows = self.api.candidates()
        if len(windows) > 1:
            raise ValueError('Ambiguous owned game windows.')
        if windows and self.api.place(windows[0], self.rectangle):
            self.applied += 1

    def run(self):
        previous_dpi = None
        try:
            previous_dpi = self.api.api.SetThreadDpiAwarenessContext(W.HANDLE(-4))
            if not previous_dpi:
                raise RuntimeError('Per-monitor window coordinates are unavailable.')
            while not self.stop.is_set() and self.api.alive():
                self.tick(display.enumerate_displays())
                self.stop.wait(2)
        except (OSError, RuntimeError, ValueError):
            self.failure = 'Borderless placement stopped; the game was left running.'
            print(self.failure, flush=True)
        finally:
            if previous_dpi:
                self.api.api.SetThreadDpiAwarenessContext(previous_dpi)

    def close(self):
        self.stop.set()
        self.thread.join(timeout=2)


def start(root, game, screen):
    executable = graphics._plain(Path(root) / 'Base/Bin/Settlers7R.exe').resolve(strict=True)
    if graphics.content.digest(executable) != graphics.EXE_SHA256:
        raise ValueError('Unsupported game executable for borderless placement.')
    bounds(screen)
    controller = Controller(Windows(game, executable), screen)
    controller.thread.start()
    return controller
