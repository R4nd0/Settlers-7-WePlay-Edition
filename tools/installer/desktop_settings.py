"""Recoverable desktop graphics group. No arbitrary paths or personal saves.

The desktop operation lock and the game's session lock must be held. The
renderer retains its own byte/ABI/journal guards. Before-images are restored
only over bytes belonging to this exact transaction; foreign edits block it.
"""
import base64
import json
import hashlib
import re
import uuid
from pathlib import Path

import graphics_profile as graphics
import graphics_choice as choice
import quality_settings as quality
import player_settings

NAME='settings-transaction.json'
DETACH='settings-detach.json'
FORMAT='weplay-desktop-settings-v1'
LIMIT=32*1024*1024
GAME_FILES={'choice':choice.CHOICE_FILE,'quality':quality.FILE,'player':player_settings.FILE,'ownership':'desktop-files.json'}

def read(path):
    path=graphics._plain(path)
    return graphics._read(path,4*1024*1024) if path.exists() else None

def encode(raw):return None if raw is None else base64.b64encode(raw).decode('ascii')

def decode(value,limit=6*1024*1024):
    if value is None:return None
    if not isinstance(value,str) or len(value)>limit:raise ValueError('preferences_invalid')
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

def validate_record(desktop,root,record):
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
    return record


def recover(desktop,root):
    path=graphics._plain(desktop.state/NAME)
    if not path.exists():return False
    record=validate_record(desktop,root,graphics._json(graphics._read(path,LIMIT)))
    Transaction(desktop,root,record).rollback();return True


def _recovery_inputs(desktop):
    journal=graphics._plain(desktop.state/NAME)
    raw=graphics._read(journal,LIMIT) if journal.exists() else None
    registration=read(desktop.state/'installation.json')
    marker=read(desktop.state/DETACH)
    root=record=None
    if raw is not None:
        try:
            candidate=graphics._json(raw)
            previous=graphics._json(decode(candidate['files']['installation']['before']))
            value=desktop.validate_installation(previous)
            root=desktop.instance(value['container'],value['current'])
            record=validate_record(desktop,root,candidate)
        except (OSError,ValueError,TypeError,KeyError):record=None
    if root is None and registration is not None:
        try:
            value=desktop.validate_installation(graphics._json(registration))
            root=desktop.instance(value['container'],value['current'])
        except (OSError,ValueError,TypeError,KeyError):pass
    current={key:read(path) for key,path in paths(desktop,root).items()} if root is not None else {}
    descriptor=read(desktop.app.parent/'standalone.json')
    # Hash complete bounded raw bytes, including foreign current settings. A
    # stale confirmation must not authorize a later, different conflict.
    inputs=dict(journal=encode(raw),installation=encode(registration),marker=encode(marker),
                descriptor=encode(descriptor),current={key:encode(value) for key,value in current.items()})
    token=hashlib.sha256(graphics.encoded(inputs)).hexdigest()
    return inputs,token,root,record


def recovery_status(desktop):
    pending=(desktop.state/NAME).exists() or (desktop.state/DETACH).exists()
    result=dict(pending=pending,recoverable=False,can_detach=False,conflicts=[],token=None)
    if not pending:return result
    try:
        inputs,token,root,record=_recovery_inputs(desktop)
        result.update(token=token,can_detach=root is not None or inputs['marker'] is not None)
        if inputs['marker'] is not None:
            _load_detach(desktop,decode(inputs['marker']))
            result['conflicts']=['detach_pending'];return result
        if record is None:result['conflicts']=['journal'];return result
        result['recoverable']=True
        result['conflicts']=[key for key,value in inputs['current'].items()
                            if value not in record['files'][key].values()]
    except (OSError,ValueError,TypeError,KeyError):
        result.update(conflicts=['unreadable'],recoverable=False,can_detach=False,token=None)
    return result


def _backup(desktop,inputs):
    name='settings-recovery-'+uuid.uuid4().hex+'.json';path=graphics._plain(desktop.state/name)
    raw=graphics.encoded(dict(format='weplay-settings-recovery-backup-v1',owner=desktop.owner,inputs=inputs))
    graphics.atomic_create(path,raw,desktop.state)
    if graphics._read(path,128*1024*1024)!=raw:raise ValueError('preferences_invalid')
    return name,hashlib.sha256(raw).hexdigest()


def resolve_conflict(desktop,request):
    if (set(request)!={'token','action'} or request['action'] not in ('restore_previous','detach')
            or not isinstance(request['token'],str) or not re.fullmatch('[0-9a-f]{64}',request['token'])):
        raise ValueError('request_invalid')
    inputs,token,root,record=_recovery_inputs(desktop)
    if token!=request['token']:raise ValueError('recovery_changed')
    if inputs['journal'] is None and inputs['marker'] is None:raise ValueError('recovery_changed')
    if request['action']=='detach':
        return _detach(desktop,inputs,token,root)
    if record is None or root is None or inputs['marker'] is not None:raise ValueError('preferences_invalid')
    with graphics.session_lock(root):
        backup,_=_backup(desktop,inputs)
        if _recovery_inputs(desktop)[1]!=token:raise ValueError('recovery_changed')
        desktop.operation_state('critical')
        try:
            # Renderer integrity is never relaxed by this explicit settings
            # resolution. Only the fixed settings targets may replace foreign
            # bytes, and only after the complete verified backup above.
            desktop.restore_settings_renderer(root,record['renderer'],decode(record['compatibility']))
            expected={key:decode(value) for key,value in inputs['current'].items()}
            targets=paths(desktop,root)
            for key,path in targets.items():
                if any(read(target)!=expected[name] for name,target in targets.items()):raise ValueError('recovery_changed')
                if graphics._read(desktop.state/NAME,LIMIT)!=decode(inputs['journal'],48*1024*1024):raise ValueError('recovery_changed')
                before=decode(record['files'][key]['before'])
                if expected[key]!=before:
                    if before is None:path.unlink()
                    else:graphics.atomic_write(path,before,path.parent)
                    expected[key]=before
            if any(read(path)!=expected[key] for key,path in targets.items()):raise ValueError('recovery_changed')
            if graphics._read(desktop.state/NAME,LIMIT)!=decode(inputs['journal'],48*1024*1024):raise ValueError('recovery_changed')
            graphics._plain(desktop.state/NAME).unlink()
        finally:desktop.operation_state('idle')
    return dict(resolved=True,action='restore_previous',backup=backup)


def _detach(desktop,inputs,token,root):
    marker_path=graphics._plain(desktop.state/DETACH)
    if inputs['marker'] is None:
        if root is None:raise ValueError('preferences_invalid')
        backup,digest=_backup(desktop,inputs)
        if _recovery_inputs(desktop)[1]!=token:raise ValueError('recovery_changed')
        marker=dict(format='weplay-settings-detach-v1',owner=desktop.owner,backup=backup,sha256=digest)
        graphics.atomic_create(marker_path,graphics.encoded(marker),desktop.state)
    marker_raw=graphics._read(marker_path,4*1024*1024)
    marker,saved=_load_detach(desktop,marker_raw)
    backup=marker['backup']
    # The marker blocks normal dispatch until all removals complete. Resuming
    # accepts only already-missing or byte-identical backed-up registrations.
    targets={'journal':desktop.state/NAME,'installation':desktop.state/'installation.json',
             'descriptor':desktop.app.parent/'standalone.json'}
    desktop.operation_state('critical')
    try:
        for key,path in targets.items():
            expected=decode(saved['inputs'][key],48*1024*1024);path=graphics._plain(path)
            actual=graphics._read(path,LIMIT if key=='journal' else 4*1024*1024) if path.exists() else None
            if actual is not None and actual!=expected:raise ValueError('recovery_changed')
        for key,path in targets.items():
            if graphics._read(marker_path,4*1024*1024)!=marker_raw:raise ValueError('recovery_changed')
            path=graphics._plain(path)
            if not path.exists():continue
            if graphics._read(path,LIMIT if key=='journal' else 4*1024*1024)!=decode(saved['inputs'][key],48*1024*1024):raise ValueError('recovery_changed')
            path.unlink()
        if graphics._read(marker_path,4*1024*1024)!=marker_raw:raise ValueError('recovery_changed')
        marker_path.unlink()
    finally:desktop.operation_state('idle')
    return dict(resolved=True,action='detach',backup=backup)


def _load_detach(desktop,raw):
    marker=graphics._json(raw)
    if (set(marker)!={'format','owner','backup','sha256'} or marker['format']!='weplay-settings-detach-v1'
            or marker['owner']!=desktop.owner or not isinstance(marker['backup'],str)
            or not re.fullmatch(r'settings-recovery-[0-9a-f]{32}\.json',marker['backup'])):
        raise ValueError('preferences_invalid')
    raw=graphics._read(graphics._plain(desktop.state/marker['backup']),128*1024*1024)
    if hashlib.sha256(raw).hexdigest()!=marker['sha256']:raise ValueError('preferences_invalid')
    saved=graphics._json(raw)
    if saved.get('format')!='weplay-settings-recovery-backup-v1' or saved.get('owner')!=desktop.owner:raise ValueError('preferences_invalid')
    return marker,saved
