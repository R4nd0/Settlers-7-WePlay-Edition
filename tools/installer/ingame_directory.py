"""Bounded public-data bridge; room changes happen only after normal game exit."""
import os
from pathlib import Path
import secrets
import threading

import compatibility
import game_directory

SNAPSHOT='global-games-v1.txt'
REQUEST='global-join-v1.txt'
HEADER='weplay-games-ui-v1|'


def snapshot(nonce,sequence,rows,allowed):
    value=HEADER+nonce+'|'+str(sequence)+'\n'
    for row in rows:
        row=game_directory.validate_game(row)
        state='i' if row['content_id'] not in allowed else {'open':'o','full':'f','started':'s'}[row['state']]
        line='|'.join((row['id'],state,str(row['players']),str(row['capacity']),row['title'].encode('utf-8').hex()))+'\n'
        if len(value)+len(line)>32768:break
        value+=line
    return value.encode('ascii')


class Bridge:
    def __init__(self,root,url,allowed,*,transport=game_directory.request):
        self.data=compatibility.plain(Path(root)/'LocalData');self.url=url;self.allowed=set(allowed)
        self.transport=transport;self.nonce=secrets.token_hex(16);self.sequence=0;self.ids=set()
        self.stop=threading.Event();self.thread=None;self.pending=None

    def write(self,rows):
        self.sequence+=1
        raw=snapshot(self.nonce,self.sequence,rows,self.allowed)
        temporary=compatibility.plain(self.data/('directory-'+self.nonce+'.tmp'))
        target=compatibility.plain(self.data/SNAPSHOT)
        try:
            with temporary.open('xb') as stream:stream.write(raw)
            os.replace(temporary,target)
        finally:temporary.unlink(missing_ok=True)
        displayed={line.split('|',1)[0] for line in raw.decode('ascii').splitlines()[1:]}
        self.ids={row['id'] for row in rows if row['id'] in displayed and row['state']=='open'
                  and row['free_slots']>0 and row['content_id'] in self.allowed}

    def start(self):
        self.data.mkdir(exist_ok=True)
        for name in (SNAPSHOT,REQUEST):
            path=compatibility.plain(self.data/name)
            if path.is_file():
                if path.stat().st_size>32768:raise game_directory.DirectoryError('directory_invalid')
                raw=path.read_bytes()
                if name==SNAPSHOT and not raw.startswith(HEADER.encode('ascii')):raise game_directory.DirectoryError('directory_invalid')
                if name==REQUEST and not __import__('re').fullmatch(rb'[0-9a-f]{32}:[0-9a-f]{32}',raw):raise game_directory.DirectoryError('directory_invalid')
                path.unlink()
        self.write([])
        def refresh():
            while not self.stop.is_set():
                try:rows=game_directory.listing(self.url,transport=self.transport)
                except (OSError,ValueError):rows=[]
                if self.stop.is_set():break
                try:self.write(rows)
                except (OSError,ValueError):break
                self.stop.wait(10)
        self.thread=threading.Thread(target=refresh,name='WePlay-global-games',daemon=True);self.thread.start()
        return self

    def close(self):
        self.stop.set()
        if self.thread:self.thread.join(timeout=7)
        request=compatibility.plain(self.data/REQUEST)
        try:
            if request.is_file() and request.stat().st_size==65:
                value=request.read_bytes().decode('ascii')
                nonce,identifier=value.split(':')
                if nonce==self.nonce and identifier in self.ids:self.pending=identifier
        except (ValueError,UnicodeError):pass
        finally:
            request.unlink(missing_ok=True)
            compatibility.plain(self.data/SNAPSHOT).unlink(missing_ok=True)
