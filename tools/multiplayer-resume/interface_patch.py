"""Fingerprinted multiplayer checkpoint UI integration.

Builds from the known WePlay interface, not arbitrary game archives. Requires
the matching guarded native checkpoint transfer implementation.
"""
import ast
import hashlib
import json
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'lan'))
from bba import Archive

SOURCE = '38ec2b865459e6af19b1291edc402e43bd37f46e783bd3c2b15d5156b42f5bcd'


def decoded_view(raw):
    """Map decoded string-table references back to original source offsets."""
    source = raw.decode('utf-8-sig')
    names = re.match(r'local (\w+),(\w+),(\w+),(\w+)=_G', source)
    table = re.search(r'=((?:\{\{)[\d.,{}]+\}),\{\},', source)
    if not names or not table:
        raise ValueError('Unknown Lua string-table header')
    rows = ast.literal_eval(table[1].replace('{', '[').replace('}', ']'))
    seed, strings = len(rows) % 256, []
    for row in rows:
        if len(row) == 1 and row[0] >= 555:
            strings.append(row[0] - seed - 555)
        else:
            previous, chars = seed, []
            for number in row:
                char = number - previous
                chars.append(chr(char % 256))
                previous = char
            strings.append(''.join(chars))
    start = source.find('\nend\nend\n', table.end())
    if start < 0:
        raise ValueError('Unknown Lua string-table footer')
    start += 9
    pattern = re.compile(r'\b(' + re.escape(names[2]) + '|' + re.escape(names[3]) + r')\((\d+)\)')
    pieces, offsets, position = [], [], start
    for match in pattern.finditer(source, start):
        pieces.append(source[position:match.start()])
        offsets.extend(range(position, match.start()))
        value = json.dumps(strings[int(match[2]) - 1], ensure_ascii=True)
        if match[1] == names[3]:
            value = '_G[' + value + ']'
        pieces.append(value)
        offsets.extend([match.start()] * (len(value) - 1) + [match.end() - 1])
        position = match.end()
    pieces.append(source[position:])
    offsets.extend(range(position, len(source)))
    offsets.append(len(source))
    return source, ''.join(pieces), offsets


def build(raw):
    if hashlib.sha256(raw).hexdigest() != SOURCE:
        raise ValueError('Multiplayer checkpoints require the fingerprinted WePlay interface')
    archive, replacements = Archive(raw), {}

    def change(name, before, after):
        key = 'Script/local/' + name
        source, body, offsets = decoded_view(replacements.get(key, archive.read(key)))
        if body.count(before) != 1:
            raise ValueError('Checkpoint Lua anchor mismatch: ' + name)
        at = body.index(before)
        replacements[key] = (source[:offsets[at]] + after + source[offsets[at + len(before)]:]).encode('utf8')

    change('menu/panel/gameMenu.lua',
        'if e["Network"]["GetGameMode"]()==1then\nf,l=o(i,l,"SAVE GAME",t["actions"]["save"])f,l=o(i,l,"LOAD GAME",t["actions"]["load"])u=true\nend',
        '''if e.Network.GetGameMode()==1 or (e.Network.InNetworkMode() and e.Network.IsHost()) then
f,l=o(i,l,"SAVE GAME",t.actions.save)
if e.Network.GetGameMode()==1 then f,l=o(i,l,"LOAD GAME",t.actions.load) end
u=true
end''')
    for file, arguments, returned in (
        ('session_list.lua', 'e,a,l', 'a'), ('host_lobby.lua', 'n,d,e', 'd')):
        module = 'sessionList' if returned == 'a' else 'hostLobby'
        fragment = (HERE / file).read_text(encoding='utf8')
        change('menu/panel/menu/play/multiplayer/' + module + '.lua', 'return ' + returned + '\n',
            '(function(...)\n' + fragment + '\nend)(' + arguments + ')\nreturn ' + returned + '\n')
    change('menu/game.lua', 'if e["head"]["isCreated"]()then e["head"]["update"]()end',
        'local weplayHead=_G.rawget(e,"head")\nif weplayHead and weplayHead.isCreated() then weplayHead.update() end')
    change('menu/panel/loadingScreen.lua', 'n["candleProgress"]["create"](n["loadingScreen"]["frame"],e,o)',
        'local weplayCandle=_G.rawget(n,"candleProgress")\nif weplayCandle then weplayCandle.create(n.loadingScreen.frame,e,o) end\n')
    change('menu/panel/loadingScreen.lua',
        'if n["candleProgress"]["isCreated"]()then\nn["candleProgress"]["destroy"]()end\nn["candleProgress"]["destroy"]()',
        'local weplayCandle=_G.rawget(n,"candleProgress")\nif weplayCandle and weplayCandle.isCreated() then weplayCandle.destroy() end\n')
    change('menu/panel/loadingScreen.lua',
        'if n["candleProgress"]["isCreated"]()then\nn["candleProgress"]["setProgress"](e/100)end',
        'local weplayCandle=_G.rawget(n,"candleProgress")\nif weplayCandle and weplayCandle.isCreated() then weplayCandle.setProgress(e/100) end')
    change('menu/tutorial.lua',
        'if i~=nil then\ne["mapData"]=e["MapSetup"]["GetMapDataBySavegame"](i)l=e["mapData"]["MapPath"]end',
        'if i~=nil and not (i==1000 and F.currentMode()==F.mode.multiplayer) then\ne["mapData"]=e["MapSetup"]["GetMapDataBySavegame"](i)l=e["mapData"]["MapPath"]end')
    runtime = (HERE / 'runtime.lua').read_text(encoding='utf8')
    change('tools/settings.lua', 'return n\n',
        'n.weplayMPResume=(function(...)\nlocal type,math,string,table,tonumber,tostring,ipairs,pairs = _G.type,_G.math,_G.string,_G.table,_G.tonumber,_G.tostring,_G.ipairs,_G.pairs\n'
        + runtime + '\nend)(e,l,n.store)\nreturn n\n')
    change('menu/panel/menu/play/multiplayer/hostLobby.lua', 'n["Network"]["StartGame"]()',
        'n.require("local.tools.settings").weplayMPResume.rememberLobby()\nn["Network"]["StartGame"]()')
    change('menu/game.lua',
        'e["game"]["pickle"]()e["state"]["save"](l["slot"],l["name"])e["game"]["lastSaveTime"]=',
        '''e["game"]["pickle"]()
if e.Network.InNetworkMode() and e.Network.IsHost() and not G.weplayMPResume.cacheSave(l.slot,l.name) then
e.ui.messageBox("WePlay", "Die Mehrspieler-Sicherung konnte nicht vorbereitet werden. Bitte erneut manuell speichern.")
return
end
e["state"]["save"](l["slot"],l["name"])
if e.Network.InNetworkMode() and e.Network.IsHost() and not G.weplayMPResume.saved(l.slot,l.name) then
e.ui.messageBox("WePlay", "Die Sicherung konnte nicht zum Fortsetzen freigegeben werden. Bitte erneut manuell speichern.")
end
e["game"]["lastSaveTime"]=''')
    change('menu/game.lua', 'l["started"]=true', 'G.weplayMPResume.resetAutosave()\nl["started"]=true')
    change('menu/game.lua', 'e["game"]["started"]=false', 'G.weplayMPResume.resetAutosave()\ne["game"]["started"]=false')
    change('menu/game.lua', 'e["game"]["updateDefaultSystems"]()e["game"]["updateModules"]()',
        '''e["game"]["updateDefaultSystems"]()
G.weplayMPResume.tickHostAutosave(e.Game.GetGameTime(),
    e.game.started and not e.game.isDisconnected and not e.game.exitTriggerTime
    and not e.game.lastDesyncTurn
    and (not e.gameEnd or not e.gameEnd.isCreated() or not e.gameEnd.state
        or e.gameEnd.state~=e.gameEnd.states.defeat), e.game.autoSave)
e["game"]["updateModules"]()''')
    result = archive.replace(replacements)
    verified = Archive(result)
    if archive.entries.keys() != verified.entries.keys():
        raise ValueError('Checkpoint archive member set changed')
    for name, entry in archive.entries.items():
        if entry.values[4] != 256 and verified.read(name) != replacements.get(name, archive.read(name)):
            raise ValueError('Checkpoint archive verification failed: ' + name)
    return result
