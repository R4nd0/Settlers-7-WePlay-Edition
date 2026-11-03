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

SOURCE='be910c1ddad7a2857665869f04503f0f2c891bd5d071ea633a9f49c719e717d6'
MEMBER='Script/local/menu/panel/menu/play/multiplayer/sessionList.lua'


def build(raw):
    if hashlib.sha256(raw).hexdigest()!=SOURCE:raise ValueError('Global browser requires the verified multiplayer interface')
    archive=Archive(raw);source,body,offsets=decoded_view(archive.read(MEMBER))
    anchor='return a\n'
    if body.count(anchor)!=1:raise ValueError('Global browser anchor mismatch')
    at=body.index(anchor)
    fragment=';(function(...)\n'+(HERE/'session_list.lua').read_text(encoding='utf-8')+'\nend)(e,a,l)\n'
    replacement=(source[:offsets[at]]+fragment+source[offsets[at]:]).encode('utf-8')
    result=archive.replace({MEMBER:replacement});verified=Archive(result)
    for name,entry in archive.entries.items():
        if entry.values[4]!=256 and verified.read(name)!=(replacement if name==MEMBER else archive.read(name)):
            raise ValueError('Global browser changed an unrelated archive member')
    return result
