"""Add the local Mittsommer weather choice to the verified NEO interface."""
import hashlib
import importlib.util
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'lan'))
from bba import Archive

spec = importlib.util.spec_from_file_location('midsummer_decoder', HERE.parent / 'multiplayer-resume/interface_patch.py')
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)
SOURCES = ('67cbd87bf33981384659b26d4c96d802d4a2a550e16381901e738541895a4e43',
           '7d2d0e91d014810ba0ce8eaa2f31ba2970abd0d8b7b61b4b98dd0c0fb9dde507')


def build(raw):
    if hashlib.sha256(raw).hexdigest() not in SOURCES:
        raise ValueError('Mittsommer requires the verified NEO interface')
    archive = Archive(raw)
    changes = {}
    def change(name, before, after):
        source, body, offsets = decoder.decoded_view(changes.get(name, archive.read(name)))
        if body.count(before) != 1:
            raise ValueError('Mittsommer interface anchor mismatch: ' + name)
        at = body.index(before)
        changes[name] = (source[:offsets[at]] + after + source[offsets[at + len(before)]:]).encode('utf-8')
    member = 'Script/local/menu/panel/menu/options/settings.lua'
    change(member, '["dayNightCycle"]=4,}', '["dayNightCycle"]=4,["midsummer"]=5,}')
    anchor = 'l["weatherNumberDropdownList"]["setSelectedIndex"](o["gameplayOptions"][i["id"]["weather"]])'
    change(member, anchor,
        'l["weatherNumberDropdownList"]["addItem"]({["text"]={"Mittsommer / Midsummer",true},["data"]=r["weatherModes"]["midsummer"]})' + anchor)
    change('Script/local/menu/tutorial.lua', 'e["weather"]["create"]()e["head"]=',
        'e["weather"]["create"]();(function(...)\n' + (HERE / 'runtime.lua').read_text(encoding='utf-8') + '\nend)(e,o,r);e["head"]=')
    return archive.replace(changes)
