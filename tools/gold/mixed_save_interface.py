"""Unembedded Gold host save-button composition; native save port still required.

Preserve the original solo Save/Load branch and use the original Save callback
only for an explicitly mixed, running Gold host. No file or NEO APIs are added.
"""
from pathlib import Path
import profile_interface as profile

HERE=Path(__file__).resolve().parent
MEMBER='Script/local/menu/panel/gameMenu.lua'
SOLO='''if e["Network"]["GetGameMode"]()==1then
f,l=o(i,l,"SAVE GAME",t["actions"]["save"])f,l=o(i,l,"LOAD GAME",t["actions"]["load"])u=true
end'''
MIXED=SOLO[:-3].replace('==1then','==1 then')+'''elseif weplayCanSaveMixed() then
f,l=o(i,l,"SAVE GAME",t["actions"]["save"])u=true
end'''


def script(raw):
    source,body,offsets=profile.decoded_view(raw)
    if body.count(SOLO)!=1:raise ValueError('Gold save menu anchor mismatch')
    at=body.index(SOLO)
    source=source[:offsets[at]]+MIXED+source[offsets[at+len(SOLO)]:]
    helper='local weplayCanSaveMixed=(function(...)\n'+(HERE/'mixed_save_menu.lua').read_text(encoding='utf-8')+'\nend)(e)\n'
    return (source[:offsets[0]]+helper+source[offsets[0]:]).encode('utf-8')
