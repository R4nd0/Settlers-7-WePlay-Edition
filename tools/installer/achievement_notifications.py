"""Suppress achievement popup creation in one fingerprinted UI factory.

This pure transform changes no native code, achievement catalogue, rewards,
profile progress, network path or simulation. Only separate prepared copies may
consume its result; the original game archive is never written here.
"""
from bba import Archive
from prepare import apply_edits, sha256

OLD='6966c878749f668de12b13126d971958bf1ba8e5952e5d7f2b224819dcd877eb'
NEW='38edae6dd347e9bc4afb9f3474285e8458f3d712dd058492f56bacc066ce15e4'
TARGET='Script/local/menu/panel/shared/achievement.lua'


def build_archive(original,spec):
    if (not isinstance(spec,dict) or spec.get('format')!='s7-achievement-notifications-v1'
            or spec.get('source_archive_sha256')!=OLD or spec.get('patched_archive_sha256')!=NEW
            or sha256(original)!=OLD):
        raise ValueError('Unsupported achievement notification source')
    files=spec.get('files')
    if (not isinstance(files,list) or len(files)!=1 or not isinstance(files[0],dict)
            or files[0].get('path')!=TARGET):
        raise ValueError('Unexpected achievement notification target')
    archive=Archive(original)
    replacement=apply_edits(archive.read(TARGET),files[0])
    patched=archive.replace({TARGET:replacement})
    if sha256(patched)!=NEW:raise ValueError('Achievement notification output hash mismatch')
    check=Archive(patched)
    if archive.entries.keys()!=check.entries.keys():raise ValueError('Archive file list changed')
    for name,entry in archive.entries.items():
        expected=replacement if name==TARGET else archive.read(name) if entry.values[4]!=256 else None
        if expected is not None and check.read(name)!=expected:
            raise ValueError('Unrelated archive member changed')
    return patched
