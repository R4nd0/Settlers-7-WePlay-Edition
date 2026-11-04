"""Public game metadata and private, host-mediated EasyTier join grants."""
import base64
import http.client
import json
from pathlib import Path
import re
import secrets
import time
from urllib.parse import urlsplit

import compatibility
import join_crypto
from discord_credentials import origin
from internet_room import parse as parse_room

FORMAT='weplay-directory-client-v1'
NETWORK_REVISION=7
HEX32=re.compile('[0-9a-f]{32}')


class DirectoryError(ValueError):
    def __init__(self,code):self.code=code;super().__init__(code)


def record(raw):
    def unique(items):
        value={}
        for key,item in items:
            if key in value:raise ValueError()
            value[key]=item
        return value
    try:return json.loads(raw,object_pairs_hook=unique)
    except (ValueError,UnicodeError,TypeError):raise DirectoryError('directory_invalid') from None


def parse_code(code):
    if not isinstance(code,str) or len(code)>2048:raise DirectoryError('directory_code_invalid')
    prefix=next((item for item in ('weplay-directory1.','weplay-join1.') if code.startswith(item)),None)
    if prefix is None:raise DirectoryError('directory_code_invalid')
    try:
        value=record(join_crypto.decoded(code[len(prefix):],1024))
        keys={'v','origin'}|({'game_id'} if prefix=='weplay-join1.' else set())
        if not isinstance(value,dict) or set(value)!=keys or type(value['v']) is not int or value['v']!=1:raise ValueError()
        url=origin(value['origin'])
        game=value.get('game_id')
        if game is not None and (not isinstance(game,str) or not HEX32.fullmatch(game)):raise ValueError()
        return url,game
    except (ValueError,KeyError,TypeError):raise DirectoryError('directory_code_invalid') from None


def configured(state):
    state=compatibility.plain(state)
    def read(path,expected):
        path=compatibility.plain(path)
        if not path.is_file():return None
        if path.stat().st_size>4096:raise DirectoryError('directory_invalid')
        value=record(path.read_bytes())
        if not isinstance(value,dict) or value.get('format')!=expected:raise DirectoryError('directory_invalid')
        if value.get('origin') is not None:origin(value['origin'])
        return value
    selected=read(state/'game-directory.json',FORMAT)
    if selected and type(selected.get('explicit_override',False)) is not bool:raise DirectoryError('directory_invalid')
    if selected and selected.get('explicit_override') is True and selected.get('origin') is not None:
        return origin(selected['origin'])
    default=read(Path(__file__).with_name('directory-service.json'),'weplay-directory-service-v1')
    if default and default.get('origin') is not None:return origin(default['origin'])
    # Legacy reader profiles followed the old central address. Keep them as a
    # fallback for unconfigured packages, without pinning upgrades to that address.
    if selected and selected.get('origin') is not None:return origin(selected['origin'])
    paired=read(state/'discord-directory.json','weplay-discord-connection-v1')
    if paired and paired.get('origin') is not None:return origin(paired['origin'])
    return None


def request(url,path,method='GET',value=None,authorization=None,*,connection_factory=http.client.HTTPSConnection):
    origin(url)
    if not isinstance(path,str) or not re.fullmatch(r'/v1/directory/[a-z0-9/-]{1,160}',path):
        raise DirectoryError('directory_invalid')
    raw=None if value is None else json.dumps(value,separators=(',',':'),ensure_ascii=True).encode('ascii')
    if raw is not None and len(raw)>8192:raise DirectoryError('directory_invalid')
    connection=connection_factory(urlsplit(url).hostname,timeout=5)
    try:
        headers={'Content-Type':'application/json'}
        if authorization is not None:headers['Authorization']=authorization
        connection.request(method,path,body=raw,headers=headers)
        response=connection.getresponse();body=response.read(131073)
        if len(body)>131072:raise DirectoryError('directory_invalid')
        if response.status!=200:
            raise DirectoryError('directory_join_unavailable' if response.status in (404,409,410,429) else 'directory_unavailable')
        return record(body)
    except (OSError,http.client.HTTPException):raise DirectoryError('directory_unavailable') from None
    finally:connection.close()


def validate_game(value):
    from discord_directory import native_games
    keys={'id','title','state','players','capacity','free_slots','content_id','network_revision','version','expires_at'}
    if not isinstance(value,dict) or set(value)!=keys or not isinstance(value['id'],str) or not HEX32.fullmatch(value['id']):
        raise DirectoryError('directory_invalid')
    native={key:value[key] for key in ('title','state','players','capacity','free_slots')}
    native_games({'format':'s7-lobby-games-v1','content_id':value['content_id'],'games':[dict(id='00000001',**native)]},value['content_id'])
    if (not isinstance(value['content_id'],str) or not re.fullmatch('[0-9a-f]{64}',value['content_id'])
            or type(value['network_revision']) is not int or value['network_revision']!=NETWORK_REVISION
            or not isinstance(value['version'],str) or not re.fullmatch(r'2\.0\.0-rc[0-9]+(?:\.[0-9]+)?',value['version'])
            or type(value['expires_at']) is not int):raise DirectoryError('directory_invalid')
    return dict(value)


def listing(url,*,transport=request):
    value=transport(url,'/v1/directory/games')
    if not isinstance(value,dict) or set(value)!={'format','games'} or value['format']!='weplay-games-directory-v1' or not isinstance(value['games'],list) or len(value['games'])>128:
        raise DirectoryError('directory_invalid')
    rows=[validate_game(row) for row in value['games']]
    if len({row['id'] for row in rows})!=len(rows):raise DirectoryError('directory_invalid')
    return rows


def associated(ticket):
    return json.dumps({key:ticket[key] for key in ('id','game_id','challenge','expires_at')},sort_keys=True,separators=(',',':')).encode('ascii')


def validate_ticket(value,game_id,challenge,clock=time.time):
    if (not isinstance(value,dict) or set(value)!={'format','id','game_id','challenge','expires_at'} or
            value['format']!='weplay-join-ticket-v1' or not isinstance(value['id'],str) or not HEX32.fullmatch(value['id'])
            or value['game_id']!=game_id or value['challenge']!=challenge or type(value['expires_at']) is not int
            or not clock()-5<value['expires_at']<=clock()+90):raise DirectoryError('directory_invalid')
    return value


def join(url,game_id,allowed,*,transport=request,crypto=join_crypto.operation,root=None,
         cancel=None,progress=None,clock=time.time,sleep=time.sleep):
    if not isinstance(game_id,str) or not HEX32.fullmatch(game_id):raise DirectoryError('directory_invalid')
    row=next((item for item in listing(url,transport=transport) if item['id']==game_id),None)
    if row is None or row['state']!='open' or not row['free_slots']:raise DirectoryError('directory_join_unavailable')
    if row['content_id'] not in allowed:raise DirectoryError('version_mismatch')
    key=crypto('keygen',{},root=root)
    if not isinstance(key,dict) or set(key)!={'format','public_key','private_key'} or key['format']!=join_crypto.FORMAT:
        raise DirectoryError('directory_join_failed')
    challenge=secrets.token_hex(16);token=join_crypto.encoded(secrets.token_bytes(32));ticket=None
    try:
        value=transport(url,'/v1/directory/join','POST',dict(format='weplay-join-request-v1',game_id=game_id,
            content_id=row['content_id'],network_revision=NETWORK_REVISION,public_key=key['public_key'],challenge=challenge,token=token))
        ticket=validate_ticket(value,game_id,challenge,clock)
        while clock()<ticket['expires_at']:
            if cancel:cancel()
            if progress:progress('directory_join')
            response=transport(url,'/v1/directory/join/'+ticket['id'],authorization='Join '+token)
            if (not isinstance(response,dict) or set(response)!={'format','id','game_id','challenge','expires_at','envelope'}
                    or response['format']!='weplay-join-delivery-v1'
                    or any(response[name]!=ticket[name] for name in ('id','game_id','challenge','expires_at'))):
                raise DirectoryError('directory_invalid')
            if response['envelope'] is not None:
                opened=crypto('open',dict(private_key=key['private_key'],envelope=response['envelope'],
                    associated=join_crypto.encoded(associated(ticket))),root=root)
                clear=record(join_crypto.decoded(opened['plaintext'],4096))
                if (not isinstance(clear,dict) or set(clear)!={'format','invitation','id','game_id','challenge','expires_at'}
                        or clear['format']!='weplay-room-grant-v1'
                        or any(clear[name]!=ticket[name] for name in ('id','game_id','challenge','expires_at'))):
                    raise DirectoryError('directory_invalid')
                room=parse_room(clear['invitation'])
                if room.room_id=='0'*32:raise DirectoryError('directory_invalid')
                current=next((item for item in listing(url,transport=transport) if item['id']==game_id),None)
                if current is None or current['state']!='open' or current['content_id']!=row['content_id']:
                    raise DirectoryError('directory_join_unavailable')
                return clear['invitation']
            sleep(.75)
        raise DirectoryError('directory_join_unavailable')
    except (KeyError,TypeError):raise DirectoryError('directory_invalid') from None
    finally:
        key.clear()
        if ticket:
            try:transport(url,'/v1/directory/join/'+ticket['id'],'DELETE',authorization='Join '+token)
            except (ValueError,OSError):pass


class HostBroker:
    def __init__(self,url,key,run_id,invitation,expected,version,*,root=None,transport=request,crypto=join_crypto.operation,clock=time.time,monotonic=time.monotonic):
        self.url,self.key,self.run_id=url,key,run_id
        self.invitation,self.room=invitation,parse_room(invitation)
        self.expected,self.version,self.root=expected,version,root
        self.transport,self.crypto,self.clock,self.monotonic=transport,crypto,clock,monotonic
        self.registered=False;self.last=-1000;self.pending={};self.stopped=False

    def poll(self,rows):
        if self.stopped:return
        if not rows:
            self.close();return
        if self.monotonic()-self.last<10:return
        response=self.transport(self.url,'/v1/directory/host','PUT',dict(format='weplay-directory-host-v1',run_id=self.run_id,
            room_id=self.room.room_id,content_id=self.expected,network_revision=NETWORK_REVISION,version=self.version),authorization='Bearer '+self.key)
        if not isinstance(response,dict) or set(response)!={'ok','requests'} or response['ok'] is not True or not isinstance(response['requests'],list) or len(response['requests'])>8:
            raise DirectoryError('directory_invalid')
        self.registered=True;self.last=self.monotonic()
        if self.stopped:self.close();return
        active={row['id']:row for row in rows}
        for item in response['requests']:
            if self.stopped:return
            if (not isinstance(item,dict) or set(item)!={'id','session_id','game_id','public_key','challenge','expires_at'}
                    or any(not isinstance(item[name],str) or not HEX32.fullmatch(item[name]) for name in ('id','game_id','challenge'))
                    or not isinstance(item['session_id'],str) or not re.fullmatch('[0-9a-f]{8}',item['session_id'])
                    or not isinstance(item['public_key'],str) or not 500<=len(item['public_key'])<=700
                    or type(item['expires_at']) is not int or not self.clock()-5<item['expires_at']<=self.clock()+90):
                raise DirectoryError('directory_invalid')
            game=active.get(item['session_id']);sealed=None
            if game and game['state']=='open' and game['free_slots']>0:
                context={name:item[name] for name in ('id','game_id','challenge','expires_at')}
                clear=dict(format='weplay-room-grant-v1',invitation=self.invitation,**context)
                sealed=self.pending.get(item['id'])
                if sealed is None:
                    sealed=self.crypto('seal',dict(public_key=item['public_key'],
                        plaintext=join_crypto.encoded(json.dumps(clear,separators=(',',':')).encode('ascii')),
                        associated=join_crypto.encoded(associated(context))),root=self.root)
                    self.pending[item['id']]=sealed
            if self.stopped:return
            self.transport(self.url,'/v1/directory/host/'+self.run_id+'/requests/'+item['id'],'PUT',
                dict(format='weplay-join-response-v1',envelope=sealed),authorization='Bearer '+self.key)
        live={item['id'] for item in response['requests']}
        self.pending={key:value for key,value in self.pending.items() if key in live}

    def close(self):
        if self.registered:
            try:self.transport(self.url,'/v1/directory/host','DELETE',dict(run_id=self.run_id),authorization='Bearer '+self.key)
            finally:self.registered=False;self.pending.clear()
