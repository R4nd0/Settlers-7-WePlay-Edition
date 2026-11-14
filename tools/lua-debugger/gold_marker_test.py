"""Temporary fixture through the real original Gold core-script loader.

Only the closed, prepared work-copy archive is changed. Restoration uses a
content comparison and refuses to overwrite an unexpected newer archive.
"""
import hashlib
import os
from pathlib import Path
import shutil
import sys
import tempfile

CORE='Script/local/core.lua'
ARCHIVE_SHA='98cda030b82f94bcdb9ba8333ee8494831c3cf8ed8d26d9163d93060e5f76b92'
CORE_SHA='e77edf7025c266df86b3c7dba934f1b01e1cb3a7c6b76522a194cfb242d4eec7'
digest=lambda value:hashlib.sha256(value).hexdigest()


def build(raw):
    sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'lan'))
    from bba import Archive
    if len(raw)>8*1024*1024 or digest(raw)!=ARCHIVE_SHA:raise ValueError('Gold marker fixture requires the reviewed prepared archive')
    original=Archive(raw);core=original.read(CORE)
    if digest(core)!=CORE_SHA:raise ValueError('Gold original core script mismatch')
    fixture=Path(__file__).with_name('gold_marker_fixture.lua').read_bytes()
    replacement=core+b'\n;(function(...)\n'+fixture+b'\nend)(e["UI"])\n'
    result=original.replace({CORE:replacement});checked=Archive(result)
    if original.entries.keys()!=checked.entries.keys():raise ValueError('Gold fixture changed member names')
    for name,entry in original.entries.items():
        if entry.values[4]!=256 and checked.read(name)!=(replacement if name==CORE else original.read(name)):
            raise ValueError('Gold fixture changed an unrelated payload')
    return result


class ArchiveFixture:
    def __init__(self,root,output):
        repo=Path(__file__).resolve().parents[2]
        root=Path(root).resolve();output=Path(output).resolve()
        if not root.is_relative_to(repo/'work') or not output.is_relative_to(repo/'work'):
            raise ValueError('Keep diagnostic game and backups under work/')
        sys.path.insert(0,str(repo/'tools/installer'))
        import compatibility as content
        self.path=content.plain(root/'Data/Base/BBA/Interface.bba')
        if self.path.stat().st_size>8*1024*1024:raise ValueError('Gold diagnostic archive exceeds size bound')
        self.backup=output/'Interface-before.private.bba'
        if self.backup.exists():raise ValueError('Archive backup already exists')
        self.before=self.path.read_bytes();self.after=build(self.before);self.active=False
        shutil.copy2(self.path,self.backup)

    def replace(self,value):
        temporary=None
        try:
            with tempfile.NamedTemporaryFile(dir=self.path.parent,delete=False) as stream:
                temporary=Path(stream.name);stream.write(value);stream.flush();os.fsync(stream.fileno())
            os.replace(temporary,self.path)
        finally:
            if temporary:temporary.unlink(missing_ok=True)

    def install(self):
        if digest(self.path.read_bytes())!=digest(self.before):raise ValueError('Archive changed before fixture staging')
        self.active=True;self.replace(self.after)
        if digest(self.path.read_bytes())!=digest(self.after):raise ValueError('Fixture write failed')

    def restore(self):
        if not self.active:return True
        current=digest(self.path.read_bytes())
        if current not in (digest(self.before),digest(self.after)):
            raise ValueError('Archive changed unexpectedly; newer bytes and diagnostic backup retained')
        if current!=digest(self.before):self.replace(self.before)
        shutil.copystat(self.backup,self.path)
        if digest(self.path.read_bytes())!=digest(self.before):raise ValueError('Gold archive restoration failed')
        self.active=False;return True
