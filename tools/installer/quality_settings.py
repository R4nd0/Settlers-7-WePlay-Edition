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
    value=validate(value)
    if len(raw)>65536:
        raise ValueError('quality_ini_invalid')
    bom,encoding=b'','utf-8'
    for prefix,name in ((b'\xff\xfe','utf-16-le'),(b'\xfe\xff','utf-16-be'),(b'\xef\xbb\xbf','utf-8')):
        if raw.startswith(prefix):bom,encoding=prefix,name;break
    text=raw[len(bom):].decode(encoding,errors='strict')
    newline='\r\n' if '\r\n' in text else '\n'
    sections={'quality':{k.casefold():k for k in LEVELS},
              'effects':{k.casefold():k for k in (*EFFECTS,'Antialiasing')}}
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
            result.append(('\n[Quality]' if section=='quality' else '\n[Effects]')+newline)
            result.extend(k+' = '+str(value[k])+newline for k in keys.values())
    after=bom+''.join(result).encode(encoding)
    if len(after)>65536:raise ValueError('quality_ini_invalid')
    return after


def prepare(root):
    value=load(root)
    graphics.ensure_closed()
    path=graphics._plain(display.options_path())
    before=graphics._read(path,65536) if path.exists() else None
    after=replace(before or b'',value)
    if after==before:return
    parent=graphics._plain(path.parent);parent.mkdir(exist_ok=True)
    if (graphics._read(path,65536) if path.exists() else None)!=before:
        raise ValueError('quality_ini_changed')
    graphics.atomic_write(path,after,parent)
