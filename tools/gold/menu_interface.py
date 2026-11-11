"""Stage Gold's fast, quiet menu from its exact original Interface.bba.

Includes the existing local-profile correction. Never edits an installation.
The black menu is the default; -mapInMainMenu explicitly retains Castle Forge.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import profile_interface as profile

MAIN = 'Script/local/menu/panel/menu/sectionMenus/mainMenu.lua'
HUD = 'Script/local/menu/panel/hud.lua'
NEWS = 'Script/local/menu/panel/menu/newsTicker.lua'
BOOT = 'Script/local/tools/boot.lua'
CARD = 'Script/local/menu/panel/menu/playerCard.lua'
PINS = {
    MAIN: 'b68176fe9cf810ec0cc26a2b5e67eef5abe9026e4dd959861d283062ce59f8cc',
    HUD: 'a131e75abb7266db168809356a03b5417982d97453b22566da53c2b26aa5c677',
    NEWS: '38432b841dd76ce6c3fcdc4f71524a0222658f25cf4043fa23c9b9f1dfa4a936',
    BOOT: '5bcca2a429799ef838a7a7324734b414c3e1e97b151ab28e86cfe13c259ddf8b',
    CARD: 'e9097624991168b32224fd38ee0c63601db137c66b443c63e9085a1c91df2f93',
}

# Retain a real empty frame for the original caller's getFrame/isCreated
# lifecycle. No feed refresh, promotional widget or URL callback is installed.
QUIET_NEWS = '''
local frame
local news = {}
function news.create()
 if frame == nil then
  frame = l.frame.create("WePlayQuietNews", l.desktop.frame, 0, 0, 0, 0, l.defaultPriorities.base)
 end
end
function news.isCreated() return frame ~= nil end
function news.getFrame() return frame end
function news.update() end
function news.destroy()
 if frame ~= nil then l.frame.destroy(frame); frame = nil end
end
return news
'''


def change(raw, before, after):
    source, body, offsets = profile.decoded_view(raw)
    if body.count(before) != 1:
        raise ValueError('Gold menu anchor must match exactly once: ' + before[:70])
    at = body.index(before)
    return (source[:offsets[at]] + after + source[offsets[at + len(before)]:]).encode('utf8')


def section(raw, first, following, replacement):
    _, body, _ = profile.decoded_view(raw)
    if body.count(first) != 1 or body.count(following) != 1:
        raise ValueError('Gold menu section boundaries are ambiguous')
    begin, end = body.index(first), body.index(following)
    if end <= begin:
        raise ValueError('Gold menu section boundaries are reversed')
    return change(raw, body[begin:end], replacement)


def main_script(raw):
    raw = change(raw,
        'if l["firstStart"]()then\nl["createIntro"](l["intro1"])else\nl["createMainMenu"]()end',
        'l["createMainMenu"]()')
    raw = change(raw, 'e["menuFocalizer"]["create"]()if r then',
                 'e["menuFocalizer"]["create"]()if false then')
    raw = change(raw, '["showLastChanges"]=function()',
                 '["showLastChanges"]=function()do return end\n')
    # Preserve the five containers used by original online-state transitions,
    # without constructing buttons, labels, tooltips or external callbacks.
    empty = '\n'.join(
        'l["' + name + '"]=e.frame.create("WePlayQuietLink",l.frame,0,0,0,0,0)'
        for name in ('onlineStorePanel', 'extrasPanel', 'uplayPanel', 'fbPanel', 'myCityPanel')) + '\n'
    raw = section(raw, 'do\nlocal t=function()e["math"]["randomseed"]',
                  'e["switchCameraCB"]=function()', empty)
    return raw


def hud_script(raw):
    raw = change(raw,
        'e["fancyDialog"]["addButton"](2,45,300+l,o,"Shop",i["shopCB"],"Use your earned coins to purchase new game content.")', '')
    raw = change(raw, '(2,45,350+l,o,"TE_TemplateExchange_Caption"',
                 '(2,45,300+l,o,"TE_TemplateExchange_Caption"')
    raw = section(raw,
        'do\nlocal l=10local o=e["fancyDialog"]["create"](4,',
        'e["fancyDialog"]["updateAll"]()if e["superMenu"]["previous"]==nil then', '')
    raw = change(raw, 'if e["superMenu"]["previous"]==nil then',
                 'if e["superMenu"]["previous"]==nil or e.superMenu.previous>3 then')
    for group, label in ((2, 'Shop'), (4, 'TreasuryTitle'), (4, 'CodeInputTitle')):
        raw = change(raw, 'e["fancyDialog"]["setButtonState"](' + str(group) + ',"' + label + '",l)', '')
    return raw


def boot_script(raw):
    raw = change(raw, '["mapInMainMenu"]=true', '["mapInMainMenu"]=false')
    return change(raw, 'local function d()if e["Opts"]["noMapInMainMenu"]then',
                  'local function d()if e.Opts.mapInMainMenu then t.mapInMainMenu=true end\n'
                  'if e["Opts"]["noMapInMainMenu"]then')


def card_script(raw):
    # The original layout also measures this local before creating the card.
    raw = change(raw, 'local M=l["Shop"]["GetPlayersCashAmount"]()', 'local M=""')
    raw = section(raw, 'e["playerCash"]=l["label"]["create"](',
                  'local o,i=o-10,20', '')
    return change(raw,
        'local o=l["Shop"]["GetPlayersCashAmount"]()if o then\n'
        'l["label"]["setContent"](e["playerCash"],{o,true})end', '')


def news_script(raw):
    source, body, offsets = profile.decoded_view(raw)
    return (source[:offsets[0]] + QUIET_NEWS).encode('utf8')


def build(raw):
    if hashlib.sha256(raw).hexdigest() != profile.SOURCE:
        raise ValueError('Gold menu requires the exact original Gold archive')
    original = profile.Archive(raw)
    archive = profile.Archive(profile.build(raw))
    replacements = {}
    for name, patch in ((MAIN, main_script), (HUD, hud_script), (NEWS, news_script),
                        (BOOT, boot_script), (CARD, card_script)):
        script = original.read(name)
        if hashlib.sha256(script).hexdigest() != PINS[name]:
            raise ValueError('Unexpected original Gold menu member: ' + name)
        replacements[name] = patch(script)
    result = archive.replace(replacements)
    verified = profile.Archive(result)
    if original.entries.keys() != verified.entries.keys():
        raise ValueError('Gold menu changed the archive member set')
    for name, entry in original.entries.items():
        if entry.values[4] != 256 and verified.read(name) != replacements.get(name, archive.read(name)):
            raise ValueError('Gold menu archive verification failed: ' + name)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='Original Gold Interface.bba')
    parser.add_argument('--output', required=True, type=Path, help='New staging directory')
    args = parser.parse_args()
    result = build(args.source.read_bytes())
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / 'Interface.bba').write_bytes(result)
    report = {'profile': 'gold-quiet-menu', 'source_sha256': profile.SOURCE,
              'archive_sha256': hashlib.sha256(result).hexdigest(), 'installed': False,
              'startup_options_requires_native_adapter': True}
    (args.output / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
