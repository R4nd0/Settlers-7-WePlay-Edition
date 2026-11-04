"""Per-game, nonce-bound invitation requests. No room secrets enter these files."""
import os
from pathlib import Path
import secrets
import threading
import time

import compatibility
import game_directory


def configuration(request,internet,state):
    mode='unavailable' if request is None else 'lan'
    code=None
    if internet is not None:
        mode='guest' if internet.get('invitation') else 'private'
        if mode=='private' and internet.get('public_lobby',internet.get('publish_discord',False)):
            import json
            from join_crypto import encoded
            mode='public';url=game_directory.configured(state)
            if url:code='weplay-directory1.'+encoded(json.dumps(dict(v=1,origin=url),separators=(',',':')).encode('ascii'))
    return mode,code

SNAPSHOT='invitation-ui-v1.txt'
REQUEST='invitation-open-v1.txt'
HEADER='weplay-invitation-ui-v1|'


class Bridge:
    def __init__(self,root,mode,notify,*,public_code=None,clock=time.monotonic):
        if mode not in ('private','public','lan','guest','unavailable'):raise ValueError('invitation_mode_invalid')
        if public_code is not None:
            if mode!='public' or game_directory.parse_code(public_code)[1] is not None:raise ValueError('invitation_code_invalid')
        self.data=compatibility.plain(Path(root)/'LocalData');self.mode=mode
        self.notify,self.public_code,self.clock=notify,public_code,clock
        self.nonce=secrets.token_hex(16);self.last=-1000.
        self.stop=threading.Event();self.thread=None

    def poll(self):
        path=compatibility.plain(self.data/REQUEST)
        if not path.is_file():return
        # Native CREATE_NEW coalesces repeated clicks; read only a fixed nonce.
        try:
            with path.open('rb') as stream:raw=stream.read(33)
        finally:path.unlink(missing_ok=True)
        if (self.stop.is_set() or raw!=self.nonce.encode('ascii')
                or self.mode not in ('private','public') or self.clock()-self.last<2):return
        self.last=self.clock()
        value=dict(run=self.nonce,mode=self.mode)
        if self.mode=='public':
            if not self.public_code:return
            value['code']=self.public_code
        self.notify(**value)

    def start(self):
        self.data.mkdir(exist_ok=True)
        for name in (SNAPSHOT,REQUEST):
            path=compatibility.plain(self.data/name)
            if path.is_file():
                if path.stat().st_size>96:raise ValueError('invitation_bridge_invalid')
                path.unlink()
        raw=(HEADER+self.nonce+'|'+self.mode+'\n').encode('ascii')
        temporary=compatibility.plain(self.data/('invitation-'+self.nonce+'.tmp'))
        try:
            with temporary.open('xb') as stream:stream.write(raw)
            os.replace(temporary,compatibility.plain(self.data/SNAPSHOT))
        finally:temporary.unlink(missing_ok=True)
        def loop():
            while not self.stop.wait(.2):
                try:self.poll()
                except (OSError,ValueError):break
        self.thread=threading.Thread(target=loop,name='WePlay-invitation',daemon=True);self.thread.start()
        return self

    def close(self):
        self.stop.set()
        if self.thread:self.thread.join(timeout=2)
        for name in (REQUEST,SNAPSHOT):compatibility.plain(self.data/name).unlink(missing_ok=True)
