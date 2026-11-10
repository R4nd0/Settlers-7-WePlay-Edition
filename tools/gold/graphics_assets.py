"""Build Gold's local visual assets from exact recipient-owned archives.

No installation, game launch, simulation changes or multiplayer admission.
The History archive guards stay separate; only authored visual helpers are shared.
"""
import hashlib
import importlib.util
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import profile_interface as gold
from bba import Archive

MIDSUMMER = HERE.parent / 'midsummer'
spec = importlib.util.spec_from_file_location('gold_midsummer_environment', MIDSUMMER / 'profile.py')
environment = importlib.util.module_from_spec(spec)
spec.loader.exec_module(environment)

SHARED_SOURCE = '736077c195660aa90edc601049672b64ac335dcc10870c78c2ddf93901a26655'
CAMERA = 'Script/local/stage/camera.lua'
FOCALIZER = 'Script/local/stage/focalizer.lua'
SETTINGS = 'Script/local/menu/panel/menu/options/settings.lua'
TUTORIAL = 'Script/local/menu/tutorial.lua'
TURN_ROTATION = ('n["focalizer"]["rotateCamera"]=n["focalizer"]["rotateCamera"]+'
                 'n["focalizer"]["getDampenerVal"]()')


def replace_once(raw, before, after):
    source, body, offsets = gold.decoded_view(raw)
    if body.count(before) != 1:
        raise ValueError('Gold graphics anchor does not match exactly once.')
    at = body.index(before)
    return (source[:offsets[at]] + after + source[offsets[at + len(before)]:]).encode('utf-8')


def camera_script(raw):
    source, body, offsets = gold.decoded_view(raw)
    call = 'e["GFX"]["ToggleDepthOfField"]('
    if body.count(call) != 6:
        raise ValueError('Gold camera requires six original depth-of-field transitions.')
    for at in reversed([i for i in range(len(body)) if body.startswith(call, i)]):
        source = source[:offsets[at]] + 'weplayCameraDepthOfField(' + source[offsets[at + len(call)]:]
    helper = ('\nlocal weplayCameraDepthOfField=(function(...)\n'
              + (MIDSUMMER / 'camera_preferences.lua').read_text(encoding='utf-8') + '\nend)(e);\n')
    return (source[:offsets[0]] + helper + source[offsets[0]:]).encode('utf-8')


def focalizer_script(raw):
    # The local native simulation now actually advances. The original helper
    # returns the cumulative game.turnsExecuted count; adding that count to a
    # camera angle produces continuous rotation without any rotation input.
    # Keep the counter, normal mouse/key movement and camera damping intact.
    return replace_once(raw, TURN_ROTATION, '')


def settings_script(raw):
    raw = replace_once(raw, '["dayNightCycle"]=4,}', '["dayNightCycle"]=4,["midsummer"]=5,}')
    anchor = 'l["weatherNumberDropdownList"]["setSelectedIndex"](o["gameplayOptions"][i["id"]["weather"]])'
    return replace_once(raw, anchor,
        'l["weatherNumberDropdownList"]["addItem"]({["text"]={"Mittsommer / Midsummer",true},'
        '["data"]=r["weatherModes"]["midsummer"]})' + anchor)


def tutorial_script(raw):
    anchor = 'e["weather"]["create"]()e["head"]='
    return replace_once(raw, anchor, 'e["weather"]["create"]();(function(...)\n'
        + (MIDSUMMER / 'runtime.lua').read_text(encoding='utf-8') + '\nend)(e,o,r);e["head"]=')


def interface(raw):
    # Compose with Gold's local profile corrections from the same original.
    # Never accept an arbitrary already-modified or History archive as input.
    if hashlib.sha256(raw).hexdigest() != gold.SOURCE:
        raise ValueError('Gold graphics requires the exact original Gold interface archive.')
    archive = Archive(gold.build(raw))
    changes = {CAMERA: camera_script(archive.read(CAMERA)),
               FOCALIZER: focalizer_script(archive.read(FOCALIZER)),
               SETTINGS: settings_script(archive.read(SETTINGS)),
               TUTORIAL: tutorial_script(archive.read(TUTORIAL))}
    result = archive.replace(changes)
    verified = Archive(result)
    if archive.entries.keys() != verified.entries.keys():
        raise ValueError('Gold graphics changed the interface member set.')
    for name, entry in archive.entries.items():
        if entry.values[4] != 256 and verified.read(name) != changes.get(name, archive.read(name)):
            raise ValueError('Gold graphics archive round-trip failed.')
    return result


def shared(raw):
    if hashlib.sha256(raw).hexdigest() != SHARED_SOURCE:
        raise ValueError('Gold lighting requires the exact original Gold shared archive.')
    archive = Archive(raw)
    return environment.append_resource(raw, environment.RESOURCE,
        environment.environment(archive.read('Config/Environment/Neutral.xml')))
