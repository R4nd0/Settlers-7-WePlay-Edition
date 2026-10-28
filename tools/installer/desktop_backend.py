"""Bounded desktop IPC over stdin/stdout; one managed application per command.

No free-form command execution or endpoint entry. The GUI supplies selections;
the existing installer, profile journal, launch locks and LAN checks do the work.
"""
import argparse
from contextlib import contextmanager, redirect_stdout
import hashlib
import io
import json
import msvcrt
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
import time
from types import SimpleNamespace
import uuid

sys.path[:0]=[str(Path(__file__).resolve().parent),str(Path(__file__).resolve().parent.parent/'offline-ui'),
              str(Path(__file__).resolve().parent.parent/'d3d11')]
import compatibility as content
import display_settings as display
import game_detection
import graphics_choice as choice
import graphics_profile as graphics
import install
import launch_local
import launch_auto_lan
import player_settings
import quality_settings as quality
from lan_discovery import LanDiscovery
from lan_interfaces import enumerate_interfaces
from lobby_directory import json_record, encode
from lobby_picker import read_choice

FORMAT='s7-desktop-install-v1'
REQUEST='s7-desktop-request-v1'
HEX=re.compile('[0-9a-f]{32}')


class Cancelled(RuntimeError): pass


class Console(io.TextIOBase):
    def __init__(self, emit):self.emit=emit;self.pending=''
    def write(self,text):
        self.pending+=text
        while '\n' in self.pending:
            line,self.pending=self.pending.split('\n',1)
            if line.strip():self.emit('log',text=line[:512])
        self.pending=self.pending[-2048:]
        return len(text)
    def flush(self):pass


class Desktop:
    def __init__(self, app, emit=lambda *a,**k:None, operation=None):
        self.app=graphics._plain(app)
        self.package=graphics._plain(self.app/'Package')
        self.state=graphics._plain(self.app/'State');self.state.mkdir(exist_ok=True)
        self.owner=hashlib.sha256(str(self.app).casefold().encode('utf-8')).hexdigest()
        self.emit=emit
        self.operation=operation or uuid.uuid4().hex
        if not HEX.fullmatch(self.operation):raise ValueError('request_invalid')
        self.cancel=graphics._plain(self.state/('cancel-'+self.operation))
        self.last_progress=0.0
        self.finalizing=False

    def progress(self,phase,current=0,total=0):
        if not self.finalizing and self.cancel.exists():raise Cancelled('cancelled')
        if phase=='patch':self.finalizing=True
        now=time.monotonic()
        if phase!='copy' or now-self.last_progress>=.2 or current==total:
            self.emit('progress',phase=phase,current=current,total=total)
            self.last_progress=now

    def read(self,path,default=None,limit=4*1024*1024):
        path=graphics._plain(path)
        return graphics._json(graphics._read(path,limit)) if path.exists() else default

    def write(self,path,value):
        path=graphics._plain(path)
        graphics.atomic_write(path,graphics.encoded(value),graphics._plain(path.parent))

    def installed(self):
        try:value=self.read(self.state/'installation.json')
        except (OSError,ValueError):raise ValueError('installation_invalid') from None
        if value is None:return None
        if (set(value)!={'format','source','container','current','previous','package_id','renderer_policy'}
                or value['format']!=FORMAT or value['renderer_policy'] not in ('auto','dxvk','native')
                or not isinstance(value['source'],str) or not isinstance(value['previous'],list)
                or len(value['previous'])>16 or not isinstance(value['package_id'],str)):
            raise ValueError('installation_invalid')
        if any(not isinstance(name,str) or not HEX.fullmatch(name) for name in [value['current'],*value['previous']]):
            raise ValueError('installation_invalid')
        if len({value['current'],*value['previous']})!=1+len(value['previous']):raise ValueError('installation_invalid')
        for name in [value['current'],*value['previous']]:self.instance(value['container'],name)
        return value

    def instance(self,container,name):
        if not isinstance(name,str) or not HEX.fullmatch(name):raise ValueError('installation_invalid')
        root=graphics._plain(Path(container)/name)
        marker=self.read(root/'desktop-owner.json')
        if marker!={'format':FORMAT,'owner':self.owner,'instance':name}:
            raise ValueError('installation_invalid')
        return root

    def game(self):
        value=self.installed()
        if value is None:raise ValueError('not_installed')
        return self.instance(value['container'],value['current'])

    def package_id(self,full=False):
        value=install.package_manifest(self.package) if full else self.read(self.package/'package.json')
        if not value or not isinstance(value.get('files'),dict):raise ValueError('package_invalid')
        # GUI/backend-only updates do not require another 7 GB game copy.
        # Every file affecting the copied game/launch contract still participates.
        mapped=[*install.GRAPHICS_TOOLS,*install.DISPLAY_TOOLS,*install.DESKTOP_TOOLS,*install.LAN_LOBBY_TOOLS,
                ('offline-ui/launch_local.py',''),('installer/compatibility.py',''),
                ('installer/start.py',''),('installer/launch_lan.py',''),('installer/install.py',''),
                ('offline-ui/patch-spec.json',''),('offline-ui/join-menu-update.json','')]
        names={('tools/'+name).lower() for name,_ in mapped}
        records={name:digest for name,digest in value['files'].items() if name in names or
                 name.startswith(('runtime/','payload/server/')) or name in
                 ('payload/uplay_r1_loader.dll','payload/graphics/d3d9.dll','tools/license.txt')}
        return content.fingerprint({'files':records,'archive':value.get('patched_archive_sha256'),
             'flags':{key:value.get(key,False) for key in ('graphics_choice','display_selection','automatic_lan','desktop_launcher')}})

    @contextmanager
    def lock(self):
        path=graphics._plain(self.state/'operation.lock')
        with path.open('a+b') as stream:
            if stream.tell()==0:stream.write(b'0');stream.flush()
            stream.seek(0)
            try:msvcrt.locking(stream.fileno(),msvcrt.LK_NBLCK,1)
            except OSError:raise RuntimeError('operation_busy') from None
            try:yield
            finally:
                stream.seek(0);msvcrt.locking(stream.fileno(),msvcrt.LK_UNLCK,1)

    def probe(self,refresh=False):
        path=self.state/'graphics-probe.json'
        dll=graphics._plain(self.package/'Payload/Graphics/d3d9.dll')
        program=graphics._plain(self.package/'Payload/Graphics/S7GraphicsProbe.exe')
        manifest=install.package_manifest(self.package)
        signature=manifest['files'].get('payload/graphics/s7graphicsprobe.exe')
        if not signature or content.digest(program)!=signature or content.digest(dll)!=graphics.DXVK_SHA256:
            raise ValueError('package_invalid')
        previous=self.read(path,limit=65536)
        adapters=sorted({screen['adapter'] for screen in self.displays()})
        if (not refresh and previous is not None and previous.get('probe_sha256')==signature
                and previous.get('adapters')==adapters and previous.get('failure')!='probe_failed'):
            return self.validate_probe(previous)
        graphics.ensure_closed()
        self.progress('graphics_probe')
        environment=dict(os.environ)
        for key in list(environment):
            if key.upper() in ('DXVK_CONFIG','DXVK_FILTER_DEVICE_NAME','DXVK_CONFIG_FILE'):
                del environment[key]
        environment.update(DXVK_CONFIG='d3d9.floatEmulation = Strict',DXVK_LOG_LEVEL='none',
                           DXVK_STATE_CACHE='0',DXVK_LOG_PATH=str(self.state))
        devices=[];failure=None
        try:
            import memory_guard
            memory_guard.require_host_headroom()
            result=subprocess.run([str(program),str(dll)],cwd=self.state,env=environment,
                capture_output=True,timeout=25,creationflags=subprocess.CREATE_NO_WINDOW)
            parsed=json_record(result.stdout,65536)
            if set(parsed)!={'format','devices'} or parsed['format']!='s7-render-probe-v1':
                raise ValueError('probe_invalid')
            devices=parsed['devices']
            if result.returncode not in (0,2):raise ValueError('probe_invalid')
            if result.returncode or not devices:failure='unsupported'
        except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired):
            devices=[];failure='probe_failed'
        value=self.validate_probe({'format':'s7-desktop-probe-v1','probe_sha256':signature,'adapters':adapters,
                                  'devices':devices,'failure':failure})
        self.write(path,value)
        return value

    @staticmethod
    def validate_probe(value):
        if (not isinstance(value,dict) or set(value)!={'format','probe_sha256','devices','failure','adapters'}
                or value['format']!='s7-desktop-probe-v1' or not isinstance(value['devices'],list)
                or len(value['devices'])>16 or not isinstance(value['adapters'],list)
                or len(value['adapters'])>16 or value['failure'] not in (None,'unsupported','probe_failed')):
            raise ValueError('probe_invalid')
        if (not isinstance(value['probe_sha256'],str) or not re.fullmatch('[0-9a-f]{64}',value['probe_sha256'])
                or (value['failure'] is None and not value['devices'])):
            raise ValueError('probe_invalid')
        for name in [*value['devices'],*value['adapters']]:
            if not isinstance(name,str):raise ValueError('probe_invalid')
            choice.validate_choice({'format':choice.FORMAT,'choice':name})
        return value

    def displays(self):
        try:return display.enumerate_displays()[:2]
        except (OSError,ValueError,RuntimeError):return []

    def snapshot(self):
        value=self.installed()
        result={'installed':value is not None,'displays':self.displays(),
                'data_default':str(value['container'] if value else self.app/'Instances'),'source':value['source'] if value else '',
                'renderer_policy':value['renderer_policy'] if value else 'auto',
                'probe':self.read(self.state/'graphics-probe.json',{'devices':[],'failure':None}),
                'choice':{'format':choice.FORMAT,'choice':None,'simple_menu':True,'display':None},
                'quality':dict(quality.DEFAULTS),'player_name':'LocalPlayer','renderer':'native',
                'current':None,'update_available':False,'needs_repair':False}
        if value:
            root=self.game()
            result.update(current=str(root),update_available=value['package_id']!=self.package_id())
            try:
                result.update(choice=choice._load_choice(root),quality=quality.load(root),
                              player_name=player_settings.load(root),renderer=choice._renderer(root,{})[0])
            except (OSError,ValueError,RuntimeError):result['needs_repair']=True
        return result

    def renderer(self,root,policy,probe):
        if policy not in ('auto','dxvk','native'):raise ValueError('request_invalid')
        supported=bool(probe['devices'])
        if policy=='dxvk' and not supported:raise ValueError('renderer_unsupported')
        use_dxvk=policy=='dxvk' or (policy=='auto' and supported)
        wanted='dxvk' if use_dxvk else 'native'
        pending=graphics._plain(root/graphics.STATE/'transaction.json').exists()
        if not pending and choice._renderer(root,{})[0]==wanted:return wanted
        self.progress('graphics_apply')
        if use_dxvk:graphics.install(root,self.package/'Payload/Graphics/d3d9.dll')
        else:graphics.restore(root)
        return 'dxvk' if use_dxvk else 'native'

    def default_choice(self,root,renderer,probe,preferred=None):
        screens=self.displays()
        selected={'format':choice.FORMAT,'choice':None,'simple_menu':True,'display':None}
        if preferred is not None:
            selected=choice.validate_choice(preferred)
        target=None
        if screens:
            target=next((s for s in screens if selected.get('display') and
                         s['device']==selected['display']['device']),screens[0])
            if selected.get('display'):
                try:display.resolve(selected['display'],screens)
                except ValueError:selected['display']=None
            if selected.get('display') is None:
                size=[target['width'],target['height']]
                mode='borderless'
                if size not in target['resolutions'] or not display._resolution(*size):
                    sizes=[s for s in target['resolutions'] if s[0]<=1920 and s[1]<=1080]
                    if sizes:size=sizes[-1];mode='windowed'
                    else:return selected
                selected['display']=dict(device=target['device'],width=size[0],height=size[1],mode=mode)
        if renderer=='native':selected['choice']=None
        elif target and target['adapter'] in probe['devices']:
            if selected['choice'] not in probe['devices'] or selected['choice']!=target['adapter']:
                selected['choice']=target['adapter']
        else:selected['choice']=None
        return selected

    def transfer_preferences(self,old,new):
        # Same-PC update only. Renderer backup/journal, caches and live runtime
        # configuration are deliberately not migrated into another version.
        data=new/'LocalData';data.mkdir(exist_ok=True)
        for name in ('local-profile-id.txt','player-settings.json','graphics-choice.json','graphics-quality.json'):
            source=graphics._plain(old/'LocalData'/name)
            if source.is_file():
                if source.stat().st_size>65536:raise ValueError('preferences_invalid')
                try:
                    if name=='player-settings.json':player_settings.load(old)
                    elif name=='graphics-choice.json':choice._load_choice(old)
                    elif name=='graphics-quality.json':quality.load(old)
                except (OSError,ValueError):
                    self.emit('warning',code='preferences_reset');continue
                shutil.copyfile(source,graphics._plain(data/name))
        saves=graphics._plain(old/'LocalData/saves')
        if saves.is_dir():
            for path in saves.rglob('*'):graphics._plain(path)
            shutil.copytree(saves,data/'saves')

    def install_game(self,source,container=None):
        self.finalizing=False
        graphics.ensure_closed()
        source=graphics._plain(source)
        install.source_base(source)
        previous=self.installed()
        base=graphics._plain(container or (previous['container'] if previous else self.app/'Instances'))
        if base==source or base.is_relative_to(source) or source.is_relative_to(base):
            raise ValueError('destination_invalid')
        if base.is_relative_to(self.package) or self.package.is_relative_to(base):
            raise ValueError('destination_invalid')
        base.mkdir(parents=True,exist_ok=True)
        instance=uuid.uuid4().hex
        root=base/instance
        self.progress('source')
        install.prepare(self.package,source,root,emit=lambda text:self.emit('log',text=text),progress=self.progress,publish_starters=False)
        self.write(root/'desktop-owner.json',{'format':FORMAT,'owner':self.owner,'instance':instance})
        old=self.game() if previous else None
        if old is None:
            retained=self.read(self.state/'retained-profile.json')
            if retained:
                try:old=self.instance(retained['container'],retained['instance'])
                except (OSError,ValueError,KeyError):pass
        if old:self.transfer_preferences(old,root)
        probe=self.probe()
        policy=previous['renderer_policy'] if previous else 'auto'
        mode=self.renderer(root,policy,probe)
        with graphics.session_lock(root):
            selected=self.default_choice(root,mode,probe,choice._load_choice(root) if old else None)
            choice.save_choice(root,selected)
            self.write(root/quality.FILE,quality.load(root))
        self.progress('verify')
        verified=content.verify(root)
        self.record_ownership(root,verified)
        record={'format':FORMAT,'source':str(source),'container':str(base),'current':instance,
                'previous':([previous['current'],*previous['previous']][:16] if previous else []),
                'package_id':self.package_id(),'renderer_policy':policy}
        self.progress('finish')
        self.write(self.state/'installation.json',record)
        return self.snapshot()

    def record_ownership(self,root,manifest=None):
        manifest=manifest or content.load(root/'compatibility.json')
        files=dict(manifest['files'])
        # Runtime files were byte-verified by prepare; reuse those package hashes.
        package=install.package_manifest(self.package)
        files.update({name:digest for name,digest in package['files'].items() if name.startswith('runtime/')})
        for path in root.iterdir():
            if path.is_file() and path.name!='desktop-files.json':files[path.name.lower()]=content.digest(path)
        for path in (root/'ArchiveStage').rglob('*'):
            if path.is_file():files[path.relative_to(root).as_posix().lower()]=content.digest(path)
        self.write(root/'desktop-files.json',{'format':FORMAT,'owner':self.owner,'files':files})

    def save_settings(self,request):
        if set(request)!={'renderer_policy','choice','quality','player_name'}:raise ValueError('request_invalid')
        root=self.game();graphics.ensure_closed()
        if self.installed()['package_id']!=self.package_id():raise ValueError('update_required')
        selected=choice.validate_choice(request['choice']);quality_value=quality.validate(request['quality'])
        player_settings.player_name(request['player_name'])
        probe=self.probe()
        policy=request['renderer_policy']
        # Reject invalid display/GPU requests before making any profile change.
        if selected.get('display') is not None:display.resolve(selected['display'],self.displays())
        expected_mode='dxvk' if policy!='native' and probe['devices'] else 'native'
        if selected['choice'] is not None and selected['choice'] not in probe['devices']:
            raise ValueError('renderer_unsupported')
        if expected_mode=='dxvk' and selected.get('display') is not None:
            display.resolve(selected['display'],self.displays(),renderer='dxvk',gpu=selected['choice'])
        mode=self.renderer(root,policy,probe)
        if mode=='native':selected['choice']=None
        elif selected['choice'] is None:
            screens=self.displays()
            target=next((s for s in screens if selected.get('display') and s['device']==selected['display']['device']),screens[0] if screens else None)
            if target and target['adapter'] in probe['devices']:selected['choice']=target['adapter']
        with graphics.session_lock(root):
            choice.save_choice(root,selected)
            self.write(root/quality.FILE,quality_value)
            player_settings.save(root,request['player_name'])
        value=self.installed();value['renderer_policy']=policy;self.write(self.state/'installation.json',value)
        self.record_ownership(root)
        return self.snapshot()

    def alternatives(self,root):
        current=content.load(root/'compatibility.json')
        if (root/graphics.STATE).is_dir():
            _,versions,_=graphics._load_state(root,full_inventory=False)
            return {('dxvk' if enabled else 'native'):json.loads(values[graphics.COMPAT])['content_id']
                    for enabled,values in versions.items()}
        before=graphics.encoded(current)
        return {'native':current['content_id'],'dxvk':json.loads(graphics._expected(before))['content_id']}

    def lan_list(self):
        root=self.game();interfaces=enumerate_interfaces();directory=LanDiscovery(interfaces)
        expected=content.load(root/'compatibility.json')['content_id'];alternatives=self.alternatives(root)
        probe=self.probe()
        rooms=directory.list_rooms(timeout=1)
        def status(room):
            if room.compatible(expected):return 'ready'
            mode=next((key for key,value in alternatives.items() if value==room.content_id),None)
            if mode is None:return 'different'
            return 'switch' if mode=='native' or probe['devices'] else 'unsupported'
        return {'networks':[{'key':i.address,'name':i.name} for i in interfaces],
                'rooms':[{'key':r.room_id,'network':r.local_interface.address,'name':r.name,
                          'members':r.members,'capacity':r.capacity,'status':status(r)} for r in rooms]}

    @staticmethod
    def validate_lan(request):
        if not isinstance(request,dict) or set(request)!={'player_name'}:raise ValueError('request_invalid')
        player_settings.player_name(request['player_name'])

    def align_lan(self,request):
        self.validate_lan(request)
        from lan_autoconnect import discover_preferred
        self.progress('lan_search')
        root=self.game();interfaces=enumerate_interfaces();directory=LanDiscovery(interfaces)
        current=content.load(root/'compatibility.json')['content_id']
        alternatives=self.alternatives(root)
        room=discover_preferred(directory,current,acceptable_content_ids=tuple(alternatives.values()),
            progress=self.progress,cancel_check=lambda:self.progress('lan_search'))
        if room is None:
            self.renderer(root,self.installed()['renderer_policy'],self.probe());return
        if current!=room.content_id:
            mode=next((key for key,value in alternatives.items() if value==room.content_id),None)
            if mode is None:raise ValueError('version_mismatch')
            probe=self.probe();self.renderer(root,mode,probe)
            with graphics.session_lock(root):
                selected=self.default_choice(root,mode,probe,choice._load_choice(root));choice.save_choice(root,selected)
            self.record_ownership(root)

    def play(self,request=None):
        if request is not None:self.validate_lan(request)
        root=self.game();self.progress('verify')
        if self.installed()['package_id']!=self.package_id():raise ValueError('update_required')
        # The binary/ABI checks and full game manifest checks remain in place.
        if request is None:content.verify(root)
        graphics.ensure_closed()
        import memory_guard
        def before():
            graphics.ensure_closed();memory_guard.require_host_headroom();self.progress('starting')
        def monitor(game):
            observer=memory_guard.Sentinel(game,root/'LocalData/desktop-memory-summary.json',
                memory_guard.Probe(game,root/'Base/Bin/Settlers7R.exe'),
                warn=lambda level:self.emit('warning',code='memory_warning',level=level))
            try:observer.start()
            except BaseException:
                if game.poll() is None:game.terminate();game.wait(timeout=10)
                raise
            self.emit('game_started',pid=game.pid)
            return observer
        # Repair saved monitor selections after a real hotplug, before launch.
        probe=self.probe()
        if request is None:mode=self.renderer(root,self.installed()['renderer_policy'],probe)
        else:
            self.align_lan(request);mode=choice._renderer(root,{})[0]
        with graphics.session_lock(root):
            selected=self.default_choice(root,mode,probe,choice._load_choice(root))
            choice.save_choice(root,selected)
        if request is None:
            args=SimpleNamespace(bundle=str(root),dotnet=str(root/'Runtime/dotnet/dotnet.exe'),
                server=str(root/'Server/S7Patcher.dll'),local_connect_probe=True,local_session_display_probe=False,
                launch_game=True,saved_graphics=True,before_game=before,game_monitor=monitor)
            code=launch_local.run(args)
        else:
            code=launch_auto_lan.run(SimpleNamespace(bundle=root,check_only=False,saved_graphics=True,
                player_name=request['player_name'],progress=self.progress,
                cancel_check=lambda:self.progress('lan_search'),before_game=before,game_monitor=monitor))
        self.emit('game_finished',exit_code=code)
        if code:raise RuntimeError('game_failed')
        return {'exit_code':code}

    def verify(self):
        self.progress('verify');content.verify(self.game());return {'verified':True}

    def cleanup_instance(self,container,name):
        root=self.instance(container,name)
        record=self.read(root/'desktop-files.json')
        if not record or set(record)!={'format','owner','files'} or record['owner']!=self.owner or record['format']!=FORMAT:
            raise ValueError('ownership_invalid')
        files=record['files']
        if not isinstance(files,dict) or len(files)>30000:raise ValueError('ownership_invalid')
        validated=[]
        for relative,digest in files.items():
            if (not isinstance(relative,str) or '\\' in relative or ':' in relative or
                    Path(relative).is_absolute() or '..' in Path(relative).parts or
                    (Path(relative).parts and Path(relative).parts[0].casefold()=='localdata') or not isinstance(digest,str) or
                    not re.fullmatch('[0-9a-f]{64}',digest)):
                raise ValueError('ownership_invalid')
            path=graphics._plain(root/relative)
            if not path.is_relative_to(root):raise ValueError('ownership_invalid')
            if relative not in ('desktop-owner.json',):validated.append((path,digest))
        # Runtime/hub processes may survive their owner's game intentionally.
        script='[Console]::OutputEncoding=New-Object Text.UTF8Encoding($false); @((Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {$_.Name -in @("Settlers7R.exe","dotnet.exe","python.exe")} | ForEach-Object {$_.ExecutablePath})) | ConvertTo-Json -Compress'
        result=subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-Command',script],
            capture_output=True,timeout=20,creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode or len(result.stdout)>65536:raise ValueError('managed_copy_busy')
        paths=json.loads(result.stdout.decode('utf-8-sig')) or []
        if isinstance(paths,str):paths=[paths]
        if any(p and Path(p).is_relative_to(root) for p in paths):raise ValueError('managed_copy_busy')
        marker=self.read(root/'LocalData/desktop-firewall.json')
        if marker is not None:
            key=hashlib.sha256(str(root).casefold().encode('utf-8')).hexdigest()[:16].upper()
            if marker!={'format':'s7-private-firewall-v1','key':key}:raise ValueError('ownership_invalid')
            helper=graphics._plain(root/'Launcher/Configure-LAN-Firewall.ps1')
            if content.digest(helper)!=files.get('launcher/configure-lan-firewall.ps1'):
                raise ValueError('ownership_invalid')
            result=subprocess.run(['powershell.exe','-NoLogo','-NoProfile','-NonInteractive','-WindowStyle','Hidden',
                '-File',str(helper),'-Bundle',str(root),'-Remove'],capture_output=True,
                creationflags=subprocess.CREATE_NO_WINDOW)
            if result.returncode:raise ValueError('firewall_remove_failed')
        kept=0
        for index,(path,digest) in enumerate(validated):
            self.progress('cleanup',index,len(validated))
            if path.is_file():
                if content.digest(path)==digest:path.unlink()
                else:kept+=1
        # Empty directories only. Personal/unknown/modified files remain.
        for directory in sorted((p for p in root.rglob('*') if p.is_dir()),key=lambda p:len(p.parts),reverse=True):
            graphics._plain(directory)
            try:directory.rmdir()
            except OSError:pass
        return {'kept_modified':kept}

    def clean_old(self):
        graphics.ensure_closed();value=self.installed()
        if not value:return {'cleaned':0}
        count=0
        for instance in value['previous']:
            self.progress('cleanup');self.cleanup_instance(value['container'],instance);count+=1
        value['previous']=[];self.write(self.state/'installation.json',value)
        return {'cleaned':count}

    def uninstall(self):
        graphics.ensure_closed();value=self.installed()
        if value:
            for name in [value['current'],*value['previous']]:self.cleanup_instance(value['container'],name)
            self.write(self.state/'retained-profile.json',{'container':value['container'],'instance':value['current']})
            graphics._plain(self.state/'installation.json').unlink()
        return {'personal_data_kept':True}

    def diagnostics(self):
        root=self.game();mode=choice._renderer(root,{})[0]
        probe=self.validate_probe(self.read(self.state/'graphics-probe.json'))
        raw=self.read(root/'LocalData/desktop-memory-summary.json',{})
        fields=('samples','max_pressure','peak_private_mib','min_free_mib','min_largest_mib','monitor_failed',
                'recovery_required','min_host_physical_free_mib','min_host_commit_free_mib')
        summary={key:raw[key] for key in fields if key in raw and
                 (raw[key] is None or type(raw[key]) in (bool,int) and 0<=raw[key]<=2**63-1)}
        return {'format':'s7-desktop-diagnostics-v1','release':'RC2','game_bits':32,'renderer':mode,
                'graphics_devices':probe['devices'],'probe_status':probe['failure'],
                'memory_summary':summary,
                'known_lan_desync_resolved':False}

    def reset_installation(self):
        """Explicit GUI recovery: preserve unknown metadata and every game file."""
        graphics.ensure_closed()
        path=graphics._plain(self.state/'installation.json')
        if path.exists():
            before=graphics._read(path,4*1024*1024)
            backup=graphics._plain(self.state/('installation-recovery-'+uuid.uuid4().hex+'.json'))
            graphics.atomic_write(backup,before,self.state)
            if graphics._read(path,4*1024*1024)!=before:raise ValueError('operation_busy')
            path.unlink()
        return self.snapshot()


def dispatch(desktop,request):
    if not isinstance(request,dict) or set(request)!={'format','action','data'} or request['format']!=REQUEST:
        raise ValueError('request_invalid')
    action,data=request['action'],request['data']
    if not isinstance(data,dict):raise ValueError('request_invalid')
    if action=='snapshot' and not data:return desktop.snapshot()
    if action=='detect' and not data:
        desktop.progress('source')
        previous=desktop.installed()
        locations=([Path(previous['source'])] if previous else [])+game_detection.candidates()
        return {'sources':game_detection.detect(locations)}
    with desktop.lock():
        if action in ('install','repair') and set(data)<= {'source','container'}:
            previous=desktop.installed()
            source=data.get('source') or (previous['source'] if previous else None)
            if not isinstance(source,str) or not source:raise ValueError('source_missing')
            container=data.get('container')
            if container is not None and not isinstance(container,str):raise ValueError('request_invalid')
            return desktop.install_game(source,container)
        if action=='save':return desktop.save_settings(data)
        if action=='play' and not data:return desktop.play()
        if action=='lan-list' and not data:return desktop.lan_list()
        if action=='lan-play':return desktop.play(data)
        if action=='probe' and not data:return desktop.probe(refresh=True)
        if action=='verify' and not data:return desktop.verify()
        if action=='cleanup' and not data:return desktop.clean_old()
        if action=='uninstall' and not data:return desktop.uninstall()
        if action=='diagnostics' and not data:return desktop.diagnostics()
        if action=='reset-installation' and not data:return desktop.reset_installation()
        raise ValueError('request_invalid')


def error_code(error):
    if getattr(error,'code',None) in ('lan_unreachable','lan_version','lan_multiple','lan_full','lan_changing'):
        return error.code
    text=str(error)
    known={'request_invalid','not_installed','installation_invalid','package_invalid','operation_busy',
        'renderer_unsupported','destination_invalid','preferences_invalid','quality_invalid','quality_ini_invalid',
        'quality_ini_changed','lobby_gone','version_mismatch','network_changed','ownership_invalid',
        'managed_copy_busy','source_missing','game_failed','cancelled','probe_invalid','update_required','firewall_remove_failed'}
    if text in known:return text
    if '4 GiB' in text:return 'memory_low'
    if 'laeuft' in text or 'running' in text or 'schliessen' in text:return 'game_running'
    if 'Speicher' in text and 'genug' in text:return 'disk_low'
    if 'Spielversion' in text:return 'source_invalid'
    if 'LAN' in text or 'Netzwerk' in text or 'Lobby' in text:return 'lan_failed'
    return 'operation_failed'


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--app',required=True);parser.add_argument('--operation',required=True)
    args=parser.parse_args()
    output=sys.stdout;lock=threading.Lock()
    def emit(kind,**fields):
        with lock:
            output.write(json.dumps({'kind':kind,**fields},ensure_ascii=True)+'\n');output.flush()
    desktop=None
    try:
        app=graphics._plain(args.app)
        if Path(__file__).resolve()!=app/'Package/Tools/installer/desktop_backend.py':raise ValueError('package_invalid')
        desktop=Desktop(app,emit,args.operation)
        request=json_record(sys.stdin.buffer.read(32769),32768)
        with redirect_stdout(Console(emit)):
            result=dispatch(desktop,request)
        emit('result',data=result);return 0
    except (OSError,ValueError,RuntimeError,TimeoutError,KeyError,TypeError) as error:
        emit('error',code=error_code(error),detail=str(error)[:512]);return 2
    finally:
        if desktop is not None and desktop.cancel.exists():desktop.cancel.unlink()


if __name__=='__main__':raise SystemExit(main())
