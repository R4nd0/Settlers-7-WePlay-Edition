"""Per-user Windows Credential Manager storage for a publisher grant, not a bot token."""
import base64
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re

import compatibility

FORMAT='weplay-discord-connection-v1'


class DiscordError(ValueError):
    def __init__(self,code):self.code=code;super().__init__(code)


def origin(value):
    if not isinstance(value,str) or not re.fullmatch(r'https://weplay-multiplayer\.[a-z0-9-]{1,63}\.workers\.dev',value):
        raise DiscordError('discord_connection_invalid')
    return value


def parse(code):
    if not isinstance(code,str) or len(code)>2048 or not code.startswith('weplay-connect1.'):
        raise DiscordError('discord_connection_invalid')
    body=code[len('weplay-connect1.'):]
    if not re.fullmatch('[A-Za-z0-9_-]+',body):raise DiscordError('discord_connection_invalid')
    def unique(pairs):
        value={}
        for key,item in pairs:
            if key in value:raise ValueError()
            value[key]=item
        return value
    try:
        raw=base64.urlsafe_b64decode(body+'='*(-len(body)%4))
        if base64.urlsafe_b64encode(raw).decode().rstrip('=')!=body:raise ValueError()
        value=json.loads(raw,object_pairs_hook=unique)
        if (not isinstance(value,dict) or set(value)!={'v','origin','key'} or type(value['v']) is not int or value['v']!=1
                or not isinstance(value['key'],str) or not re.fullmatch('[A-Za-z0-9_-]{43}',value['key'])):raise ValueError()
        return origin(value['origin']),value['key']
    except (ValueError,UnicodeError,KeyError,TypeError):raise DiscordError('discord_connection_invalid') from None


class Credential(ctypes.Structure):
    _fields_=[('Flags',wintypes.DWORD),('Type',wintypes.DWORD),('TargetName',wintypes.LPWSTR),
              ('Comment',wintypes.LPWSTR),('LastWritten',wintypes.FILETIME),('CredentialBlobSize',wintypes.DWORD),
              ('CredentialBlob',ctypes.POINTER(wintypes.BYTE)),('Persist',wintypes.DWORD),('AttributeCount',wintypes.DWORD),
              ('Attributes',ctypes.c_void_p),('TargetAlias',wintypes.LPWSTR),('UserName',wintypes.LPWSTR)]


def api():
    if os.name!='nt':raise DiscordError('discord_windows_required')
    library=ctypes.WinDLL('advapi32',use_last_error=True)
    for name,args,result in (
        ('CredReadW',[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,ctypes.POINTER(ctypes.POINTER(Credential))],wintypes.BOOL),
        ('CredWriteW',[ctypes.POINTER(Credential),wintypes.DWORD],wintypes.BOOL),
        ('CredDeleteW',[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD],wintypes.BOOL),
        ('CredFree',[ctypes.c_void_p],None)):
        function=getattr(library,name);function.argtypes=args;function.restype=result
    return library


def target(url):return 'Settlers7WePlay/DiscordPublisher/'+hashlib.sha256(origin(url).encode('ascii')).hexdigest()


def store(url,key):
    if not isinstance(key,str) or not re.fullmatch('[A-Za-z0-9_-]{43}',key):raise DiscordError('discord_connection_invalid')
    value=Credential();blob=ctypes.create_string_buffer(key.encode('ascii'))
    value.Type=1;value.TargetName=target(url);value.UserName='WePlay publisher';value.Persist=2
    value.CredentialBlobSize=43;value.CredentialBlob=ctypes.cast(blob,ctypes.POINTER(wintypes.BYTE))
    try:
        if not api().CredWriteW(ctypes.byref(value),0):raise DiscordError('discord_connection_failed')
    finally:ctypes.memset(blob,0,ctypes.sizeof(blob))


def get(url):
    library=api();pointer=ctypes.POINTER(Credential)()
    if not library.CredReadW(target(url),1,0,ctypes.byref(pointer)):
        if ctypes.get_last_error()==1168:return None
        raise DiscordError('discord_connection_failed')
    try:
        value=pointer.contents
        if value.CredentialBlobSize!=43 or not value.CredentialBlob:raise DiscordError('discord_connection_invalid')
        key=ctypes.string_at(value.CredentialBlob,43).decode('ascii')
        if not re.fullmatch('[A-Za-z0-9_-]{43}',key):raise DiscordError('discord_connection_invalid')
        return key
    finally:
        if pointer.contents.CredentialBlob and pointer.contents.CredentialBlobSize<=512:
            ctypes.memset(pointer.contents.CredentialBlob,0,pointer.contents.CredentialBlobSize)
        library.CredFree(pointer)


def remove(url):
    if not api().CredDeleteW(target(url),1,0) and ctypes.get_last_error()!=1168:
        raise DiscordError('discord_connection_failed')


def public_settings(state):
    path=compatibility.plain(Path(state)/'discord-directory.json')
    try:
        if path.stat().st_size>4096:raise ValueError()
        value=json.loads(path.read_text(encoding='utf-8'))
        if (not isinstance(value,dict) or set(value)!={'format','origin','publish_enabled'} or
                value['format']!=FORMAT or type(value['publish_enabled']) is not bool):raise ValueError()
        origin(value['origin']);return value
    except FileNotFoundError:return None
    except (ValueError,KeyError,TypeError):raise DiscordError('discord_connection_invalid') from None


def load(state):
    value=public_settings(state)
    if value is None:return None
    key=get(value['origin'])
    return (value['origin'],key) if key else None


def connected(state):
    try:return load(state) is not None
    except (OSError,ValueError):return False
