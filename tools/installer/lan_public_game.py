"""Hash-locked LAN creation UI delta; server privacy checks stay unchanged.

WePlay multiplayer uses the local service even when the engine's legacy LAN
boolean is false. Both flags must therefore obey this package's public-to-LAN
creation policy. This never modifies the original game installation.
"""
from bba import Archive
from prepare import apply_edits, sha256

OLD='2b1ad77ab1afda98e9d9aab8bbc91279607e1bbbac506d2b52de675550de08ad'
NEW='6966c878749f668de12b13126d971958bf1ba8e5952e5d7f2b224819dcd877eb'
TARGETS=frozenset('Script/local/menu/panel/menu/play/multiplayer/'+name+'.lua'
                  for name in ('sessionCreation','hostLobby'))


def build_archive(original,spec):
    if (not isinstance(spec,dict) or spec.get('format')!='s7-lan-public-creation-v1'
            or spec.get('source_archive_sha256')!=OLD or spec.get('patched_archive_sha256')!=NEW
            or sha256(original)!=OLD):
        raise ValueError('Unsupported LAN creation menu source')
    files=spec.get('files')
    if (not isinstance(files,list) or len(files)!=len(TARGETS) or any(not isinstance(item,dict) for item in files)
            or {item.get('path') for item in files}!=TARGETS):
        raise ValueError('Unexpected LAN creation menu targets')
    archive=Archive(original)
    replacements={item['path']:apply_edits(archive.read(item['path']),item) for item in files}
    patched=archive.replace(replacements)
    if sha256(patched)!=NEW:raise ValueError('LAN creation menu output hash mismatch')
    check=Archive(patched)
    if archive.entries.keys()!=check.entries.keys():raise ValueError('Archive file list changed')
    for name,entry in archive.entries.items():
        if entry.values[4]!=256 and check.read(name)!=replacements.get(name,archive.read(name)):
            raise ValueError('Unrelated archive member changed')
    return patched
