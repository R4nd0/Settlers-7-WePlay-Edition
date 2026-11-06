"""Fingerprint-locked per-map NEO-Logistics integration; no game assets bundled."""
import hashlib
import importlib.util
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'lan'))
from bba import Archive

spec = importlib.util.spec_from_file_location('weplay_neo_decoder', HERE.parent / 'multiplayer-resume/interface_patch.py')
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)
SOURCE = '6541f49185b0e37835fd1187378d09a001015046e5b667e7921bc7ae282a0223'
SOURCE_BASE='1e76e018d79aa39712f142d1f8996a58b1a0b73f438ee709428e5db82609ade3'

def build(raw):
    if hashlib.sha256(raw).hexdigest() not in (SOURCE,SOURCE_BASE):
        raise ValueError('NEO-Logistics requires the verified WePlay interface')
    archive, replacements = Archive(raw), {}
    def change(member, before, after):
        source, body, offsets = decoder.decoded_view(replacements.get(member, archive.read(member)))
        if body.count(before) != 1:
            raise ValueError('NEO-Logistics anchor mismatch: ' + member)
        at = body.index(before)
        replacements[member] = (source[:offsets[at]] + after + source[offsets[at+len(before)]:]).encode('utf8')
    def fragment(name): return (HERE / name).read_text(encoding='utf8')
    change('Script/local/tools/settings.lua', 'return n\n',
        'n.weplayNeoLogistics=(function(...)\n' + fragment('runtime.lua') + '\nend)(e,l,n.store,n.weplayMPResume)\nreturn n\n')
    change('Script/local/menu/panel/menu/play/singleplayer/mapSetup.lua', 'return i\n',
        ';(function(...)\n' + fragment('map_setup.lua') + '\nend)(n,i,function() return e end)\nreturn i\n')
    member='Script/local/menu/panel/menu/play/multiplayer/sessionCreation.lua'
    source,body,_=decoder.decoded_view(archive.read(member))
    if not body.endswith('return l\n'):
        raise ValueError('NEO-Logistics session creation API anchor mismatch')
    change(member,'local l={}function l.create','local weplayNeoGlobal=l\nlocal l={}function l.create')
    change(member,'return l\n',';(function(...)\n'+fragment('session_creation.lua')+'\nend)(weplayNeoGlobal,l,function() return e end)\nreturn l\n')
    for panel,glob,api,state in (('hostLobby','n','d','e'),('joinLobby','l','c','o')):
        member='Script/local/menu/panel/menu/play/multiplayer/'+panel+'.lua'
        change(member,'return '+api+'\n',';(function(...)\n'+fragment('lobby_ui.lua')+'\nend)('+glob+','+api+',function() return '+state+' end)\nreturn '+api+'\n')
    member='Script/local/menu/panel/menu/play/multiplayer/joinLobby.lua'
    change(member,'local function X()local e=l["Network"]["GetLocalPlayerSlot"]()',
        'local function X()if not l.require("local.tools.settings").weplayNeoLogistics.networkReady() then return end local e=l["Network"]["GetLocalPlayerSlot"]()')
    member='Script/local/menu/panel/menu/play/multiplayer/sessionList.lua'
    change(member,'    local savePath = G.Game.GetSaveGameFileName(slot)',
        '    if not G.require("local.tools.settings").weplayNeoLogistics.selectSaved(slot) then\n'
        '        G.ui.messageBox("NEO-Logistics", "Die Logistikeinstellung des Spielstands konnte nicht uebernommen werden.");return\n'
        '    end\n    local savePath = G.Game.GetSaveGameFileName(slot)')
    # This branch is reached only for the engine's real start event while a
    # lobby is active. It preserves native readiness/event delivery and locks
    # the already received match choice before the local Lua state is recreated.
    change('Script/local/menu/panel/menu/control/mainState.lua',
        'local n=i["data"]["savegame"]L(n)',
        'local n=i["data"]["savegame"];'
        '_G.assert(e.require("local.tools.settings").weplayNeoLogistics.acceptNetworkStart(),'
        '"NEO-Logistics native start event rejected");L(n)')
    member = 'Script/local/menu/game.lua'
    source, _, _ = decoder.decoded_view(archive.read(member))
    replacements[member] = (source + '\n' +
        'e.require("local.tools.settings").weplayNeoLogistics.hookGame(e.game,j)\n').encode('utf8')
    member = 'Script/local/menu/panel/logisticsSystem.lua'
    source, _, _ = decoder.decoded_view(archive.read(member))
    replacements[member] = (source + '\n;(function(...)\n' + fragment('logistics_ui.lua') + '\nend)(e)\n').encode('utf8')
    original = 'e["UI"]["GetTextFromLoca"]("prioritize tasks or panels",nil,"LOCA_Logistics_Panel")'
    change(member, original, '(e.require("local.tools.settings").weplayNeoLogistics.active() and "Einzelne Auftraege priorisieren; Gruppenprioritaeten deaktiviert" or ' + original + ')')
    result = archive.replace(replacements)
    verified = Archive(result)
    for name, entry in archive.entries.items():
        if entry.values[4] != 256 and verified.read(name) != replacements.get(name, archive.read(name)):
            raise ValueError('NEO-Logistics changed an unrelated archive member')
    return result
