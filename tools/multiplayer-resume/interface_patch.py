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


def replace_lua(raw, before, after):
    source, body, offsets = decoded_view(raw)
    if body.count(before) != 1:
        raise ValueError('Board Lua anchor mismatch: ' + before[:80])
    at = body.index(before)
    return (source[:offsets[at]] + after + source[offsets[at + len(before)]:]).encode('utf8')


LAYOUT_READY = '''local function weplayLayoutReady(layout, slots)
    if _G.type(layout) ~= "table" or _G.type(layout.nodes) ~= "table"
            or _G.type(slots) ~= "table" or _G.next(slots) == nil then return false end
    if l.misc.getTableCount(layout.nodes) ~= l.misc.getTableCount(slots) then return false end
    for key in _G.pairs(slots) do
        local node = layout.nodes[key]
        if _G.type(node) ~= "table" or _G.type(node.x) ~= "number" or _G.type(node.y) ~= "number" then
            return false
        end
    end
    return true
end
'''


def guard_research_panel(raw):
    raw = replace_lua(raw,
        '''local r=l["Technology"]["GetSlots"](l["GUI"]["GetCurrentPlayer"]())local u=l["misc"]["getTableCount"](r)if u==0then
return
end''', LAYOUT_READY + '''local function weplayCreate()
local r=l.Technology.GetSlots(l.GUI.GetCurrentPlayer())
if not weplayLayoutReady(a.techBoardLayout, r) then return end
local u=l.misc.getTableCount(r)
''')
    raw = replace_lua(raw, '''return n
end
local i=nil''', '''return n
end
local ok, board = _G.pcall(weplayCreate)
if not ok then
    if e.frame then p() end
    return nil
end
return board
end
local i=nil''')
    return replace_lua(raw,
        '''l["Sound"]["PlayAsOneshot"](l["Sounds"]["UIMenuBigOpen"])i=o()end
else''', '''i=o()
if i then l.Sound.PlayAsOneshot(l.Sounds.UIMenuBigOpen) end
end
return i ~= nil
else''')


def guard_trade_panel(raw):
    raw = replace_lua(raw, 'if not l["misc"]["getTableCount"](i["tradeBoardLayout"]["nodes"])==a then',
                      'if l.misc.getTableCount(i.tradeBoardLayout.nodes) ~= a then')
    raw = replace_lua(raw, '''local function T(e)local t,c=l["system"]["getScreenSize"]()''', LAYOUT_READY + '''local function T(e)
a()
local layout = i.tradeBoardLayout
if not weplayLayoutReady(layout, d.staticTradeSlots) or d.startLocation == nil then return false end
local image = layout.backgroundImage
if _G.type(image) ~= "table" or _G.type(image.name) ~= "string"
        or _G.type(image.w) ~= "number" or _G.type(image.h) ~= "number"
        or image.w <= 0 or image.h <= 0 then return false end
local t,c=l.system.getScreenSize()''')
    raw = replace_lua(raw, 'a()C(e)e["graph"]=y(e)', 'C(e)e.graph=y(e)')
    raw = replace_lua(raw, '''h(e["mainFrame"])e["blinkImages"]["updateAvailability"]()end
end''', '''h(e.mainFrame)e.blinkImages.updateAvailability()
return true
end
return false
end''')
    _, body, _ = decoded_view(raw)
    cleanup = next(line for line in body.splitlines() if line.startswith('local function v()_G["assert"](e["mainFrame"])'))
    raw = replace_lua(raw, cleanup, '''local weplayHandlers = {}
local function weplayRegister(kind, handler)
    t.addEventHandler(kind, handler)
    weplayHandlers[#weplayHandlers + 1] = kind
end
local function v()
    if e.mainFrame then l.frame.destroy(e.mainFrame) end
    for _, kind in _G.ipairs(weplayHandlers) do t.removeEventHandler(kind, a) end
    e=nil''')
    _, body, _ = decoded_view(raw)
    creation = next(line for line in body.splitlines() if line.startswith('T(e)t["addEventHandler"]'))
    raw = replace_lua(raw, creation, '''local function weplayCreate()
if not T(e) then return end
''' + creation.removeprefix('T(e)').replace('t["addEventHandler"]', 'weplayRegister'))
    raw = replace_lua(raw, '''return e
end
local e={}function e.isCreated()return u~=nil''', '''return e
end
local ok, board = _G.pcall(weplayCreate)
if not ok or not board then
    v()
    return nil
end
return board
end
local e={}function e.isCreated()return u~=nil''')
    raw = replace_lua(raw, '''function e.show(l)if e["isCreated"]()then
u["show"](l)end
end''', '''function e.show(visible)
    if visible and not u then u=p() end
    if not u then return false end
    local ok = _G.pcall(u.show, visible)
    if not ok then e.destroy() end
    return ok
end''')
    return replace_lua(raw, 'return l["misc"]["getTableCount"](e)>0end',
                       'return _G.type(e)=="table" and l.misc.getTableCount(e)>0 end')


def guard_toolbar(raw):
    return replace_lua(raw, '''l["bottomBar"]["activePanel"]["show"](true)if(l["bottomBar"]["clickedButtonKey"]==t)then''',
        '''local panel = l.bottomBar.activePanel
local opened = panel.show(true)
if (panel == t or panel == o) and opened == false then
    l.bottomBar.activePanel = nil
    l.bottomBar.preferredGameInfoMode = l.gameInfo.modes.default
elseif(l.bottomBar.clickedButtonKey==t)then''')


def guard_boards(raw):
    archive = Archive(raw)
    changes = {
        'Script/local/menu/panel/' + name: patch(archive.read('Script/local/menu/panel/' + name))
        for name, patch in (('techBoardPanel.lua', guard_research_panel), ('tradeBoard.lua', guard_trade_panel),
                            ('bottomBar.lua', guard_toolbar))
    }
    return archive.replace(changes)


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
        ('session_list.lua', 'e,a,function() return l end', 'a'),
        ('host_lobby.lua', 'n,d,function() return e end', 'd')):
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
