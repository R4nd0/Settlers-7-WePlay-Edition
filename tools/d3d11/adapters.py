"""Enumerate DXGI hardware adapters in dgVoodoo's one-based ordinal order."""
import ctypes as c
import uuid


def enumerate_adapters():
    if not hasattr(c, 'WinDLL'):
        raise RuntimeError('Die Grafikkartenauswahl benoetigt Windows.')
    class Description(c.Structure):
        _fields_ = [('name', c.c_wchar * 128), ('vendor', c.c_uint32),
                    ('device', c.c_uint32), ('subsystem', c.c_uint32),
                    ('revision', c.c_uint32), ('vram', c.c_size_t),
                    ('system', c.c_size_t), ('shared', c.c_size_t),
                    ('luid_low', c.c_uint32), ('luid_high', c.c_int32),
                    ('flags', c.c_uint32)]
    def method(obj, index, result, *args):
        table = c.cast(obj, c.POINTER(c.POINTER(c.c_void_p))).contents
        return c.WINFUNCTYPE(result, c.c_void_p, *args)(table[index])
    def release(obj):
        method(obj, 2, c.c_uint32)(obj)
    iid = (c.c_ubyte * 16).from_buffer_copy(
        uuid.UUID('770aae78-f26f-4dba-a829-253c83d1b387').bytes_le)
    factory = c.c_void_p()
    create = c.WinDLL('dxgi', use_last_error=True).CreateDXGIFactory1
    create.argtypes = [c.c_void_p, c.POINTER(c.c_void_p)]
    create.restype = c.c_int32
    if create(c.byref(iid), c.byref(factory)) < 0:
        raise RuntimeError('Grafikkarten konnten nicht aufgelistet werden.')
    result = []
    try:
        for index in range(32):
            adapter = c.c_void_p()
            hr = method(factory, 12, c.c_int32, c.c_uint32,
                        c.POINTER(c.c_void_p))(factory, index, c.byref(adapter))
            if hr & 0xffffffff == 0x887a0002:
                return result
            if hr < 0:
                raise RuntimeError('Grafikkartenaufzaehlung ist unvollstaendig.')
            try:
                desc = Description()
                if method(adapter, 10, c.c_int32, c.POINTER(Description))(
                        adapter, c.byref(desc)) < 0:
                    raise RuntimeError('Grafikkartenbeschreibung ist nicht lesbar.')
                if not desc.flags & 2:  # DXGI_ADAPTER_FLAG_SOFTWARE
                    result.append({'ordinal': index + 1, 'name': desc.name,
                                   'vram_mb': desc.vram // (1024 * 1024)})
            finally:
                release(adapter)
        raise RuntimeError('Zu viele Grafikadapter.')
    finally:
        release(factory)


if __name__ == '__main__':
    import json
    print(json.dumps(enumerate_adapters(), ensure_ascii=True))
