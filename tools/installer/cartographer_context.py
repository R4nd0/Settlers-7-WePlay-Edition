"""Load the existing menu world before opening the fingerprinted MapForge UI.

The default simple menu stays unloaded until the Kartograph is requested.
Only the existing menu action is deferred; game simulation and transport are
untouched. This pure archive transform is used only by separate game copies.
"""
from bba import Archive
from prepare import apply_edits, sha256

OLD='38edae6dd347e9bc4afb9f3474285e8458f3d712dd058492f56bacc066ce15e4'
NEW='38ec2b865459e6af19b1291edc402e43bd37f46e783bd3c2b15d5156b42f5bcd'
TARGET='Script/local/menu/panel/menu/control/mainState.lua'


def build_archive(original,spec):
    if (not isinstance(spec,dict) or spec.get('format')!='s7-cartographer-context-v1'
            or spec.get('source_archive_sha256')!=OLD or spec.get('patched_archive_sha256')!=NEW
            or sha256(original)!=OLD):
        raise ValueError('Unsupported cartographer context source')
    files=spec.get('files')
    if (not isinstance(files,list) or len(files)!=1 or not isinstance(files[0],dict)
            or files[0].get('path')!=TARGET):
        raise ValueError('Unexpected cartographer context target')
    archive=Archive(original)
    replacement=apply_edits(archive.read(TARGET),files[0])
    patched=archive.replace({TARGET:replacement})
    if sha256(patched)!=NEW:raise ValueError('Cartographer context output hash mismatch')
    check=Archive(patched)
    if archive.entries.keys()!=check.entries.keys():raise ValueError('Archive file list changed')
    for name,entry in archive.entries.items():
        expected=replacement if name==TARGET else archive.read(name) if entry.values[4]!=256 else None
        if expected is not None and check.read(name)!=expected:
            raise ValueError('Unrelated archive member changed')
    return patched
