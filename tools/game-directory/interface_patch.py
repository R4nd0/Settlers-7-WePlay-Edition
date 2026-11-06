"""Hash-locked global browser extension for the verified RC3.11 interface."""
import hashlib
import importlib.util
from pathlib import Path
import sys

HERE=Path(__file__).resolve().parent
sys.path[:0]=[str(HERE.parent/'lan'),str(HERE.parent/'multiplayer-resume')]
from bba import Archive
spec=importlib.util.spec_from_file_location('weplay_multiplayer_interface',HERE.parent/'multiplayer-resume/interface_patch.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
decoded_view=module.decoded_view

SOURCE='1e76e018d79aa39712f142d1f8996a58b1a0b73f438ee709428e5db82609ade3'
MEMBER='Script/local/menu/panel/menu/play/multiplayer/sessionList.lua'
HOST='Script/local/menu/panel/menu/play/multiplayer/hostLobby.lua'


def build(raw):
    if hashlib.sha256(raw).hexdigest()!=SOURCE:raise ValueError('Global browser requires the verified multiplayer interface')
    archive=Archive(raw);source,body,offsets=decoded_view(archive.read(MEMBER))
    anchor='return a\n'
    if body.count(anchor)!=1:raise ValueError('Global browser anchor mismatch')
    at=body.index(anchor)
    fragment=';(function(...)\n'+(HERE/'session_list.lua').read_text(encoding='utf-8')+'\nend)(e,a,l)\n'
    replacement=(source[:offsets[at]]+fragment+source[offsets[at]:]).encode('utf-8')
    host_source,host_body,host_offsets=decoded_view(archive.read(HOST))
    legacy='local function X()n["Network"]["ShowInviteOverlay"]()end'
    if host_body.count(legacy)!=1:raise ValueError('WePlay invitation anchor mismatch')
    at=host_body.index(legacy)
    invitation='local X=(function(...)\n'+(HERE/'host_invitation.lua').read_text(encoding='utf-8')+'\nend)(n)'
    friend='local function T()local o=e["friendsDropdownList"]["getSelectedItemData"]()_G["assert"](o)local a=n["chatInput"]["getContent"](e["friendInviteText"])n["Network"]["InvitePlayerToSession"](o["PlayerID"],o["UPlayName"],a)n["ui"]["hide"](e["friendInviteFrame"])end'
    if host_body.count(friend)!=1:raise ValueError('WePlay friend invitation anchor mismatch')
    edits=[(at,legacy,invitation),(host_body.index(friend),friend,'local function T()n["ui"]["hide"](e["friendInviteFrame"])X()end')]
    for position,before,after in sorted(edits,reverse=True):
        host_source=host_source[:host_offsets[position]]+after+host_source[host_offsets[position+len(before)]:]
    host=host_source.encode('utf-8')
    replacements={MEMBER:replacement,HOST:host}
    result=archive.replace(replacements);verified=Archive(result)
    for name,entry in archive.entries.items():
        if entry.values[4]!=256 and verified.read(name)!=replacements.get(name,archive.read(name)):
            raise ValueError('Global browser changed an unrelated archive member')
    return result
