"""Copy only owned game saves/settings when preparing a new Linux generation.

No Wine registry, Steam account files, browser data or transient network files
are imported. Wine/Proton creates a fresh prefix for the new game copy.
"""
from pathlib import Path
import json
import os
import shutil
import uuid

import compatibility

RECORDS = ('local-profile-id.txt', 'player-settings.json', 'linux-runtime.json')
FOLDERS = ('saves', 'Profiles', 'Savegames')
DOCUMENT_GAMES = frozenset(('settlers7', 'the settlers 7', 'the settlers 7 history edition',
                            'the settlers 7 - history edition', 'die siedler 7', 'weplay'))


def document_roots(root):
    """Wine can create real Documents directories instead of home symlinks."""
    home = compatibility.plain(root / 'LocalData/WineHome/Documents')
    result = [home] if home.is_dir() else []
    for layout in ('WinePrefix/drive_c/users', 'ProtonData/pfx/drive_c/users'):
        users = compatibility.plain(root / layout)
        if not users.is_dir(): continue
        for user in users.iterdir():
            user = compatibility.plain(user)
            if not user.is_dir(): continue
            for name in ('Documents', 'My Documents'):
                documents = user / name
                if documents.is_symlink():
                    # The ordinary Wine alias is already covered. Never follow
                    # an arbitrary user's documents or a foreign prefix.
                    target = documents.resolve()
                    if target == home or target == user / 'Documents': continue
                    raise ValueError('Wine documents point outside the owned profile')
                documents = compatibility.plain(documents)
                if documents.is_dir() and documents not in result: result.append(documents)
    return result


def copy_tree(source, destination):
    source, destination = map(compatibility.plain, (source, destination))
    files = []; stack = [source]
    while stack:
        for item in stack.pop().iterdir():
            item = compatibility.plain(item)
            if item.is_dir(): stack.append(item)
            elif item.is_file(): files.append(item)
            else: raise ValueError('Unsupported file in owned game profiles')
    for item in files:
        target = compatibility.plain(destination / item.relative_to(source))
        target.parent.mkdir(parents=True,exist_ok=True)
        if target.exists(): raise ValueError('Profile destination already exists')
        expected = compatibility.digest(item)
        shutil.copy2(item,target)
        if compatibility.digest(target) != expected or compatibility.digest(item) != expected:
            raise ValueError('Game profile changed during migration')
    return len(files)


def migrate(source, destination):
    source, destination = map(compatibility.plain, (source, destination))
    old = compatibility.plain(source / 'LocalData')
    new = compatibility.plain(destination / 'LocalData'); new.mkdir(mode=0o700,exist_ok=True)
    count = 0
    for name in RECORDS:
        path = compatibility.plain(old / name)
        if path.is_file():
            target = compatibility.plain(new / name)
            if target.exists(): raise ValueError('Profile destination already exists')
            shutil.copy2(path,target)
            if compatibility.digest(path)!=compatibility.digest(target): raise ValueError('Profile copy differs')
            count += 1
    for name in FOLDERS:
        path = compatibility.plain(old / name)
        if path.is_dir(): count += copy_tree(path,new/name)
    for documents in document_roots(source):
        for game in documents.iterdir():
            if game.name.casefold() in DOCUMENT_GAMES:
                count += copy_tree(game,new/'WineHome/Documents'/game.name)
    return count


def relocate(root):
    """Rebase only Wine's owned document links after moving the whole export."""
    root = compatibility.plain(root)
    data = compatibility.plain(root / 'LocalData'); data.mkdir(mode=0o700,exist_ok=True)
    record = compatibility.plain(data / 'linux-location.json')
    previous = root
    if record.exists():
        if not record.is_file() or record.stat().st_size>4096: raise ValueError('Invalid Linux location record')
        value=json.loads(record.read_text(encoding='utf-8'))
        if (not isinstance(value,dict) or set(value)!={'format','root'} or value['format']!='weplay-linux-location-v1'
                or not isinstance(value['root'],str) or not Path(value['root']).is_absolute()):
            raise ValueError('Invalid Linux location record')
        previous=Path(os.path.normpath(value['root']))
    home=root/'LocalData/WineHome';old_home=previous/'LocalData/WineHome'
    changes=[]
    names=('Desktop','Documents','My Documents','Downloads','Music','Pictures','Videos')
    for layout in ('WinePrefix/drive_c/users','ProtonData/pfx/drive_c/users'):
        users=compatibility.plain(root/layout)
        if not users.is_dir(): continue
        for user in users.iterdir():
            user=compatibility.plain(user)
            if not user.is_dir(): continue
            for name in names:
                path=user/name
                if not path.is_symlink(): continue
                raw=os.readlink(path)
                target=Path(os.path.normpath(str(path.parent/raw) if not Path(raw).is_absolute() else raw))
                if target.is_relative_to(home): continue
                if not target.is_relative_to(old_home):
                    raise ValueError('Wine document link points outside this owned game copy')
                changes.append((path,raw,home/target.relative_to(old_home)))
    for path,raw,target in changes:
        if not path.is_symlink() or os.readlink(path)!=raw: raise ValueError('Wine document link changed')
        temporary=path.with_name('weplay-link-'+uuid.uuid4().hex)
        try:
            temporary.symlink_to(target,target_is_directory=True);os.replace(temporary,path)
        finally: temporary.unlink(missing_ok=True)
    temporary=data/('location-'+uuid.uuid4().hex+'.tmp')
    try:
        with temporary.open('x',encoding='utf-8') as stream:
            json.dump(dict(format='weplay-linux-location-v1',root=str(root)),stream)
            stream.flush();os.fsync(stream.fileno())
        os.replace(temporary,record)
    finally: temporary.unlink(missing_ok=True)
    return len(changes)
