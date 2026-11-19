"""Compose mixed-room menus from the verified ordinary Gold and History UI."""
import hashlib
import importlib.util
from pathlib import Path
import sys

HERE=Path(__file__).resolve().parent
sys.path[:0]=[str(HERE),str(HERE.parent/'lan')]
from bba import Archive
from profile_interface import decoded_view as gold_decode

spec=importlib.util.spec_from_file_location('mixed_history_decoder',HERE.parent/'multiplayer-resume/interface_patch.py')
SOURCES={
 'gold':'9f8e4c7a4d26732dccff437df09028656c7dd198bc1ce065e243016fd560f928',
 'history':'b58db2d87938727de09cd75256afccbba4e2d4932bccb2ddf0ac0b2684503f16',
}
HISTORY_MIDSUMMER = '1b9e17d711d3ee7b4e36f72d690e89e3b789d1174148a9337c7cbdd67af75e67'
MAIN='Script/local/menu/panel/menu/control/mainState.lua'
BROWSER='Script/local/menu/panel/menu/play/multiplayer/sessionList.lua'


def fragment(name):
    return (HERE/name).read_text(encoding='utf-8')


def insert(raw,decode,anchor,addition,*,after=False):
    source,body,offsets=decode(raw)
    if body.count(anchor)!=1:raise ValueError('Mixed UI anchor mismatch')
    at=body.index(anchor)+(len(anchor) if after else 0)
    return (source[:offsets[at]]+addition+source[offsets[at]:]).encode('utf-8')


def main_fragment(edition):
    if edition not in SOURCES:raise ValueError('Unknown mixed UI edition')
    mode='host' if edition=='gold' else 'guest'
    prepare=('function()return false end' if edition=='gold' else
             '(function(...)\n'+fragment('mixed_guest_prepare.lua')+'\nend)(e)')
    present=('function()return false end' if edition=='gold' else
             '(function(...)\n'+fragment('mixed_guest_lobby.lua')+'\nend)(e,l)')
    room='function(g,state,mode)\n return (function(...)\n'+fragment('mixed_room_menu.lua')+(
        '\nend)(g,state,mode,'+('true' if edition=='gold' else 'false')+')\nend')
    gold_room_ai = ('\nif l and e.UI.WriteToConsole("weplay-crossplay-v1:mode")=="room-guest" then\n'
                    ' if not l.weplayGoldRoomAI then\n'
                    '  l.weplayGoldRoomAI=(function(...)\n' + fragment('room_ai_guest.lua') +
                    '\nend)(e)\n end\n l.weplayGoldRoomAI()\nend\n') if edition=='gold' else ''
    return '''
if l and not l.weplayMixedMenuBuilding then
 if not l.weplayMixedMenu then
  l.weplayMixedMenuBuilding=true
  l.weplayMixedMenu=(function(...)
'''+fragment('mixed_main.lua')+'''
end)(e,l,"'''+mode+'''",function(...)
'''+fragment('mixed_menu.lua')+'''
end,'''+prepare+''','''+present+''','''+room+''')
  l.weplayMixedMenuBuilding=false
 end
 l.weplayMixedMenu.tick()
end
'''+gold_room_ai


def saved_start_fragment():
    return '\nl=(function(...)\n'+fragment('mixed_saved_start.lua')+'\nend)(e,l)\n'


def build(raw,edition,*,history_decoder=None):
    expected=(SOURCES['history'],HISTORY_MIDSUMMER) if edition=='history' else (SOURCES.get(edition),)
    if hashlib.sha256(raw).hexdigest() not in expected:
        raise ValueError('Mixed UI requires the exact normal production input')
    if edition=='gold':
        decode=gold_decode
    else:
        if history_decoder is None:
            history_decoder=importlib.util.module_from_spec(spec);spec.loader.exec_module(history_decoder)
        decode=history_decoder.decoded_view
    archive=Archive(raw)
    changes={MAIN:insert(archive.read(MAIN),decode,'l["moduleManager"]["update"]()',
                         main_fragment(edition),after=True)}
    changes[MAIN]=insert(changes[MAIN],decode,'i["restartWithMultiplayer"](l)',saved_start_fragment())
    if edition=='gold':
        import neo_runtime_interface
        changes.update(neo_runtime_interface.replacements(archive))
        changes[MAIN]=insert(changes[MAIN],decode,'i["restartWithMultiplayer"](l)',
            '\nlocal weplayMode=e.UI.WriteToConsole("weplay-crossplay-v1:mode")\n'
            'if weplayMode=="host" or weplayMode=="guest" or weplayMode=="room-host" or weplayMode=="room-guest" then\n'
            ' _G.assert(e.require("local.tools.settings").weplayNeoLogistics.acceptNetworkStart(),'
            '"NEO-Logistics native start event rejected")\nend\n')
        changes[BROWSER]=insert(archive.read(BROWSER),decode,'return f\n',
            '\n;(function(...)\n'+fragment('mixed_browser.lua')+'\nend)(e,f,l,h,D)\n'
            '\n;(function(...)\n'+fragment('mixed_guest_browser.lua')+'\nend)(e,f,l,h)\n')
    else:
        changes[BROWSER]=insert(archive.read(BROWSER),decode,'return a\n',
            '\n;(function(...)\n'+fragment('mixed_guest_browser.lua')+'\nend)(e,a,l,r)\n')
    result=archive.replace(changes)
    verified=Archive(result)
    if archive.entries.keys()!=verified.entries.keys():raise ValueError('Mixed UI member set changed')
    for name,entry in archive.entries.items():
        if entry.values[4]!=256 and verified.read(name)!=changes.get(name,archive.read(name)):
            raise ValueError('Mixed UI changed an unrelated member')
    return result
