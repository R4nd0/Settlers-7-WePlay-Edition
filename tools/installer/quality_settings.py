"""Allowlisted shared INI graphics values, applied only before a closed-game start."""
import re
import graphics_profile as graphics
import display_settings as display

LEVELS = ('ShaderQuality','TextureQuality','ObjectQuality','DistanceCulling','ShadowQuality','WaterQuality','ClutterQuality')
EFFECTS = ('Shadows','SSAO','DepthOfField','SunShafts','Parallax')
DEFAULTS = {**dict.fromkeys(LEVELS,1),**dict.fromkeys(EFFECTS,0),'Shadows':1,'Antialiasing':1}
FILE = 'LocalData/graphics-quality.json'


def validate(value):
    if not isinstance(value,dict) or set(value)!=set(DEFAULTS):
        raise ValueError('quality_invalid')
    for key, number in value.items():
        if type(number) is not int or number not in ((0,1,2) if key in LEVELS else (0,1)):
            raise ValueError('quality_invalid')
    return dict(value)


def load(root):
    path=graphics._plain(root/FILE)
    return validate(graphics._json(graphics._read(path,4096))) if path.exists() else dict(DEFAULTS)


def replace(raw,value):
    return _replace(raw,validate(value))


def without_game_aa(value, smaa):
    return {**value,'Antialiasing':0} if smaa and value['Antialiasing'] else value


def _replace(raw,value):
    if len(raw)>65536:
        raise ValueError('quality_ini_invalid')
    text, bom, encoding = display._decode_options(raw)
    newline='\r\n' if '\r\n' in text else '\n'
    sections={'quality':{k.casefold():k for k in LEVELS if k in value},
              'effects':{k.casefold():k for k in (*EFFECTS,'Antialiasing') if k in value}}
    sections={section:keys for section,keys in sections.items() if keys}
    result=[]; section=None; seen_sections=set();seen_keys=set()
    def missing():
        if section in sections:
            if result and not result[-1].endswith(('\r','\n')):result[-1]+=newline
            for key in sections[section].values():
                if key not in seen_keys:result.append(key+' = '+str(value[key])+newline)
    for line in text.splitlines(keepends=True):
        match=re.fullmatch(r'\s*\[([^\]]+)\]\s*(?:[;#].*)?',line.strip())
        if match:
            missing();section=match[1].casefold()
            if section in sections and section in seen_sections:raise ValueError('quality_ini_invalid')
            seen_sections.add(section)
        keymatch=re.match(r'\s*([^=\r\n]+?)\s*=',line)
        if keymatch and keymatch[1].casefold() in sections.get(section,{}):
            key=sections[section][keymatch[1].casefold()]
            if key in seen_keys:raise ValueError('quality_ini_invalid')
            seen_keys.add(key)
            number=re.fullmatch(r'(\s*[^=]+?\s*=\s*)([012])([ \t]*(?:[;#][^\r\n]*)?)(\r?\n)?',line)
            if not number or (key not in LEVELS and number[2]=='2'):raise ValueError('quality_ini_invalid')
            line=number[1]+str(value[key])+number[3]+(number[4] or '')
        result.append(line)
    missing()
    for section,keys in sections.items():
        if section not in seen_sections:
            if result and not result[-1].endswith(('\r','\n')):result[-1]+=newline
            heading = '[Quality]' if section == 'quality' else '[Effects]'
            result.extend((newline, heading + newline))
            result.extend(k+' = '+str(value[k])+newline for k in keys.values())
    after=bom+''.join(result).encode(encoding, errors='surrogateescape')
    if len(after)>65536:raise ValueError('quality_ini_invalid')
    return after


def prepare(root, *, smaa=False, use_saved_quality=True, force_game_aa=False):
    if use_saved_quality and graphics._plain(root/FILE).exists():
        value=without_game_aa(load(root),smaa)
    elif smaa or force_game_aa:
        # Legacy interactive starts may have only the game's own Options.ini.
        value={'Antialiasing':0}
    else:
        return
    if force_game_aa:value['Antialiasing']=1
    graphics.ensure_closed()
    path=graphics._plain(display.options_path(root) if display.uses_portable_profile(root) else display.options_path())
    before=graphics._read(path,65536) if path.exists() else None
    after=_replace(before or b'',value)
    if after==before:return
    graphics.ensure_closed()
    parent=graphics._plain(path.parent);parent.mkdir(parents=True,exist_ok=True)
    if (graphics._read(path,65536) if path.exists() else None)!=before:
        raise ValueError('quality_ini_changed')
    graphics.atomic_write(path,after,parent)
