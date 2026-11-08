"""Recoverable desktop graphics group. No arbitrary paths or personal saves.

The desktop operation lock and the game's session lock must be held. The
renderer retains its own byte/ABI/journal guards. Before-images are restored
only over bytes belonging to this exact transaction; foreign edits block it.
"""
import base64
import json
from pathlib import Path

import graphics_profile as graphics
import graphics_choice as choice
import quality_settings as quality
import player_settings

NAME='settings-transaction.json'
FORMAT='weplay-desktop-settings-v1'
LIMIT=32*1024*1024
GAME_FILES={'choice':choice.CHOICE_FILE,'quality':quality.FILE,'player':player_settings.FILE,'ownership':'desktop-files.json'}

def read(path):
    path=graphics._plain(path)
    return graphics._read(path,4*1024*1024) if path.exists() else None

def encode(raw):return None if raw is None else base64.b64encode(raw).decode('ascii')

def decode(value):
    if value is None:return None
    if not isinstance(value,str) or len(value)>6*1024*1024:raise ValueError('preferences_invalid')
    try:return base64.b64decode(value,validate=True)
    except (ValueError,TypeError):raise ValueError('preferences_invalid') from None

def paths(desktop,root):
    return {**{key:graphics._plain(root/name) for key,name in GAME_FILES.items()},
            'installation':graphics._plain(desktop.state/'installation.json')}

def validate(desktop,root,key,raw):
    if raw is None:
        if key in ('installation','ownership'):raise ValueError('preferences_invalid')
        return
    value=graphics._json(raw)
    if key=='choice':choice.validate_choice(value)
    elif key=='quality':quality.validate(value)
    elif key=='player':
        if set(value)!={'format','display_name'} or value['format']!=player_settings.FORMAT:raise ValueError('preferences_invalid')
        player_settings.player_name(value['display_name'])
    elif key=='installation':
        checked=desktop.validate_installation(value)
        if desktop.instance(checked['container'],checked['current'])!=root:raise ValueError('preferences_invalid')
    elif key=='ownership':
        if (set(value)!={'format','owner','files'} or value['format']!='s7-desktop-install-v1'
                or value['owner']!=desktop.owner or not isinstance(value['files'],dict)
                or len(value['files'])>30000):raise ValueError('preferences_invalid')
        for name,digest in value['files'].items():
            if (not isinstance(name,str) or not name or name.startswith('/') or '\\' in name or ':' in name
                    or any(part in ('','.','..') for part in name.split('/')) or name.split('/')[0].lower()=='localdata'
                    or not isinstance(digest,str) or len(digest)!=64 or any(c not in '0123456789abcdef' for c in digest)):
                raise ValueError('preferences_invalid')
    else:raise ValueError('preferences_invalid')

class Transaction:
    def __init__(self,desktop,root,record):
        self.desktop=desktop;self.root=graphics._plain(root);self.record=record
        self.path=graphics._plain(desktop.state/NAME);self.targets=paths(desktop,self.root)

    @classmethod
    def begin(cls,desktop,root,mode,planned):
        targets=paths(desktop,root)
        if set(planned)!=set(targets)-{'ownership'}:raise ValueError('preferences_invalid')
        if (desktop.state/NAME).exists():raise ValueError('preferences_invalid')
        before={key:read(path) for key,path in targets.items()}
        after={**before,**planned}
        for key in targets:
            validate(desktop,root,key,before[key]);validate(desktop,root,key,after[key])
        record=dict(format=FORMAT,owner=desktop.owner,instance=root.name,renderer=mode,
                    compatibility=encode(read(root/graphics.COMPAT)),
                    files={key:dict(before=encode(before[key]),after=encode(after[key])) for key in targets})
        result=cls(desktop,root,record);result.persist();return result

    def persist(self):
        raw=graphics.encoded(self.record)
        if len(raw)>LIMIT:raise ValueError('preferences_invalid')
        graphics.atomic_write(self.path,raw,self.desktop.state)

    def ownership(self,raw):
        validate(self.desktop,self.root,'ownership',raw)
        self.record['files']['ownership']['after']=encode(raw);self.persist()

    def check(self):
        for key,path in self.targets.items():
            values=self.record['files'][key]
            if read(path) not in (decode(values['before']),decode(values['after'])):
                raise ValueError('preferences_invalid')

    def rollback(self):
        # Validate ALL targets before changing even the renderer or one setting.
        self.check()
        self.desktop.restore_settings_renderer(self.root,self.record['renderer'],decode(self.record['compatibility']))
        for key,path in self.targets.items():
            self.check()
            before=decode(self.record['files'][key]['before'])
            if read(path)==before:continue
            if before is None:path.unlink()
            else:graphics.atomic_write(path,before,graphics._plain(path.parent))
        self.path.unlink()

    def commit(self):
        for key,path in self.targets.items():
            if read(path)!=decode(self.record['files'][key]['after']):raise ValueError('preferences_invalid')
        self.path.unlink()

def recover(desktop,root):
    path=graphics._plain(desktop.state/NAME)
    if not path.exists():return False
    record=graphics._json(graphics._read(path,LIMIT))
    if (set(record)!={'format','owner','instance','renderer','compatibility','files'}
            or record['format']!=FORMAT or record['owner']!=desktop.owner or record['instance']!=root.name
            or record['renderer'] not in ('native','dxvk') or not isinstance(record['files'],dict)
            or set(record['files'])!=set(paths(desktop,root))):raise ValueError('preferences_invalid')
    compatibility=decode(record['compatibility'])
    if compatibility is None:raise ValueError('preferences_invalid')
    graphics.content.validate(graphics._json(compatibility))
    for key,values in record['files'].items():
        if not isinstance(values,dict) or set(values)!={'before','after'}:raise ValueError('preferences_invalid')
        for raw in values.values():validate(desktop,root,key,decode(raw))
    Transaction(desktop,root,record).rollback();return True
