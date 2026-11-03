"""Join envelope operations using the bundled .NET cryptography implementation."""
import base64
import json
import os
import subprocess

import compatibility
from windows_system import helper_directory

FORMAT='weplay-join-crypto-v1'


def encoded(value):
    return base64.urlsafe_b64encode(value).decode('ascii').rstrip('=')


def decoded(value,limit=8192):
    if not isinstance(value,str) or len(value)>limit*2:raise ValueError('directory_join_failed')
    raw=base64.b64decode(value+'='*(-len(value)%4),altchars=b'-_',validate=True)
    if len(raw)>limit or encoded(raw)!=value:raise ValueError('directory_join_failed')
    return raw


def operation(action,value,*,root=None):
    if action not in ('keygen','seal','open'):raise ValueError('directory_join_failed')
    executable=compatibility.plain(helper_directory(root)/'WePlay.WindowsHelper.exe')
    environment={key:item for key,item in os.environ.items()
                 if not key.upper().startswith(('DOTNET_','CORECLR_','COR_','COMPLUS_'))}
    environment['PATH']=str(compatibility.plain(os.environ.get('SystemRoot',r'C:\Windows'))/'System32')
    payload=json.dumps(dict(format=FORMAT,**value),separators=(',',':')).encode('ascii')
    if len(payload)>16384:raise ValueError('directory_join_failed')
    try:
        result=subprocess.run([str(executable),'directory-crypto',action],input=payload,capture_output=True,
            timeout=30,env=environment,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    except (OSError,subprocess.TimeoutExpired):raise ValueError('directory_join_failed') from None
    if result.returncode or len(result.stdout)>16384:raise ValueError('directory_join_failed')
    try:return json.loads(result.stdout.decode('utf-8-sig'))
    except (ValueError,UnicodeError):raise ValueError('directory_join_failed') from None
