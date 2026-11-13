"""Installer-only package verification and bounded retirement of an old backup.

Executed from Setup's extracted script, never from the old package. Verification
checks every byte before importing any tool from the staged package.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import sys


def plain(path):
    path=Path(os.path.abspath(path))
    for part in (path,*path.parents):
        try:info=part.lstat()
        except FileNotFoundError:continue
        if stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0)&0x400:
            raise ValueError('Linked package path')
    return path


def digest(path):
    with plain(path).open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def manifest(path):
    path=plain(path)
    if path.stat().st_size>4*1024*1024:raise ValueError('Package manifest too large')
    value=json.loads(path.read_text(encoding='utf-8'))
    files=value.get('files')
    if value.get('format')!='s7-patch-package-v1' or not isinstance(files,dict) or not files or len(files)>30000:
        raise ValueError('Invalid package manifest')
    for name,hashed in files.items():
        if (not isinstance(name,str) or name!=name.lower() or '\\' in name or ':' in name
                or name.startswith('/') or any(p in ('','.','..') for p in name.split('/'))
                or name.split('/')[0] not in ('payload','runtime','tools')
                or not isinstance(hashed,str) or not re.fullmatch('[a-f0-9]{64}',hashed)):
            raise ValueError('Invalid package entry')
    return value


def verify(package,expected):
    package=plain(package);expected=plain(expected)
    if digest(package/'package.json')!=digest(expected):raise ValueError('Unexpected staged package manifest')
    value=manifest(expected);actual={}
    for name in ('Payload','Runtime','Tools'):
        folder=plain(package/name)
        if not folder.is_dir():raise ValueError('Incomplete package')
        stack=[folder]
        while stack:
            for path in stack.pop().iterdir():
                path=plain(path)
                if path.is_dir():stack.append(path)
                elif path.is_file():
                    relative=path.relative_to(package).as_posix().lower()
                    if relative in actual or len(actual)>=30000:raise ValueError('Duplicate package entry')
                    actual[relative]=digest(path)
                else:raise ValueError('Invalid package file')
    if actual!=value['files']:raise ValueError('Staged package is incomplete or modified')
    sys.path.insert(0,str(package/'Tools/installer'))
    import install
    install.package_manifest(package)


def retire(app,backup):
    """Delete only old manifest-pinned, unchanged files in an exact backup slot."""
    app,backup=plain(app),plain(backup)
    if (backup.parent!=plain(app/'State/PackageBackups')
            or not re.fullmatch(r'\.weplay-package-[a-f0-9]{32}(?:\.previous)?',backup.name)):
        raise ValueError('Invalid package backup')
    record=backup/'package.json';before=digest(record);value=manifest(record)
    removed=0;kept=0
    for name,hashed in value['files'].items():
        path=plain(backup/name)
        if path.is_file():
            if digest(path)==hashed:
                try:path.unlink();removed+=1
                except OSError:kept+=1
            else:kept+=1
    if kept==0 and digest(record)==before:record.unlink()
    for directory in sorted((p for p in backup.rglob('*') if p.is_dir()),key=lambda p:len(p.parts),reverse=True):
        plain(directory)
        try:directory.rmdir()
        except OSError:pass
    try:backup.rmdir()
    except OSError:pass
    return {'removed':removed,'kept_modified_or_busy':kept,'remaining':backup.exists()}


def stage(package,expected,app,target):
    app,target=plain(app),plain(target)
    if target.parent!=app or not re.fullmatch(r'\.weplay-package-[a-f0-9]{32}',target.name):
        raise ValueError('Invalid staging slot')
    verify(package,expected)
    value=manifest(expected)
    import release_policy
    release_policy.check_package(app,value)
    names=['package.json',*value['files']]
    # Manifests use lowercase keys, but runtime notice inventories preserve
    # the on-disk spelling (GPL-3.0.txt, etc.) even on Windows.
    paths={'package.json':plain(package/'package.json')}
    for folder in ('Payload','Runtime','Tools'):
        for path in (package/folder).rglob('*'):
            path=plain(path)
            if path.is_file():paths[path.relative_to(package).as_posix().lower()]=path
    required=sum(paths[name].stat().st_size for name in names)+64*1024*1024
    if shutil.disk_usage(app).free<required:raise ValueError('Insufficient package staging space')
    target.mkdir()
    for name in names:
        src=paths[name];dst=plain(target/src.relative_to(package));dst.parent.mkdir(parents=True,exist_ok=True)
        with src.open('rb') as source,dst.open('xb') as output:shutil.copyfileobj(source,output,1024*1024)
    verify(target,expected)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package',type=Path)
    parser.add_argument('--expected',type=Path)
    parser.add_argument('--retire',type=Path)
    parser.add_argument('--app',type=Path)
    parser.add_argument('--stage-to',type=Path)
    args=parser.parse_args()
    try:
        if args.retire is not None:
            if args.app is None or args.package is not None or args.expected is not None or args.stage_to is not None:raise ValueError('Invalid arguments')
            print(json.dumps(retire(args.app,args.retire)))
        else:
            if args.package is None or args.expected is None:raise ValueError('Invalid arguments')
            if args.stage_to is not None:
                if args.app is None:raise ValueError('Invalid arguments')
                stage(args.package,args.expected,args.app,args.stage_to)
            else:
                if args.app is not None:raise ValueError('Invalid arguments')
                verify(args.package,args.expected)
            print('Verified complete staged package')
        return 0
    except (OSError,ValueError,TypeError,KeyError,ImportError) as error:
        print('Package check failed: '+str(error));return 2


if __name__=='__main__':raise SystemExit(main())
