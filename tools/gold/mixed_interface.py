"""Pinned production UI routing candidate; does not enable mixed capability.

Only menu routing and applied-setup guest presentation are composed here;
no laboratory drivers, Ready/Start actions or NEO API are embedded.
The ordinary installer does not call this candidate builder yet.
"""
import hashlib
import importlib.util
from pathlib import Path
import sys

HERE=Path(__file__).resolve().parent
sys.path[:0]=[str(HERE),str(HERE.parent/'lan')]
from bba import Archive
from profile_interface import decoded_view as gold_decode

spec=importlib.util.spec_from_file_location('mixed_history_decoder',HERE.parent/'multiplayer-resume/interface_patch.py')
history=importlib.util.module_from_spec(spec);spec.loader.exec_module(history)
SOURCES={
 'gold':'9f8e4c7a4d26732dccff437df09028656c7dd198bc1ce065e243016fd560f928',
 'history':'2fbf4a98a89ac735b3930995a1ff92331eb4d02958e46bf580458d0f4c899f68',
}
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
    return '''
if l and not l.weplayMixedMenuBuilding then
 if not l.weplayMixedMenu then
  l.weplayMixedMenuBuilding=true
  l.weplayMixedMenu=(function(...)
'''+fragment('mixed_main.lua')+'''
end)(e,l,"'''+mode+'''",function(...)
'''+fragment('mixed_menu.lua')+'''
end,'''+prepare+''','''+present+''')
  l.weplayMixedMenuBuilding=false
 end
 l.weplayMixedMenu.tick()
end
'''


def saved_start_fragment():
    return '\nl=(function(...)\n'+fragment('mixed_saved_start.lua')+'\nend)(e,l)\n'


def build(raw,edition):
    if edition not in SOURCES or hashlib.sha256(raw).hexdigest()!=SOURCES[edition]:
        raise ValueError('Mixed UI requires the exact normal production input')
    decode=gold_decode if edition=='gold' else history.decoded_view
    archive=Archive(raw)
    changes={MAIN:insert(archive.read(MAIN),decode,'l["moduleManager"]["update"]()',
                         main_fragment(edition),after=True)}
    changes[MAIN]=insert(changes[MAIN],decode,'i["restartWithMultiplayer"](l)',saved_start_fragment())
    if edition=='gold':
        changes[BROWSER]=insert(archive.read(BROWSER),decode,'return f\n',
            '\n;(function(...)\n'+fragment('mixed_browser.lua')+'\nend)(e,f,l,h,D)\n')
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
