"""Windows display enumeration and allowlisted S7 startup settings.

Never changes desktop modes, registry values or game files. Windowed startup
can clear only Quality/FullScreen in the game's shared Options.ini.
The native engine distinguishes only primary/secondary D3D9 adapters. Explicit
output selection is limited to the first two active displays.
"""
import ctypes as C
from ctypes import wintypes as W
import os
from pathlib import Path
import re


MAX_PIXELS = 3840 * 2160  # Bound render-target pressure in the 32-bit game.
DEVICE = re.compile(r'\\\\\.\\DISPLAY[1-9][0-9]{0,2}')


def validate(value):
    if value is None:
        return None
    if (not isinstance(value, dict) or set(value) != {'device', 'width', 'height', 'mode'}
            or not isinstance(value['device'], str) or not DEVICE.fullmatch(value['device'])
            or type(value['width']) is not int or type(value['height']) is not int
            or not 800 <= value['width'] <= 3840 or not 600 <= value['height'] <= 3840
            or value['width'] * value['height'] > MAX_PIXELS
            or value['mode'] not in ('windowed', 'borderless')):
        raise ValueError('Invalid display selection.')
    return dict(value)


class Device(C.Structure):
    _fields_ = [('size', W.DWORD), ('name', W.WCHAR * 32), ('description', W.WCHAR * 128),
                ('flags', W.DWORD), ('unused_id', W.WCHAR * 128), ('unused_key', W.WCHAR * 128)]


class Mode(C.Structure):
    # DEVMODEW's 16-byte printer/display union uses the display layout here.
    _fields_ = [('device', W.WCHAR * 32)] + [(key, W.WORD) for key in
        ('spec', 'driver', 'size', 'extra')] + [('fields', W.DWORD),
        ('x', W.LONG), ('y', W.LONG), ('orientation', W.DWORD), ('fixed_output', W.DWORD)] + [
        (key, W.SHORT) for key in ('color', 'duplex', 'y_resolution', 'tt', 'collate')] + [
        ('form', W.WCHAR * 32), ('log_pixels', W.WORD)] + [(key, W.DWORD) for key in
        ('bpp', 'width', 'height', 'flags', 'frequency', 'icm_method', 'icm_intent',
         'media', 'dither', 'reserved1', 'reserved2', 'panning_width', 'panning_height')]


def _resolution(width, height):
    return (800 <= width <= 3840 and 600 <= height <= 3840
            and width * height <= MAX_PIXELS)


def enumerate_displays():
    """Public GDI names/modes only; never persist device IDs or registry keys."""
    if os.name != 'nt' or C.sizeof(Device) != 840 or C.sizeof(Mode) != 220:
        raise RuntimeError('Windows display information is unavailable.')
    api = C.WinDLL('user32', use_last_error=True)
    api.EnumDisplayDevicesW.argtypes = [W.LPCWSTR, W.DWORD, C.POINTER(Device), W.DWORD]
    api.EnumDisplayDevicesW.restype = W.BOOL
    api.EnumDisplaySettingsW.argtypes = [W.LPCWSTR, W.DWORD, C.POINTER(Mode)]
    api.EnumDisplaySettingsW.restype = W.BOOL
    found = []
    for index in range(32):
        device = Device(); device.size = C.sizeof(device)
        if not api.EnumDisplayDevicesW(None, index, C.byref(device), 0):
            break
        if not device.flags & 1:
            continue
        if not DEVICE.fullmatch(device.name):
            raise ValueError('Unexpected Windows display name.')
        current = Mode(); current.size = C.sizeof(current)
        if not api.EnumDisplaySettingsW(device.name, 0xffffffff, C.byref(current)):
            raise RuntimeError('The current display mode could not be read.')
        modes = set()
        for number in range(4096):
            mode = Mode(); mode.size = C.sizeof(mode)
            if not api.EnumDisplaySettingsW(device.name, number, C.byref(mode)):
                break
            if mode.bpp == 32 and _resolution(mode.width, mode.height):
                modes.add((mode.width, mode.height))
        else:
            raise ValueError('Unexpectedly many display modes.')
        if len(modes) > 128:
            raise ValueError('Unexpectedly many distinct resolutions.')
        found.append({'device': device.name, 'adapter': device.description,
            'primary': bool(device.flags & 4), 'x': current.x, 'y': current.y,
            'width': current.width, 'height': current.height,
            'resolutions': [list(size) for size in sorted(modes, key=lambda size: (size[0]*size[1], size))]})
    if not found or sum(display['primary'] for display in found) != 1:
        raise RuntimeError('An unambiguous primary display is required.')
    # Match the game's primary-first GDI list for the supported two outputs.
    found.sort(key=lambda display: not display['primary'])
    return found


def resolve(value, displays, *, renderer='native', gpu=None):
    value = validate(value)
    if value is None:
        return None
    candidates = [(index, display) for index, display in enumerate(displays[:2])
                  if display['device'] == value['device']]
    if len(candidates) != 1:
        raise ValueError('The selected display is unavailable. Reopen Graphics.')
    index, display = candidates[0]
    if [value['width'], value['height']] not in display['resolutions']:
        raise ValueError('The selected resolution is unavailable on this display.')
    if value['mode'] == 'borderless' and (value['width'], value['height']) != (display['width'], display['height']):
        raise ValueError('Borderless fullscreen requires the current desktop resolution. Reopen Graphics.')
    if renderer == 'dxvk' and gpu is not None and gpu.casefold() != display['adapter'].casefold():
        raise ValueError('Choose Automatic or the graphics card connected to the selected display.')
    return index, display


def game_arguments(value, displays):
    """Build fixed flags from validated numbers; never accept free-form argv."""
    selected = resolve(value, displays)
    if selected is None:
        return ()
    index, display = selected
    value = validate(value)
    x = display['x'] + max(0, (display['width'] - value['width']) // 2)
    y = display['y'] + max(0, (display['height'] - value['height']) // 2)
    if not all(type(number) is int and -65536 <= number <= 65536 for number in (x, y)):
        raise ValueError('Unexpected desktop coordinates.')
    # The exact game's bool-option parser (VA 0x4C5860) always stores true.
    # Passing "false" after -fullScreen still enables fullscreen.
    return ('-width', str(value['width']), '-height', str(value['height']),
            '-screenNumber', str(index), '-x', str(x), '-y', str(y))


def windowed_options(raw):
    """Clear only FullScreen, preserving BOM, whitespace and unrelated bytes.

    Refuse ambiguous target sections/keys and unexpected values before writing.
    """
    if len(raw) > 65536:
        raise ValueError('The game display settings are unexpectedly large.')
    bom, encoding = b'', 'utf-8'
    for prefix, name in ((b'\xff\xfe','utf-16-le'), (b'\xfe\xff','utf-16-be'), (b'\xef\xbb\xbf','utf-8')):
        if raw.startswith(prefix):
            bom, encoding = prefix, name
            break
    text = raw[len(bom):].decode(encoding, errors='strict')
    newline = '\r\n' if '\r\n' in text else '\n'
    lines = text.splitlines(keepends=True)
    quality = False; section_seen = key_seen = False
    insertion = None
    for index, line in enumerate(lines):
        match = re.fullmatch(r'\s*\[([^\]]+)\]\s*(?:[;#].*)?', line.strip())
        if match:
            if quality and insertion is None:
                insertion = index
            quality = match[1].casefold() == 'quality'
            if quality:
                if section_seen:
                    raise ValueError('Duplicate Quality section in Options.ini.')
                section_seen = True
        if quality and re.match(r'\s*fullscreen\s*=', line, re.IGNORECASE):
            if key_seen:
                raise ValueError('Duplicate FullScreen entry in Options.ini.')
            key_seen = True
            number = re.fullmatch(r'(\s*fullscreen\s*=\s*)([01])([ \t]*(?:[;#][^\r\n]*)?)(\r?\n)?', line, re.IGNORECASE)
            if not number:
                raise ValueError('Unexpected FullScreen value in Options.ini.')
            lines[index] = number[1] + '0' + number[3] + (number[4] or '')
    if not key_seen:
        if insertion is None:
            insertion = len(lines)
        if insertion and not lines[insertion-1].endswith(('\r','\n')):
            lines[insertion-1] += newline
        if not section_seen:
            lines.insert(insertion, '[Quality]' + newline)
            insertion += 1
        lines.insert(insertion, 'FullScreen = 0' + newline)
    result = bom + ''.join(lines).encode(encoding)
    if len(result) > 65536:
        raise ValueError('The game display settings would become too large.')
    return result


def options_path():
    # Same known Documents folder used by the game, including redirection.
    buffer = C.create_unicode_buffer(32768)
    api = C.WinDLL('shell32').SHGetFolderPathW
    api.argtypes = [W.HWND, C.c_int, W.HANDLE, W.DWORD, W.LPWSTR]
    api.restype = C.c_long
    if api(None, 5, None, 0, buffer) < 0:
        raise RuntimeError('The Windows Documents folder is unavailable.')
    return Path(buffer.value) / 'THE SETTLERS 7 - History Edition' / 'Options.ini'


def prepare_windowed():
    """Atomic, single-key preparation; a running game prevents any mutation."""
    import graphics_profile as graphics
    path = graphics._plain(options_path())
    before = graphics._read(path, 65536) if path.exists() else None
    after = windowed_options(before or b'')
    if after == before:
        return
    graphics.ensure_closed()
    parent = graphics._plain(path.parent)
    parent.mkdir(exist_ok=True)
    path = graphics._plain(path)
    if (graphics._read(path, 65536) if path.exists() else None) != before:
        raise ValueError('Display settings changed during startup. Reopen Graphics.')
    graphics.atomic_write(path, after, parent)
