"""Private, bounded Windows named-pipe channel for invitation secrets only.

The desktop server uses PipeOptions.CurrentUserOnly and a random 128-bit name.
The normal stdout event stream never receives invitation material.
"""
import ctypes
from ctypes import wintypes
import json
import re
import time

from internet_room import InternetError

FORMAT = 'weplay-internet-private-v1'


class PrivateChannel:
    def __init__(self, name, *, cancel_check=None):
        if not isinstance(name, str) or not re.fullmatch('weplay-internet-[0-9a-f]{32}', name):
            raise InternetError('internet_channel_failed')
        self.name, self.cancel = name, cancel_check
        self.handle = None

    def __enter__(self):
        self.api = ctypes.WinDLL('kernel32', use_last_error=True)
        for name, args, result in (
            ('CreateFileW', [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                             wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE], wintypes.HANDLE),
            ('SetNamedPipeHandleState', [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD),
                                        ctypes.c_void_p, ctypes.c_void_p], wintypes.BOOL),
            ('ReadFile', [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                          ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p], wintypes.BOOL),
            ('WriteFile', [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                           ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p], wintypes.BOOL),
            ('CloseHandle', [wintypes.HANDLE], wintypes.BOOL)):
            f = getattr(self.api, name); f.argtypes = args; f.restype = result
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if self.cancel: self.cancel()
            handle = self.api.CreateFileW('\\\\.\\pipe\\' + self.name, 0xC0000000, 0, None, 3, 0, None)
            if handle != ctypes.c_void_p(-1).value:
                self.handle = handle
                mode = wintypes.DWORD(1)  # byte read mode, PIPE_NOWAIT
                if self.api.SetNamedPipeHandleState(handle, ctypes.byref(mode), None, None):
                    return self
                self.close()
                break
            if ctypes.get_last_error() not in (2, 231): break
            time.sleep(.02)
        raise InternetError('internet_channel_failed')

    def _transfer(self, length, data=None):
        result = bytearray(); position = 0
        deadline = time.monotonic() + 10
        while position < length and time.monotonic() < deadline:
            if self.cancel: self.cancel()
            amount = min(4096, length - position)
            buffer = ctypes.create_string_buffer(data[position:position + amount] if data is not None else amount)
            count = wintypes.DWORD()
            function = self.api.WriteFile if data is not None else self.api.ReadFile
            ok = function(self.handle, buffer, amount, ctypes.byref(count), None)
            if not ok and ctypes.get_last_error() not in (232, 536):
                raise InternetError('internet_channel_failed')
            if count.value:
                if data is None: result.extend(buffer.raw[:count.value])
                position += count.value
            else:
                time.sleep(.01)
        if position != length: raise InternetError('internet_channel_failed')
        return bytes(result)

    def read(self):
        size = int.from_bytes(self._transfer(4), 'little')
        if not 1 <= size <= 4096: raise InternetError('internet_channel_failed')
        try:
            value = json.loads(self._transfer(size))
            if not isinstance(value, dict) or set(value) != {'format', 'invitation'} or value['format'] != FORMAT:
                raise ValueError()
            if value['invitation'] is not None and (not isinstance(value['invitation'], str) or len(value['invitation']) > 2048):
                raise ValueError()
            return value['invitation']
        except (ValueError, TypeError, UnicodeError):
            raise InternetError('internet_channel_failed') from None

    def invitation_ready(self, invitation):
        if not isinstance(invitation, str) or len(invitation) > 2048:
            raise InternetError('internet_channel_failed')
        raw = json.dumps(dict(format=FORMAT, invitation=invitation), separators=(',', ':')).encode('ascii')
        self._transfer(4 + len(raw), len(raw).to_bytes(4, 'little') + raw)

    def close(self):
        if self.handle:
            self.api.CloseHandle(self.handle); self.handle = None

    def __exit__(self, *_):
        self.close()
