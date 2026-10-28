"""Allowlisted S7 rendering preferences, independent of player/account data."""
import ctypes
import re

# Values come from S7's own graphics settings UI, not guessed engine ranges.
LEVELS = ('ShaderQuality', 'TextureQuality', 'ObjectQuality', 'DistanceCulling',
          'ShadowQuality', 'WaterQuality', 'ClutterQuality')
EFFECTS = ('Shadows', 'SSAO', 'DepthOfField', 'SunShafts', 'Parallax')
DEFAULTS = {**dict.fromkeys(LEVELS, 1), **dict.fromkeys(EFFECTS, 0),
            'Shadows': 1, 'AA': 'standard', 'Filtering': 'appdriven', 'FPS': 60}
AA = {'off': ('off', 0), 'standard': ('appdriven', 1),
      'msaa2': ('2x', 0), 'msaa4': ('4x', 0), 'msaa8': ('8x', 0)}
FILTERS = ('appdriven', '2', '4', '8', '16')


def validate(value):
    if not isinstance(value, dict) or set(value) != set(DEFAULTS):
        raise ValueError('Unbekanntes Grafikprofil.')
    for key in LEVELS + EFFECTS + ('FPS',):
        if type(value[key]) is not int:
            raise ValueError('Ungueltiger Grafikwert.')
    if any(value[k] not in (0, 1, 2) for k in LEVELS):
        raise ValueError('Grafikqualitaet ausserhalb des unterstuetzten Bereichs.')
    if any(value[k] not in (0, 1) for k in EFFECTS):
        raise ValueError('Ungueltiger Bildeffekt.')
    if (not isinstance(value['AA'], str) or not isinstance(value['Filtering'], str)
            or value['AA'] not in AA or value['Filtering'] not in FILTERS
            or not 30 <= value['FPS'] <= 240):
        raise ValueError('Nicht unterstuetzte Bildausgabe.')
    return dict(value)


def game_values(value):
    value = validate(value)
    return {'Quality': {key: value[key] for key in LEVELS},
            'Effects': {**{key: value[key] for key in EFFECTS},
                        'Antialiasing': AA[value['AA']][1]}}


def conservative(value, recovery=False):
    """Do not let saved/command-line profiles bypass the startup safety limits."""
    result = validate(value)
    for key in LEVELS:
        result[key] = min(result[key], 0 if recovery else 1)
    for key in EFFECTS:
        result[key] = 0 if recovery or key != 'Shadows' else result[key]
    result['AA'] = 'off' if recovery or result['AA'].startswith('msaa') else result['AA']
    # These wrapper settings have no effect on the native fallback.
    result['Filtering'], result['FPS'] = 'appdriven', 60
    return result


def decode(raw):
    if len(raw) > 65536:
        raise ValueError('S7-Grafikeinstellungen sind unerwartet gross.')
    encoding = 'utf-16' if raw.startswith((b'\xff\xfe', b'\xfe\xff')) else (
        'utf-8-sig' if raw.startswith(b'\xef\xbb\xbf') else 'utf-8')
    return raw.decode(encoding, errors='strict'), encoding


def read_defaults(raw):
    text, _ = decode(raw)
    result = dict(DEFAULTS)
    section = None
    for line in text.splitlines():
        if re.fullmatch(r'\s*\[[^\]]+\]\s*', line):
            section = line.strip()[1:-1]
        key, sep, number = line.partition('=')
        key = key.strip()
        if sep and ((section == 'Quality' and key in LEVELS)
                    or (section == 'Effects' and key in EFFECTS)):
            try:
                result[key] = int(number.strip())
            except ValueError:
                raise ValueError('Unbekanntes S7-Grafikformat.') from None
        if sep and section == 'Effects' and key == 'Antialiasing':
            result['AA'] = 'off' if number.strip() == '0' else 'standard'
    return validate(result)


def replace_game_values(raw, value):
    """Preserve unrelated settings/comments; reject duplicate target keys."""
    text, encoding = decode(raw)
    values = game_values(value)
    newline = '\r\n' if '\r\n' in text else '\n'
    lines = text.splitlines(keepends=True)
    result = []
    section = None
    seen_sections, seen_keys = set(), set()
    def append_missing():
        if section in values:
            if result and not result[-1].endswith(('\r', '\n')):
                result[-1] += newline
            for key, number in values[section].items():
                if (section, key) not in seen_keys:
                    result.append(f'{key} = {number}{newline}')
    for line in lines:
        match = re.fullmatch(r'\s*\[([^\]]+)\]\s*', line.strip())
        if match:
            append_missing()
            section = match[1]
            if section in values and section in seen_sections:
                raise ValueError('Doppelte Grafiksektion in Options.ini.')
            seen_sections.add(section)
        key, sep, _ = line.partition('=')
        key = key.strip()
        if sep and key in values.get(section, {}):
            if (section, key) in seen_keys:
                raise ValueError('Doppelter Grafikwert in Options.ini.')
            seen_keys.add((section, key))
            end = '\r\n' if line.endswith('\r\n') else ('\n' if line.endswith('\n') else '')
            line = f'{key} = {values[section][key]}{end}'
        result.append(line)
    append_missing()
    for name in values:
        if name not in seen_sections:
            if result and not result[-1].endswith(('\r', '\n')):
                result[-1] += newline
            result.append(f'{newline}[{name}]{newline}')
            result.extend(f'{key} = {number}{newline}' for key, number in values[name].items())
    return ''.join(result).encode(encoding)


def options_path():
    """Use Windows' Documents location, including redirected Documents."""
    from pathlib import Path
    buffer = ctypes.create_unicode_buffer(32768)
    shell = ctypes.WinDLL('shell32')
    if shell.SHGetFolderPathW(None, 5, None, 0, buffer) < 0:
        raise RuntimeError('Windows-Dokumentenordner ist nicht verfuegbar.')
    return Path(buffer.value) / 'THE SETTLERS 7 - History Edition' / 'Options.ini'


def wrapper_configuration(template, adapter, value):
    from prepare import configuration
    value = validate(value)
    text = configuration(template, adapter, value['FPS']).decode('utf-8')
    match = re.search(r'(?ms)^\[DirectX\]\r?\n(.*?)(?=^\[|\Z)', text)
    if not match:
        raise ValueError('DirectX-Konfiguration fehlt.')
    body = match[1]
    for key, number in [('Antialiasing', AA[value['AA']][0]),
                        ('Filtering', value['Filtering'])]:
        body, count = re.subn(r'(?m)^' + key + r'\s*=.*$', key + ' = ' + number, body)
        if count != 1:
            raise ValueError('Unerwartete DirectX-Konfiguration.')
    return (text[:match.start(1)] + body + text[match.end(1):]).encode('utf-8')
