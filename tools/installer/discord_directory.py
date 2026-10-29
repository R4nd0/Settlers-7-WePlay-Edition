"""Read own admitted game facts and send only their public projection over TLS."""
import http.client
import json
import re
import threading
import time
import unicodedata
import uuid
from urllib.parse import urlsplit

from discord_credentials import DiscordError,load,origin
from lan_control import request


def check_connection(url,key,connection_factory=http.client.HTTPSConnection):
    connection=connection_factory(urlsplit(origin(url)).hostname,timeout=5)
    try:
        connection.request('GET','/v1/publisher',headers={'Authorization':'Bearer '+key})
        response=connection.getresponse();raw=response.read(4097)
        if response.status!=200 or len(raw)>4096 or json.loads(raw)!={'ok':True}:
            raise DiscordError('discord_connection_failed')
    finally:connection.close()


def native_games(value,expected):
    if (not isinstance(value,dict) or set(value)!={'format','content_id','games'} or
            value['format']!='s7-lobby-games-v1' or value['content_id']!=expected or
            not isinstance(value['games'],list) or len(value['games'])>8):
        raise DiscordError('discord_status_invalid')
    result=[];seen=set()
    for game in value['games']:
        if (not isinstance(game,dict) or set(game)!={'id','title','state','players','capacity','free_slots'} or
                not isinstance(game['id'],str) or not re.fullmatch('[0-9a-f]{8}',game['id']) or game['id']=='00000000' or
                game['id'] in seen or not isinstance(game['title'],str) or not 1<=len(game['title'])<=48 or
                game['title']!=game['title'].strip() or any(unicodedata.category(c) in ('Cc','Cf') for c in game['title']) or
                any(x in game['title'].lower() for x in ('@','://','weplay1.','discord.gg')) or
                any(type(game[k]) is not int for k in ('players','capacity','free_slots')) or
                not 1<=game['players']<=game['capacity']<=4 or not 0<=game['free_slots']<=game['capacity']-game['players'] or
                game['state'] not in ('open','full','started') or
                (game['free_slots']==0 if game['state']=='open' else game['free_slots']!=0)):
            raise DiscordError('discord_status_invalid')
        seen.add(game['id']);result.append(dict(game))
    return result


class Reporter:
    def __init__(self,state,interface,coordinator,expected,*,warn=None,transport=request,
                 connection_factory=http.client.HTTPSConnection,clock=time.monotonic):
        credential=load(state)
        if credential is None:raise DiscordError('discord_not_connected')
        self.url,self.key=credential;origin(self.url)
        self.interface,self.coordinator,self.expected=interface,coordinator,expected
        self.warn,self.transport,self.connection_factory,self.clock=warn,transport,connection_factory,clock
        self.run_id=uuid.uuid4().hex;self.sequence=0;self.registered=False
        self.stop=threading.Event();self.thread=None;self.active_connection=None
        self.last_payload=None;self.last_send=-1000.;self.last_warning=-1000.

    def send(self,method,games):
        value={'format':'weplay-game-report-v1','run_id':self.run_id,
               'sequence':0 if method=='POST' else self.sequence,'games':games}
        connection=self.connection_factory(urlsplit(self.url).hostname,timeout=5)
        self.active_connection=connection
        try:
            connection.request(method,'/v1/report',body=json.dumps(value,separators=(',',':')).encode('utf-8'),
                               headers={'Content-Type':'application/json','Authorization':'Bearer '+self.key})
            response=connection.getresponse();raw=response.read(4097)
            if response.status==409:self.registered=False
            if response.status!=200 or len(raw)>4096 or json.loads(raw)!={'ok':True}:
                raise DiscordError('discord_publish_failed')
        finally:connection.close();self.active_connection=None

    def poll(self):
        value=self.transport(self.coordinator,self.interface.address,'games',expected=self.expected,timeout=2)
        games=native_games(value,self.expected)
        if not self.registered:
            self.send('POST',[]);self.registered=True
        payload=json.dumps(games,sort_keys=True,separators=(',',':'))
        if payload!=self.last_payload or self.clock()-self.last_send>=60:
            self.sequence+=1;self.send('PUT',games)
            self.last_payload,self.last_send=payload,self.clock()

    def start(self):
        def loop():
            while not self.stop.is_set():
                try:self.poll()
                except (OSError,ValueError,TimeoutError,http.client.HTTPException):
                    if self.warn and self.clock()-self.last_warning>=60:
                        self.last_warning=self.clock();self.warn('discord_publish_failed')
                self.stop.wait(5)
        self.thread=threading.Thread(target=loop,name='WePlay-public-status',daemon=True);self.thread.start()
        return self

    def close(self):
        self.stop.set()
        if self.active_connection:
            try:self.active_connection.close()
            except OSError:pass
        if self.thread:self.thread.join(timeout=8)
        if self.registered and (not self.thread or not self.thread.is_alive()):
            self.sequence+=1
            try:self.send('DELETE',[])
            except (OSError,ValueError,TimeoutError,http.client.HTTPException):pass
        self.key=''
