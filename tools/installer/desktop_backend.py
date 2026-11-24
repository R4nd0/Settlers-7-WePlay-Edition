"""Bounded desktop IPC over stdin/stdout; one managed application per command.

No free-form command execution or endpoint entry. The GUI supplies selections;
the existing installer, profile journal, launch locks and LAN checks do the work.
"""
import argparse
from contextlib import ExitStack, contextmanager, redirect_stdout
from functools import wraps
import hashlib
import http.client
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
import desktop_settings
from lan_discovery import LanDiscovery
from lan_hub import LanHubError
from lan_interfaces import enumerate_interfaces
from lobby_directory import DISCOVERY_PORT, json_record, encode, validate_discovery_port
from lobby_picker import read_choice
from standalone_entry import NativeStartupError

FORMAT='s7-desktop-install-v1'
INSTALL_FORMAT='s7-desktop-install-v2'
REQUEST='s7-desktop-request-v1'
HEX=re.compile('[0-9a-f]{32}')


class Cancelled(RuntimeError): pass


def verified_operation(method):
    @wraps(method)
    def invoke(self,*args,**kwargs):
        with self.verification():return method(self,*args,**kwargs)
    return invoke


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
        self._verification=None
        self._settings_lock_held=False

    @contextmanager
    def verification(self):
        previous=self._verification
        if previous is None:self._verification=install.VerifiedPackage(self.package)
        try:yield
        finally:self._verification=previous

    def package_manifest(self):
        value=self._verification.load() if self._verification is not None else install.package_manifest(self.package)
        import release_policy
        release_policy.check_package(self.app,value)
        return value

    def is_gold(self,root=None):
        import gold_support
        return gold_support.is_gold(self.game() if root is None else root)

    def verify_instance(self,root):
        if self.is_gold(root):
            import gold_support
            value=gold_support.verify(self.package,root)
        else:value=content.verify(root)
        if self.package_manifest().get('platform_independent',False):
            import platform_standalone
            platform_standalone.load(root)
        return value

    def operation_state(self,phase):self.emit('operation_state',phase=phase)

    def progress(self,phase,current=0,total=0):
        if not self.finalizing and self.cancel.exists():raise Cancelled('cancelled')
        if phase=='patch':self.finalizing=True;self.operation_state('critical')
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
        return self.validate_installation(value)

    def validate_installation(self,value):
        if not isinstance(value,dict):raise ValueError('installation_invalid')
        if (set(value)!={'format','source','container','current','previous','package_id','renderer_policy'}
                or value['format'] not in (FORMAT,INSTALL_FORMAT) or value['renderer_policy'] not in ('auto',*graphics.RENDERERS)
                or not isinstance(value['source'],str) or not isinstance(value['previous'],list)
                or len(value['previous'])>16 or not isinstance(value['package_id'],str)):
            raise ValueError('installation_invalid')
        self.instance(value['container'],value['current'])
        references=[];seen={value['current']}
        for item in value['previous']:
            if value['format']==FORMAT:
                if not isinstance(item,str) or not HEX.fullmatch(item):raise ValueError('installation_invalid')
                container=value['container']
                # Pre-release v1 lost old locations when the destination changed.
                # Recover only exact owned IDs in this app's original default;
                # never search drives or adopt an existing foreign directory.
                declared=graphics._plain(Path(container)/item)
                if not declared.exists():container=str(self.app/'Instances')
                item={'container':container,'instance':item}
            if not isinstance(item,dict) or set(item)!={'container','instance'}:raise ValueError('installation_invalid')
            root=self.instance(item['container'],item['instance'])
            if root.name in seen:raise ValueError('installation_invalid')
            seen.add(item['instance']);references.append({'container':str(root.parent),'instance':root.name})
        policy=value['renderer_policy']
        if policy=='auto' or (policy!='dxvk' and policy not in self.supported_renderers()):
            policy='dxvk'
        value={**value,'format':INSTALL_FORMAT,'previous':references,'renderer_policy':policy}
        return value

    def instance(self,container,name):
        if not isinstance(container,(str,Path)) or not container or not Path(container).is_absolute():raise ValueError('installation_invalid')
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
        value=self.package_manifest() if full else install.package_metadata(self.package)
        if not value or not isinstance(value.get('files'),dict):raise ValueError('package_invalid')
        # GUI/backend-only updates do not require another 7 GB game copy.
        # Every file affecting the copied game/launch contract still participates.
        mapped=[*install.GRAPHICS_TOOLS,*install.DISPLAY_TOOLS,*install.DESKTOP_TOOLS,*install.LAN_LOBBY_TOOLS,
                ('offline-ui/launch_local.py',''),('installer/compatibility.py',''),
                ('installer/start.py',''),('installer/launch_lan.py',''),('installer/install.py',''),
                ('offline-ui/patch-spec.json',''),('offline-ui/join-menu-update.json',''),
                ('installer/lan_public_game.py',''),('offline-ui/lan-public-creation.json','')]
        mapped += [(name,'') for name in install.ACHIEVEMENT_PACKAGE_TOOLS]
        mapped += [(name,'') for name in install.CARTOGRAPHER_PACKAGE_TOOLS]
        mapped += [(name,'') for name in install.MULTIPLAYER_RESUME_TOOLS]
        names={('tools/'+name).lower() for name,_ in mapped}
        records={name:digest for name,digest in value['files'].items() if name in names or
                 name.startswith(('runtime/','payload/server/')) or name in
                 ('payload/uplay_r1_loader.dll','payload/graphics/d3d9.dll','tools/license.txt')}
        flags={key:value.get(key,False) for key in ('graphics_choice','display_selection','automatic_lan','desktop_launcher')}
        if value.get('achievement_notifications_disabled',False):flags['achievement_notifications_disabled']=True
        if value.get('cartographer_context',False):flags['cartographer_context']=True
        if value.get('multiplayer_castles',False):flags['multiplayer_castles']=True
        if value.get('multiplayer_resume',False):flags['multiplayer_resume']=True
        if value.get('midsummer_profile',False):
            flags['midsummer_profile']=True
            effects={'tools/'+name.lower() for name in install.MIDSUMMER_PACKAGE_TOOLS}
            records.update({name:digest for name,digest in value['files'].items() if name in effects})
        for key in ('platform_independent','main_package','gold_standalone','crossplay_mixed','alternate_renderers','compat_smaa_probe'):
            if value.get(key,False):flags[key]=True
        if value.get('platform_independent',False):
            import platform_standalone
            records.update({name:hashed for name,hashed in value['files'].items() if
                name.startswith(('payload/platform/','payload/gold/','payload/crossplay/')) or name in
                {'tools/installer/'+n for n in (*platform_standalone.TOOLS,'gold_support.py','release_policy.py')}})
            flags['platform_isolation']=value.get('platform_isolation')
        if value.get('windows_helpers',False):
            flags['windows_helpers']=True
            records.update({name:digest for name,digest in value['files'].items() if name=='tools/installer/windows_system.py'})
        return content.fingerprint({'files':records,'archive':value.get('patched_archive_sha256'),
             'flags':flags})

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
        manifest=self.package_manifest()
        signature=manifest['files'].get('payload/graphics/s7graphicsprobe.exe')
        if not signature or content.digest(program)!=signature or content.digest(dll)!=graphics.DXVK_SHA256:
            raise ValueError('package_invalid')
        adapters=sorted({screen['adapter'] for screen in self.displays()})
        if not refresh:
            try:
                previous=self.validate_probe(self.read(path,limit=65536))
            except (OSError,ValueError,TypeError):
                previous=None  # Generated telemetry can be rebuilt.
            if (previous is not None and previous['probe_sha256']==signature
                    and previous.get('renderer_sha256')==graphics.DXVK_SHA256
                    and previous['adapters']==adapters and previous['failure']!='probe_failed'):
                return previous
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
            self.check_start_memory()
            result=subprocess.run([str(program),str(dll)],cwd=self.state,env=environment,
                capture_output=True,timeout=25,creationflags=subprocess.CREATE_NO_WINDOW)
            parsed=json_record(result.stdout,65536)
            if set(parsed)!={'format','devices'} or parsed['format']!='s7-render-probe-v1':
                raise ValueError('probe_invalid')
            reported=parsed['devices']
            if not isinstance(reported,list) or len(reported)>16:
                raise ValueError('probe_invalid')
            for name in reported:
                if not isinstance(name,str):
                    raise ValueError('probe_invalid')
                try:
                    choice.validate_choice({'format':choice.FORMAT,'choice':name})
                except ValueError:
                    continue  # The driver may leave one adapter description blank.
                if name not in devices:
                    devices.append(name)
            if result.returncode not in (0,2):raise ValueError('probe_invalid')
            if result.returncode or not devices:failure='unsupported'
        except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired):
            devices=[];failure='probe_failed'
        value=self.validate_probe({'format':'s7-desktop-probe-v1','probe_sha256':signature,
                                  'renderer_sha256':graphics.DXVK_SHA256,'adapters':adapters,
                                  'devices':devices,'failure':failure})
        self.write(path,value)
        return value

    def cached_graphics_probe(self):
        try:
            value=self.validate_probe(self.read(self.state/'graphics-probe.json',limit=65536))
            if value.get('renderer_sha256')==graphics.DXVK_SHA256:
                return value
        except (OSError,ValueError,TypeError):
            pass
        return {'devices':[], 'failure':None}

    @staticmethod
    def validate_probe(value):
        fields={'format','probe_sha256','devices','failure','adapters'}
        if (not isinstance(value,dict) or set(value) not in (fields,fields|{'renderer_sha256'})
                or value['format']!='s7-desktop-probe-v1' or not isinstance(value['devices'],list)
                or len(value['devices'])>16 or not isinstance(value['adapters'],list)
                or len(value['adapters'])>16 or value['failure'] not in (None,'unsupported','probe_failed')):
            raise ValueError('probe_invalid')
        hashes=[value['probe_sha256']] + ([value['renderer_sha256']] if 'renderer_sha256' in value else [])
        if (any(not isinstance(value,str) or not re.fullmatch('[0-9a-f]{64}',value) for value in hashes)
                or (value['failure'] is None and not value['devices'])):
            raise ValueError('probe_invalid')
        for name in [*value['devices'],*value['adapters']]:
            if not isinstance(name,str):raise ValueError('probe_invalid')
            choice.validate_choice({'format':choice.FORMAT,'choice':name})
        return value

    def displays(self):
        try:return display.enumerate_displays()[:2]
        except (OSError,ValueError,RuntimeError):return []

    def supported_renderers(self):
        metadata=install.package_metadata(self.package)
        return graphics.RENDERERS if metadata.get('alternate_renderers') is True else ('dxvk',)

    def network_setup_preferred(self, root=None):
        try:
            record=self.read(self.state/'network-setup.json',limit=1024)
            if record is not None:
                return (isinstance(record,dict) and set(record)=={'format','prepare'}
                        and record['format']=='weplay-network-setup-v1' and record['prepare'] is True)
            if root is None:return False
            marker=self.read(root/'LocalData/desktop-firewall.json',limit=1024)
            key=hashlib.sha256(str(root).lower().encode('utf-8')).hexdigest()[:16].upper()
            return marker=={'format':'s7-private-firewall-v1','key':key}
        except (OSError,ValueError):
            return False

    def remember_network_setup(self, prepare):
        try:
            self.write(self.state/'network-setup.json',{'format':'weplay-network-setup-v1','prepare':prepare})
            return True
        except (OSError,ValueError):
            return False

    def prepare_install_network(self, root, gold):
        import platform_standalone
        import windows_system
        failures=[]
        try:
            if platform_standalone.enabled(root):
                try:platform_standalone.configure(root,'check')
                except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired):
                    platform_standalone.configure(root)
        except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired):
            failures.append('network_isolation_setup_failed')
        if not gold:
            try:windows_system.lan_firewall(root)
            except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired):
                failures.append('network_private_lan_setup_failed')
        return failures

    def launch_renderer(self):
        policy=self.installed()['renderer_policy']
        return (policy,self.package) if policy!='dxvk' else None

    def recover_launch_renderer(self, root=None):
        if root is None:
            try:root=self.game()
            except ValueError as error:
                if str(error) in ('not_installed','installation_invalid'):return
                raise
        if not graphics._plain(root/graphics.RENDERER_JOURNAL).exists():return
        import platform_standalone
        with graphics.session_lock(root):
            graphics.ensure_closed()
            _,executable,_=platform_standalone.load(root)
            graphics.recover_launch_renderer(root,executable,self.package)

    def remember_lan_start(self, code):
        try:self.write(self.state/'lan-start.json',{'code':code})
        except (OSError,ValueError):pass

    def lan_discovery_port(self):
        try:
            value=self.read(self.state/'lan-discovery.json',limit=1024)
            if isinstance(value,dict) and set(value)=={'port'}:
                return validate_discovery_port(value['port'])
        except (OSError,ValueError):
            pass
        return DISCOVERY_PORT

    def save_lan_discovery_port(self,data):
        if set(data)!={'port'}:raise ValueError('request_invalid')
        port=validate_discovery_port(data['port'])
        self.write(self.state/'lan-discovery.json',{'port':port})
        return {'lan_discovery_port':port}

    def snapshot(self,*,displays=None):
        value=self.installed()
        metadata=install.package_metadata(self.package)
        result={'installed':value is not None,'displays':self.displays() if displays is None else displays,
                'data_default':str(value['container'] if value else self.app/'Instances'),'source':str(self.game()) if value else '',
                'renderer_policy':value['renderer_policy'] if value else 'dxvk',
                'renderer_modes':self.supported_renderers(),
                'compat_smaa_supported':metadata.get('compat_smaa_probe',False),
                'network_setup_preferred':self.network_setup_preferred(self.game() if value else None),
                'lan_discovery_port':self.lan_discovery_port(),
                'probe':self.cached_graphics_probe(),
                'choice':{'format':choice.FORMAT,'choice':None,'simple_menu':True,'display':None},
                'quality':dict(quality.DEFAULTS),'player_name':'LocalPlayer','renderer':value['renderer_policy'] if value else 'dxvk',
                'current':None,'update_available':False,'needs_repair':False,'graphics_backup':'none',
                'maintenance_pending':len(value['previous']) if value else 0,
                'internet_available':metadata.get('internet_easytier',False) is True,
                'crossplay_available':metadata.get('crossplay_mixed',False) is True}
        result['updates_supported']=metadata.get('update_patcher',False) is True
        result['update_last_error']=None
        if result['updates_supported']:
            try:
                update_status=self.read(self.state/'Updates/status.json',{},limit=8192)
                if isinstance(update_status,dict) and update_status.get('format')=='weplay-update-status-v1' and update_status.get('phase')=='failed':
                    code=update_status.get('code','')
                    result['update_last_error']=code if isinstance(code,str) and re.fullmatch('update_[a-z_]{1,48}',code) else 'update_install_failed'
            except (OSError,ValueError,TypeError):result['update_last_error']='update_invalid'
        result['discord_available']=metadata.get('discord_directory',False) is True
        result['directory_available']=metadata.get('game_directory',False) is True
        result['public_directory_available']=metadata.get('public_game_directory',False) is True
        result['directory_connected']=False
        if result['directory_available']:
            from game_directory import configured
            result['directory_connected']=configured(self.state) is not None
        result['discord_connected']=False;result['discord_publish_enabled']=False
        if result['discord_available']:
            from discord_credentials import connected,public_settings
            result['discord_connected']=connected(self.state)
            if result['discord_connected']:result['discord_publish_enabled']=public_settings(self.state)['publish_enabled']
        if value:
            root=self.game()
            result.update(current=str(root),update_available=value['package_id']!=self.package_id())
            backup=desktop_settings.graphics_backup(self,root)
            if backup is not None:result['graphics_backup']='confirmed' if backup[0] else 'previous'
            if self.is_gold(root):
                result.update(edition='gold',single_player_only=True,
                    internet_available=False,discord_available=False,directory_available=False,public_directory_available=False,
                    visual_detail_supported=False,midsummer_tone_supported=False,
                    maximum_fx_supported=False)
                try:
                    self.verify_instance(root)
                    maximum=choice.lod_maximum(root)
                    result.update(choice=choice._read_choice(root),quality=quality.load(root),
                        visual_detail_supported=True,lod_steps_supported=maximum>1,lod_maximum=maximum,
                        midsummer_tone_supported=metadata.get('midsummer_profile',False))
                except (OSError,ValueError,RuntimeError):result['needs_repair']=True
                return result
            try:
                result['visual_detail_supported']=choice.visual_supported(root)
                result['lod_maximum']=choice.lod_maximum(root)
                result['lod_steps_supported']=result['lod_maximum']>1
                result['midsummer_tone_supported']=(root/'Launcher/reshade_runtime.py').is_file() and (root/'Launcher/WePlay-Midsummer.fx').is_file()
                choice._renderer(root,{})
                result.update(choice=choice._read_choice(root),quality=quality.load(root),
                              player_name=player_settings.load(root))
            except (OSError,ValueError,RuntimeError):result['needs_repair']=True
        return result

    def renderer(self,root,policy,probe=None):
        # Accept the optional third argument used by older standalone scripts.
        # This installs and verifies the files. Only the game needs a real GPU.
        if policy not in ('auto','dxvk','native'):raise ValueError('request_invalid')
        if self.is_gold(root):
            import gold_graphics
            gold_graphics.checked_root(root)
            return 'dxvk'
        # Keep the installed manifest canonical. Alternative DLLs are applied
        # only after the game and network content checks, under the session lock.
        wanted='dxvk'
        pending=graphics._plain(root/graphics.STATE/'transaction.json').exists()
        if not pending and choice._renderer(root,{})[0]==wanted:return wanted
        self.progress('graphics_apply')
        if self._settings_lock_held:
            graphics._change_locked(root,True,self.package/'Payload/Graphics/d3d9.dll')
        else:graphics.install(root,self.package/'Payload/Graphics/d3d9.dll')
        return 'dxvk'

    def restore_settings_renderer(self,root,mode,expected):
        if self.is_gold(root):
            if mode!='dxvk' or expected is not None:raise ValueError('preferences_invalid')
            import gold_graphics
            gold_graphics.checked_root(root)
            return
        if (root/graphics.STATE).exists():
            _,versions,_=graphics._load_state(root)
            if versions[mode=='dxvk'][graphics.COMPAT]!=expected:raise ValueError('preferences_invalid')
            graphics._change_locked(root,mode=='dxvk',self.package/'Payload/Graphics/d3d9.dll' if mode=='dxvk' else None)
        elif mode!='native' or desktop_settings.read(root/graphics.COMPAT)!=expected:
            raise ValueError('preferences_invalid')
        if desktop_settings.read(root/graphics.COMPAT)!=expected:raise ValueError('preferences_invalid')

    def recovery_display(self,screens):
        if not screens:return None
        target=next((screen for screen in screens if screen.get('primary')),screens[0])
        sizes=[(w,h) for w,h in target['resolutions'] if w<=1920 and h<=1080]
        if not sizes:return None
        width,height=max(sizes,key=lambda size:size[0]*size[1])
        return dict(device=target['device'],width=width,height=height,mode='windowed')

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
            if selected.get('display') is None and (preferred is None or preferred.get('display') is not None):
                selected['display']=self.recovery_display(screens)
                if selected['display'] is not None:
                    target=next(screen for screen in screens if screen['device']==selected['display']['device'])
                    if (preferred is None and (selected['display']['width'],selected['display']['height'])
                            ==(target['width'],target['height'])):
                        selected['display']['mode']='borderless'
        elif selected.get('display') is not None:
            selected['display']=None
        if renderer=='native':
            selected['choice']=None
            for name in ('midsummer_tone','enhanced_aa','contact_shadows','maximum_fx'):
                if name in selected:selected[name]=False
        elif selected['choice'] not in probe['devices'] or (
                target and selected['display'] is not None
                and selected['choice'].casefold()!=target['adapter'].casefold()):
            selected['choice']=None
        return selected

    def standalone_descriptor(self, record):
        import standalone
        bundled = Path(record['container']) == self.app/'Instances'
        return {'format': standalone.FORMAT if bundled else standalone.MANAGED_FORMAT,
                'instance': record['current'], 'package_id': record['package_id']}

    def recover_publication(self):
        """Recover the metadata pair under the desktop operation lock."""
        path=self.state/'installation-transaction.json';journal=self.read(path)
        if journal is None:return
        if (not isinstance(journal,dict) or set(journal)!={'format','owner','before','after','descriptor_before','descriptor_after'}
                or journal['format']!='weplay-install-publication-v1' or journal['owner']!=self.owner):
            raise ValueError('installation_invalid')
        before,after=journal['before'],journal['after']
        if before is not None:self.validate_installation(before)
        self.validate_installation(after)
        descriptor=self.app.parent/'standalone.json'
        db,da=journal['descriptor_before'],journal['descriptor_after']
        if (db is None)!=(da is None):raise ValueError('installation_invalid')
        for value,record in ((db,before),(da,after)):
            if value is not None and (record is None or value != self.standalone_descriptor(record)):
                raise ValueError('installation_invalid')
        current=self.read(self.state/'installation.json')
        portable=self.read(descriptor,limit=4096) if db is not None else None
        if current not in (before,after) or portable not in (db,da):raise ValueError('installation_invalid')
        if current==after and portable==da:
            # Only a fully verified target can complete an interrupted commit.
            self.verify_instance(self.instance(after['container'],after['current']))
            if after['package_id']!=self.package_id(full=True):raise ValueError('installation_invalid')
        else:
            if current!=before:
                if before is None:graphics._plain(self.state/'installation.json').unlink()
                else:self.write(self.state/'installation.json',before)
            if db is not None and portable!=db:self.write(descriptor,db)
        graphics._plain(path).unlink()

    def publish_installation(self,record):
        if self.package_manifest().get('platform_independent',False):
            import release_policy
            self.write(self.state/release_policy.FLOOR,release_policy.CONTRACT)
        before=self.read(self.state/'installation.json');descriptor=self.app.parent/'standalone.json'
        if before is not None:self.validate_installation(before)
        db=self.read(descriptor,limit=4096) if descriptor.is_file() else None
        da=None
        if db is not None:
            if before is None or db != self.standalone_descriptor(before):
                raise ValueError('standalone_invalid')
            da=self.standalone_descriptor(record)
        journal=self.state/'installation-transaction.json'
        self.write(journal,{'format':'weplay-install-publication-v1','owner':self.owner,
            'before':before,'after':record,'descriptor_before':db,'descriptor_after':da})
        try:
            self.write(self.state/'installation.json',record)
            if da is not None:self.write(descriptor,da)
        except BaseException:
            # A failed second write rolls back the active selection. A hard
            # interruption leaves the journal for the next locked startup.
            self.recover_publication()
            raise
        try:
            graphics._plain(journal).unlink()
        except OSError:
            # Both records committed. Keep this exact generation until its
            # journal can be retired before changing history again.
            self.emit('warning',code='cleanup_deferred')
            return False
        return True

    def preference_bytes(self,old):
        if old is None:return 0
        total=0
        for name in ('local-profile-id.txt','player-settings.json','graphics-choice.json','graphics-quality.json'):
            path=graphics._plain(old/'LocalData'/name)
            if path.is_file():total+=path.stat().st_size
        for folder in ('saves','ProfileRoot'):
            saves=graphics._plain(old/'LocalData'/folder)
            if saves.is_dir():
                for path in saves.rglob('*'):
                    path=graphics._plain(path)
                    if path.is_file():total+=path.stat().st_size
        return total

    def cleanup_preparations(self):
        folder=graphics._plain(self.state/'Preparations')
        if not folder.is_dir():return
        active=self.installed();pending=False
        for path in folder.glob('*.json'):
            if not HEX.fullmatch(path.stem):continue
            value=self.read(path)
            if (not isinstance(value,dict) or set(value)!={'format','owner','container','instance','allocated'}
                    or value['format']!='weplay-preparation-v1' or value['owner']!=self.owner
                    or value['instance']!=path.stem or type(value['allocated']) is not bool
                    or not isinstance(value['container'],str) or not Path(value['container']).is_absolute()):
                raise ValueError('ownership_invalid')
            root=graphics._plain(Path(value['container'])/value['instance'])
            if active and root==Path(active['container'])/active['current']:
                path.unlink();continue
            if not root.exists():path.unlink();continue
            if not value['allocated']:
                names=set()
                ownership_files={'desktop-owner.json','desktop-files.json'}
                for child in root.iterdir():
                    names.add(child.name)
                    if child.name not in ownership_files:
                        break
                if names:
                    marker={'format':FORMAT,'owner':self.owner,'instance':value['instance']}
                    empty={'format':FORMAT,'owner':self.owner,'files':{}}
                    if (not names<=ownership_files or self.read(root/'desktop-owner.json')!=marker
                            or ('desktop-files.json' in names and self.read(root/'desktop-files.json')!=empty)):
                        pending=True
                        continue
                    # Allocation was interrupted before any payload was
                    # copied. Remove only the exact receipts already written.
                    for name in sorted(names):
                        graphics._plain(root/name).unlink()
                root.rmdir();path.unlink()
                continue
            try:
                self.cleanup_instance(value['container'],value['instance'])
            except ValueError as error:
                if str(error) not in ('managed_copy_busy','firewall_remove_failed'):
                    raise
                pending=True
                continue
            except (OSError,subprocess.TimeoutExpired):
                pending=True
                continue
            # Keep a receipt for preserved personal/unknown/modified files.
            receipt=folder/'Retained';receipt.mkdir(exist_ok=True)
            self.write(receipt/path.name,value)
            path.unlink()
        if pending:self.emit('warning',code='cleanup_deferred')

    def transfer_preferences(self,old,new):
        # Same-PC update only. Renderer backup/journal, caches and live runtime
        # configuration are deliberately not migrated into another version.
        if shutil.disk_usage(new).free<self.preference_bytes(old)+64*1024*1024:
            raise ValueError('Nicht genug freier Speicher fuer die Spielstaende.')
        data=graphics._plain(new/'LocalData');data.mkdir(exist_ok=True)
        for name in ('local-profile-id.txt','player-settings.json','graphics-choice.json','graphics-quality.json'):
            source=graphics._plain(old/'LocalData'/name)
            if source.is_file():
                try:
                    if source.stat().st_size>65536:raise ValueError('preferences_invalid')
                    if name=='player-settings.json':player_settings.load(old)
                    elif name=='graphics-choice.json':choice._load_choice(old)
                    elif name=='graphics-quality.json':quality.load(old)
                except (OSError,ValueError):
                    self.emit('warning',code='preferences_reset');continue
                shutil.copyfile(source,graphics._plain(data/name))
        for folder in ('saves','ProfileRoot'):
            saves=graphics._plain(old/'LocalData'/folder)
            if not saves.is_dir():continue
            for path in saves.rglob('*'):graphics._plain(path)
            def copy_progress(source,target):
                source,target=map(graphics._plain,(source,target))
                # A newly prepared profile has empty directories. Never replace
                # an independently created/newer save while publishing repair.
                if target.exists():
                    if content.digest(source)!=content.digest(target):raise ValueError('profile_changed')
                    return str(target)
                with source.open('rb') as incoming,target.open('xb') as outgoing:
                    shutil.copyfileobj(incoming,outgoing,1024*1024)
                shutil.copystat(source,target,follow_symlinks=False)
                if content.digest(source)!=content.digest(target):raise ValueError('profile_changed')
                return str(target)
            shutil.copytree(saves,data/folder,dirs_exist_ok=True,copy_function=copy_progress)

    @verified_operation
    def install_game(self,source,container=None,*,prepare_network=None):
        if prepare_network is not None and type(prepare_network) is not bool:
            raise ValueError('request_invalid')
        self.finalizing=False
        graphics.ensure_closed()
        self.recover_publication()
        self.cleanup_preparations()
        source=graphics._plain(source)
        previous=self.installed()
        managed=bool(previous and source==self.game())
        manifest=self.package_manifest()
        import release_policy
        release_policy.preserve_game(source,manifest)
        gold=self.is_gold(source) and manifest.get('gold_standalone',False)
        repairable=managed or (source/('gold-install.json' if gold else 'local-bundle.json')).is_file()
        supported=manifest.get('steam_history',False)
        if gold:
            import gold_support
            gold_support.inspect(self.package,source,managed=repairable)
        elif repairable:install.repair_source(source,steam_supported=supported,tools=self.package/'Tools')
        else:install.source_base(source,steam_supported=supported)
        # Never discard an older owned location to make room in the bounded
        # history. Retry its cleanup before allocating another full game copy.
        if previous and len(previous['previous'])>=16:
            self.clean_old(automatic=True)
            previous=self.installed()
            if len(previous['previous'])>=16:raise ValueError('cleanup_required')
        base=graphics._plain(container or (previous['container'] if previous else self.app/'Instances'))
        if previous:
            references=[{'container':previous['container'],'instance':previous['current']},*previous['previous']]
            # Selecting our existing copy means keep its storage location,
            # rather than nesting another full copy inside the old instance.
            owned=[self.instance(item['container'],item['instance']) for item in references]
            if base in owned:base=base.parent
            if any(base.is_relative_to(path) for path in owned):raise ValueError('destination_invalid')
        if base==source or base.is_relative_to(source) or (source.is_relative_to(base) and not managed):
            raise ValueError('destination_invalid')
        if base.is_relative_to(self.package) or self.package.is_relative_to(base):
            raise ValueError('destination_invalid')
        instance=uuid.uuid4().hex
        root=base/instance
        if manifest.get('platform_independent',False):
            from platform_standalone import validate_location
            validate_location(root,gold=gold)
        base.mkdir(parents=True,exist_ok=True)
        self.progress('source')
        old=source if repairable else self.game() if previous else None
        if old is None:
            retained=self.read(self.state/'retained-profile.json')
            if retained:
                try:old=self.instance(retained['container'],retained['instance'])
                except (OSError,ValueError,KeyError):pass
        preparations=graphics._plain(self.state/'Preparations');preparations.mkdir(exist_ok=True)
        if len(list(preparations.glob('*.json')))>=32:raise ValueError('cleanup_required')
        pending=preparations/(instance+'.json')
        allocation={'format':'weplay-preparation-v1','owner':self.owner,'container':str(base),'instance':instance,'allocated':False}
        self.write(pending,allocation)
        tracked={}
        def created(target):
            if target!=root:raise ValueError('ownership_invalid')
            self.write(root/'desktop-owner.json',{'format':FORMAT,'owner':self.owner,'instance':instance})
            self.write(root/'desktop-files.json',{'format':FORMAT,'owner':self.owner,'files':tracked})
            allocation['allocated']=True;self.write(pending,allocation)
        def track(path,digest):
            relative=graphics._plain(path).relative_to(root).as_posix().lower()
            if relative.startswith('localdata/'):raise ValueError('ownership_invalid')
            tracked[relative]=digest
            self.write(root/'desktop-files.json',{'format':FORMAT,'owner':self.owner,'files':tracked})
        options={'managed':True} if repairable else {}
        try:
            if gold:
                gold_support.prepare(self.package,source,root,network_runtime=self.package_manifest().get('internet_easytier',False),
                    managed=repairable,on_created=created,tracker=track,progress=self.progress)
            else:
                install.prepare(self.package,source,root,emit=lambda text:self.emit('log',text=text),progress=self.progress,
                    publish_starters=False,additional_bytes=self.preference_bytes(old),on_created=created,tracker=track,**options)
            if old:
                self.transfer_preferences(old,root)
                if self.is_gold(old)==gold:
                    backup=desktop_settings.graphics_backup(self,old)
                    if backup is not None:
                        desktop_settings.remember_graphics(self,root,backup[1],confirmed=backup[0])
            policy=previous['renderer_policy'] if previous else 'dxvk'
            if policy not in self.supported_renderers():policy='dxvk'
            probe=self.cached_graphics_probe()
            mode=self.renderer(root,'dxvk')
            with graphics.session_lock(root):
                selected=self.default_choice(root,mode,probe,choice._load_choice(root) if old and self.is_gold(old)==gold else None)
                if old is None and policy=='dxvk' and manifest.get('midsummer_profile',False):
                    selected['enhanced_aa']=True
                choice.save_choice(root,selected,renderer=policy)
                self.write(root/quality.FILE,quality.without_game_aa(
                    quality.load(root),policy=='dxvk' and selected.get('enhanced_aa',False)))
            self.progress('verify')
            verified=self.verify_instance(root)
            self.record_ownership(root,verified)
            record={'format':INSTALL_FORMAT,'source':'' if managed else str(source),'container':str(base),'current':instance,
                    'previous':([{'container':previous['container'],'instance':previous['current']},*previous['previous']] if previous else []),
                    'package_id':self.package_id(),'renderer_policy':policy}
            self.progress('finish')
            history_writable=self.publish_installation(record)
        except BaseException:
            self.finalizing=True
            try:self.cleanup_preparations()
            except (OSError,ValueError,subprocess.TimeoutExpired):self.emit('warning',code='cleanup_deferred')
            raise
        try:pending.unlink()
        except OSError:self.emit('warning',code='cleanup_deferred')
        network_failures=[]
        if prepare_network is not None and not self.remember_network_setup(prepare_network):
            network_failures.append('network_setup_preference_failed')
        if prepare_network:
            self.progress('network_setup')
            network_failures.extend(self.prepare_install_network(root,gold))
        # Publish and verify first. Only then retire the old owned game files;
        # cleanup failure must not turn a successful installation into failure.
        self.finalizing=True
        if history_writable:
            try:self.clean_old(automatic=True)
            except (OSError,ValueError,subprocess.TimeoutExpired):self.emit('warning',code='cleanup_deferred')
        return {**self.snapshot(),'network_setup_failures':network_failures}

    def record_ownership(self,root,manifest=None):
        self.write(root/'desktop-files.json',self.ownership_record(root,manifest))

    def ownership_record(self,root,manifest=None):
        manifest=manifest or (self.verify_instance(root) if self.is_gold(root) else content.load(root/'compatibility.json'))
        files=dict(manifest['files'])
        # Runtime files were byte-verified by prepare; reuse those package hashes.
        package=self.package_manifest()
        if not self.is_gold(root):files.update({name:digest for name,digest in package['files'].items() if name.startswith('runtime/')})
        # Only generated metadata is owned. Never adopt an unknown root or
        # repair/staging file merely because it appeared during preparation.
        generated=('desktop-owner.json','compatibility.json','local-bundle.json','ArchiveStage/manifest.json',
            'ArchiveStage/Base/BBA/Interface.bba','ArchiveStage/Data/Base/BBA/Interface.bba',
            install.REPAIR+'/files.json',*(install.REPAIR+'/'+name for name in install.REPAIR_INPUTS))
        for name in generated:
            path=graphics._plain(root/name)
            if path.is_file():files[name.lower()]=content.digest(path)
        return {'format':FORMAT,'owner':self.owner,'files':files}

    @verified_operation
    def save_settings(self,request,*,remember_previous=True):
        gold=self.is_gold()
        if set(request)!={'renderer_policy','choice','quality','player_name'}:raise ValueError('request_invalid')
        policy=request['renderer_policy']
        if policy not in self.supported_renderers():raise ValueError('request_invalid')
        root=self.game();graphics.ensure_closed()
        manifest=self.package_manifest()
        if self.installed()['package_id']!=self.package_id():raise ValueError('update_required')
        selected=choice.validate_choice(request['choice']);quality_value=quality.validate(request['quality'])
        smaa_supported=policy=='dxvk' or (policy in graphics.ASYNC_RENDERERS and manifest.get('compat_smaa_probe',False))
        if not smaa_supported and selected.get('enhanced_aa',False):
            selected['enhanced_aa']=False
        quality_value=quality.without_game_aa(quality_value,selected.get('enhanced_aa',False))
        if gold:
            import gold_graphics
            gold_graphics.validate(root,selected)
        choice._require_visual_support(root,selected)
        player_settings.player_name(request['player_name'])
        probe=self.cached_graphics_probe()
        screens=self.displays()
        # Reject invalid display/GPU requests before making any profile change.
        if selected.get('display') is not None:display.resolve(selected['display'],screens)
        if policy=='dxvk' and selected['choice'] is not None and selected['choice'] not in probe['devices']:
            raise ValueError('renderer_unsupported')
        if policy=='dxvk' and selected.get('display') is not None:
            display.resolve(selected['display'],screens,renderer='dxvk',gpu=selected['choice'])
        with graphics.session_lock(root):
            # Validate all old user records before staging their before-images.
            before=desktop_settings.current_graphics(self,root);player_settings.load(root)
            after=dict(renderer_policy=policy,choice=selected,quality=quality_value)
            if remember_previous and after!=before and desktop_settings.graphics_backup(self,root) is None:
                desktop_settings.remember_graphics(self,root,before,confirmed=False)
            if policy=='dxvk' and any(selected.get(name,False) for name in ('midsummer_tone','enhanced_aa','contact_shadows','maximum_fx')):
                if gold:gold_graphics.prepare_effects(root,selected,cancelled=lambda:self.cancel.exists(),progress=self.progress)
                else:
                    import reshade_runtime
                    reshade_runtime.prepare(root,options=selected,cancelled=lambda:self.cancel.exists(),progress=self.progress)
            if self.cancel.exists():raise Cancelled('cancelled')
            value=self.installed();value['renderer_policy']=policy
            planned={'choice':graphics.encoded(selected),'quality':graphics.encoded(quality_value),
                     'player':json.dumps({'format':player_settings.FORMAT,'display_name':request['player_name']},ensure_ascii=True).encode('ascii'),
                     'installation':graphics.encoded(value)}
            transaction=desktop_settings.Transaction.begin(self,root,choice._renderer(root,{})[0],planned)
            self.operation_state('critical');self._settings_lock_held=True
            try:
                transaction.check()
                self.renderer(root,'dxvk')
                transaction.check()
                choice.save_choice(root,selected,renderer=policy)
                transaction.check()
                self.write(root/quality.FILE,quality_value)
                transaction.check()
                player_settings.save(root,request['player_name'])
                transaction.check()
                self.write(self.state/'installation.json',value)
                owned=self.ownership_record(root)
                transaction.ownership(graphics.encoded(owned))
                transaction.check()
                self.write(root/'desktop-files.json',owned)
                transaction.commit()
            except BaseException:
                transaction.rollback()
                raise
            finally:
                self._settings_lock_held=False;self.operation_state('idle')
        # The generation has committed. A failed UI refresh must not undo it.
        committed=dict(renderer_policy=policy,choice=selected,quality=quality_value,
                       player_name=request['player_name'],renderer=policy)
        try:result=self.snapshot(displays=screens);refresh_failed=False
        except (OSError,ValueError,RuntimeError):result={};refresh_failed=True
        return {**result,'settings_committed':True,'committed_settings':committed,'refresh_failed':refresh_failed}

    def restore_graphics(self,root):
        screens=self.displays()
        backup=desktop_settings.graphics_backup(self,root)
        renderers=self.supported_renderers()
        settings=backup[1] if backup else dict(
            renderer_policy='native' if 'native' in renderers else 'dxvk',
            choice=dict(format=choice.FORMAT,choice=None,simple_menu=True,display=self.recovery_display(screens)),
            quality=dict(quality.DEFAULTS))
        selected=dict(settings['choice'])
        policy=settings['renderer_policy']
        if policy not in renderers:policy='dxvk'
        if policy=='dxvk' and selected['choice'] not in self.cached_graphics_probe()['devices']:
            selected['choice']=None
        try:
            display.resolve(selected.get('display'),screens,renderer=policy,gpu=selected['choice'])
        except display.DisplayUnavailable:
            selected.update(choice=None,display=self.recovery_display(screens))
        return self.save_settings(dict(renderer_policy=policy,choice=selected,quality=settings['quality'],
                                       player_name=player_settings.load(root)),remember_previous=False)

    @contextmanager
    def graphics_startup(self,root):
        startup=desktop_settings.GraphicsStartup(self,root)
        try:
            yield startup
        except (ValueError,RuntimeError) as error:
            if ((startup.observed and not startup.ready and str(error)=='game_failed')
                    or isinstance(error,display.DisplayUnavailable)
                    or str(error)=='renderer_unsupported'):
                backup=desktop_settings.graphics_backup(self,root)
                restored=False
                try:
                    if backup is not None and backup[1]!=desktop_settings.current_graphics(self,root):
                        self.restore_graphics(root)
                        restored=True
                except (OSError,ValueError,RuntimeError):
                    self.emit('warning',code='graphics_restore_failed')
                if restored:raise ValueError('graphics_start_reverted') from error
            raise

    @verified_operation
    def recover_settings(self):
        if not (self.state/desktop_settings.NAME).exists():return
        self.package_manifest();root=self.game();graphics.ensure_closed()
        with graphics.session_lock(root):
            self.operation_state('critical')
            try:desktop_settings.recover(self,root)
            finally:self.operation_state('idle')

    def recovery_status(self):return desktop_settings.recovery_status(self)

    def resolve_recovery(self,request):
        graphics.ensure_closed()
        if request.get('action')=='restore_previous':self.package_manifest()
        # The caller owns the desktop lock. Resolution never imports foreign
        # settings into the managed generation; their full bytes are backed up.
        return desktop_settings.resolve_conflict(self,request)

    def network_id(self,root):
        return content.network_id(content.load(root/'compatibility.json'))

    def alternatives(self,root):
        current=content.load(root/'compatibility.json')
        if (root/graphics.STATE).is_dir():
            _,versions,_=graphics._load_state(root,full_inventory=False)
            return {('dxvk' if enabled else 'native'):content.network_id(json.loads(values[graphics.COMPAT]))
                    for enabled,values in versions.items()}
        before=graphics.encoded(current)
        return {'native':content.network_id(current),'dxvk':content.network_id(json.loads(graphics._expected(before)))}

    def lan_list(self):
        if self.is_gold():raise ValueError('gold_single_player_only')
        root=self.game();interfaces=enumerate_interfaces();directory=LanDiscovery(interfaces,port=self.lan_discovery_port())
        expected=self.network_id(root);alternatives=self.alternatives(root)
        rooms=directory.list_rooms(timeout=1)
        def status(room):
            if room.compatible(expected):return 'ready'
            return 'switch' if room.content_id==alternatives.get('dxvk') else 'different'
        return {'networks':[{'key':i.address,'name':i.name} for i in interfaces],
                'rooms':[{'key':r.room_id,'network':r.local_interface.address,'name':r.name,
                          'members':r.members,'capacity':r.capacity,'status':status(r)} for r in rooms]}

    @staticmethod
    def validate_lan(request):
        if not isinstance(request,dict) or set(request)!={'player_name'}:raise ValueError('request_invalid')
        player_settings.player_name(request['player_name'])

    def align_lan(self,request,*,discovery_port=None):
        self.validate_lan(request)
        from lan_autoconnect import discover_preferred
        self.progress('lan_search')
        if discovery_port is None:discovery_port=self.lan_discovery_port()
        root=self.game();interfaces=enumerate_interfaces();directory=LanDiscovery(interfaces,port=discovery_port)
        # DXVK is required on this PC; discovery cannot select another pipeline.
        self.renderer(root,'dxvk')
        current=self.network_id(root)
        room=discover_preferred(directory,current,
            progress=self.progress,cancel_check=lambda:self.progress('lan_search'))
        if room is not None and current!=room.content_id:raise ValueError('version_mismatch')

    def connect_discord(self,data):
        if not self.package_manifest().get('discord_directory',False):raise ValueError('discord_unavailable')
        if not isinstance(data,dict) or set(data)!={'channel'}:raise ValueError('request_invalid')
        from internet_private_channel import PrivateChannel
        from discord_credentials import parse,store,FORMAT
        from discord_directory import check_connection
        with PrivateChannel(data['channel']) as channel:
            url,key=parse(channel.read())
            check_connection(url,key)
            store(url,key)
            self.write(self.state/'discord-directory.json',{'format':FORMAT,'origin':url,'publish_enabled':False})
        return {'connected':True}

    def check_updates(self):
        if not self.read(self.package/'package.json',{}).get('update_patcher',False):raise ValueError('update_unavailable')
        from update_client import check
        return check(self.app,progress=self.progress,cancelled=lambda:self.cancel.exists())

    def install_update(self,data):
        if not self.read(self.package/'package.json',{}).get('update_patcher',False):raise ValueError('update_unavailable')
        if not isinstance(data,dict) or set(data)!={'sequence'} or type(data['sequence']) is not int or data['sequence']<1:
            raise ValueError('request_invalid')
        if self.cancel.exists():raise Cancelled('cancelled')
        graphics.ensure_closed()
        from update_client import process_info,prepare,start_handoff,remove_owned_job
        parent=process_info(os.getppid())
        if graphics._plain(parent['path'])!=self.app/'S7Launcher.exe':raise ValueError('update_parent_invalid')
        prepared=prepare(self.app,parent['pid'],parent['started_ticks'],progress=self.progress,
                         expected_sequence=data['sequence'],cancelled=lambda:self.cancel.exists())
        # The last download progress notification precedes final verification.
        # A cancellation during that work must never authorize an installer.
        if self.cancel.exists():
            try:remove_owned_job(self.app,prepared['job'])
            except (OSError,ValueError):pass
            raise Cancelled('cancelled')
        # Once the verified helper owns the handoff, interrupting it can leave
        # two installers racing. Retain ownership until its ready/error result.
        self.operation_state('critical')
        try:
            started=start_handoff(self.app,prepared['job'])
            return {'handoff':True,'version':started['version']}
        finally:self.operation_state('idle')

    def disconnect_discord(self):
        if not self.package_manifest().get('discord_directory',False):raise ValueError('discord_unavailable')
        from discord_credentials import load,remove
        value=load(self.state)
        if value:remove(value[0])
        graphics._plain(self.state/'discord-directory.json').unlink(missing_ok=True)
        return {'connected':False}

    @verified_operation
    def internet_play(self,request,*,host):
        if self.is_gold():raise ValueError('gold_single_player_only')
        if not self.package_manifest().get('internet_easytier',False):
            raise ValueError('package_invalid')
        required={'player_name','channel','mode','direct_endpoint','internet_consent'} if host else {'player_name','channel','internet_consent'}
        optional={'publish_discord','public_lobby'} if host else {'publish_discord'}
        if not isinstance(request,dict) or not required<=set(request) or set(request)-required-optional:raise ValueError('request_invalid')
        if type(request['internet_consent']) is not int or request['internet_consent']!=1:raise ValueError('internet_consent_required')
        publish=request.get('publish_discord',False)
        if type(publish) is not bool:raise ValueError('request_invalid')
        public=request.get('public_lobby',publish)
        if type(public) is not bool or (publish and not public) or (public and not host):raise ValueError('request_invalid')
        if public and not publish and not self.package_manifest().get('public_game_directory',False):raise ValueError('directory_unavailable')
        if publish:
            from discord_credentials import connected
            if not self.package_manifest().get('discord_directory',False) or not connected(self.state):
                raise ValueError('discord_not_connected')
        if public and 'publish_discord' in request and self.package_manifest().get('discord_directory',False):
            from discord_credentials import public_settings
            preferences=public_settings(self.state)
            if preferences:
                preferences['publish_enabled']=publish;self.write(self.state/'discord-directory.json',preferences)
        self.validate_lan({'player_name':request['player_name']})
        if host and (request['mode'] not in ('assisted','direct') or
                (request['mode']=='direct')!=(request['direct_endpoint'] is not None)):
            raise ValueError('request_invalid')
        from internet_private_channel import PrivateChannel
        from internet_room import InternetError, parse, endpoint
        with PrivateChannel(request['channel'],cancel_check=lambda:self.progress('internet_connect')) as channel:
            invitation=channel.read()
            if (invitation is None)!=host:raise InternetError('internet_invitation_invalid')
            if not host:parse(invitation)
            elif request['mode']=='direct':endpoint(request['direct_endpoint'])
            options={'invitation':invitation,'mode':request.get('mode','assisted'),
                     'direct_endpoint':request.get('direct_endpoint'),'publish_discord':publish,'public_lobby':public,'internet_consent':1}
            return self.play({'player_name':request['player_name']},internet=options,
                             invitation_ready=channel.invitation_ready)

    def directory_connect(self,data):
        if not self.package_manifest().get('game_directory',False):raise ValueError('directory_unavailable')
        if not isinstance(data,dict) or set(data)!={'channel'}:raise ValueError('request_invalid')
        from internet_private_channel import PrivateChannel
        from game_directory import parse_code, listing, FORMAT
        with PrivateChannel(data['channel']) as channel:url,game=parse_code(channel.read())
        listing(url)
        self.write(self.state/'game-directory.json',{'format':FORMAT,'origin':url,'explicit_override':True})
        return {'connected':True,'game_id':game}

    def directory_list(self):
        if self.installed() and self.is_gold():raise ValueError('gold_single_player_only')
        if not install.package_metadata(self.package).get('game_directory',False):raise ValueError('directory_unavailable')
        from game_directory import configured,listing
        url=configured(self.state)
        if url is None:return {'connected':False,'games':[]}
        root=self.game();current=self.network_id(root)
        # In v3, the complete pinned DXVK profile already shares the native
        # gameplay ID. Browsing needs only this metadata hint, never renderer
        # payload/journal scans or admission of a legacy/changed alternative.
        rows=listing(url)
        for row in rows:row['compatible']=row['content_id']==current
        return {'connected':True,'games':rows}

    @verified_operation
    def directory_join(self,data):
        if self.is_gold():raise ValueError('gold_single_player_only')
        if not self.package_manifest().get('game_directory',False):raise ValueError('directory_unavailable')
        if not isinstance(data,dict) or set(data)!={'game_id','player_name','internet_consent'}:raise ValueError('request_invalid')
        if type(data['internet_consent']) is not int or data['internet_consent']!=1:raise ValueError('internet_consent_required')
        self.validate_lan({'player_name':data['player_name']});graphics.ensure_closed()
        if self.installed()['package_id']!=self.package_id():raise ValueError('update_required')
        from game_directory import configured,join
        url=configured(self.state)
        if url is None:raise ValueError('directory_not_connected')
        root=self.game();allowed={self.network_id(root),*self.alternatives(root).values()}
        invitation=join(url,data['game_id'],allowed,root=self.package,
            cancel=lambda:self.progress('directory_join'),progress=self.progress)
        return self.play({'player_name':data['player_name']},internet={'invitation':invitation,
            'mode':'assisted','direct_endpoint':None,'publish_discord':False,'internet_consent':1})

    def align_internet(self,root,interface,coordinator,host):
        if host:return
        from lan_control import request
        from internet_room import InternetError
        deadline=time.monotonic()+45
        while True:
            self.progress('internet_connect')
            try:
                # Alignment precedes LAN admission. The bootstrap status is
                # public; the full configuration correctly requires membership.
                status=request(coordinator,interface.address,'status')
                break
            except (OSError,ValueError,http.client.HTTPException):
                if time.monotonic()>=deadline:raise InternetError('internet_coordinator_unreachable') from None
                time.sleep(.2)
        actual=status.get('content_id')
        if actual!=self.network_id(root):raise ValueError('version_mismatch')

    def check_start_memory(self):
        import memory_guard
        sample=memory_guard.require_game_headroom()
        if sample['host_commit_free']<4096*memory_guard.MIB:
            self.emit('warning',code='memory_commit_warning')
        elif sample['host_physical_free']<=1536*memory_guard.MIB:
            self.emit('warning',code='memory_physical_warning')

    @verified_operation
    def crossplay_play(self,request,*,host):
        manifest=self.package_manifest()
        if manifest.get('crossplay_mixed',False) is not True:raise ValueError('crossplay_unavailable')
        required=({'player_name','channel','history_edition','mode','direct_endpoint','internet_consent'}
                  if host else {'player_name','channel','internet_consent'})
        if type(host) is not bool or not isinstance(request,dict) or set(request)!=required:
            raise ValueError('request_invalid')
        if type(request['internet_consent']) is not int or request['internet_consent']!=1:
            raise ValueError('internet_consent_required')
        name=request['player_name']
        if (not isinstance(name,str) or not 1<=len(name)<=64 or any(not 32<=ord(c)<=126 for c in name)):
            raise ValueError('crossplay_configuration_invalid')
        if host and (type(request['history_edition']) is not int or request['history_edition'] not in (2,3)
                or request['mode'] not in ('assisted','direct')
                or (request['mode']=='direct')!=(request['direct_endpoint'] is not None)):
            raise ValueError('request_invalid')
        if self.installed()['package_id']!=self.package_id():raise ValueError('update_required')
        root=self.game();self.progress('verify');self.verify_instance(root)
        import platform_standalone,crossplay_content,crossplay_session
        _,executable,_=platform_standalone.load(root)
        edition=crossplay_session.EXECUTABLE_EDITIONS.get(content.digest(executable))
        if (host and edition!=1) or (not host and edition not in (2,3)):
            raise ValueError('crossplay_edition_mismatch')
        catalog=crossplay_content.package_catalog(self.package,manifest)
        catalog.verify(root,edition)
        graphics.ensure_closed();self.check_start_memory()
        from internet_private_channel import PrivateChannel
        from internet_room import create,check_overlap,endpoint
        from crossplay_invitation import parse
        from crossplay_network import NetworkSession
        with PrivateChannel(request['channel'],cancel_check=lambda:self.progress('internet_connect')) as channel:
            invitation=channel.read()
            if (invitation is None)!=host:raise ValueError('crossplay_invitation_invalid')
            if host:
                if request['mode']=='direct':endpoint(request['direct_endpoint'])
                room=create(enumerate_interfaces(allow_empty=True, include_all=True),mode=request['mode'],direct_endpoint=request['direct_endpoint'])
            else:
                invite=parse(invitation)
                if invite.edition!=edition:raise ValueError('crossplay_edition_mismatch')
                check_overlap(invite.room,enumerate_interfaces(allow_empty=True, include_all=True))
            options=dict(name=name,consent=1,cancel_check=lambda:self.progress('internet_connect'),progress=self.progress)
            with self.graphics_startup(root) as startup,graphics.session_lock(root):
                owner=(NetworkSession.host(root,catalog,room=room,history=request['history_edition'],**options)
                       if host else NetworkSession.guest(root,catalog,invitation=invitation,**options))
                with owner:
                    return self.crossplay_game(root,catalog,owner,startup=startup,
                                               invitation_ready=channel.invitation_ready if host else None)

    def crossplay_game(self,root,catalog,owner,*,invitation_ready=None,startup=None):
        import platform_standalone,memory_guard
        environment={k:v for k,v in os.environ.items() if not k.upper().startswith(('DXVK_','S7_','WEPLAY_'))}
        environment.update(DXVK_LOG_PATH=str(root/'LocalData'),DXVK_STATE_CACHE_PATH=str(root/'LocalData'))
        renderer=self.launch_renderer()
        policy=renderer[0] if renderer else 'dxvk'
        environment=choice.saved_launch_environment(root,environment,renderer=policy,
            renderer_package=renderer[1] if renderer else None)
        graphics_options=None
        if owner.binding.edition==1 and renderer is None:
            import gold_graphics
            graphics_options=gold_graphics.launch_options(root)
        game=observer=window=None;reported=False
        try:
            owner.check();self.progress('starting')
            game=platform_standalone.launch(root,environment=environment,arguments=choice.game_arguments(root,renderer=policy),
                graphics_options=graphics_options,crossplay=owner.binding,crossplay_catalog=catalog,
                on_initialized=owner.attach,renderer=renderer)
            self.emit('game_started',pid=game.pid,invitation_run=None);reported=True;self.operation_state('live_game')
            window=launch_local.try_window_mode(root,game)
            _,executable,_=platform_standalone.load(root)
            observer=memory_guard.Sentinel(game,root/'LocalData/desktop-memory-summary.json',
                memory_guard.Probe(game,executable),
                warn=lambda level:self.emit('warning',code='memory_warning',level=level))
            observer.start()
            if invitation_ready:invitation_ready(owner.invitation())
            if startup is not None:startup.observe(game)
            code=game.wait(close=False);owner.runtime.check()
        except BaseException:
            # If a post-start UI/pipe observer fails, close only the room. Keep
            # the parent alive for the original game/disconnect and user exit.
            try:owner.close()
            finally:
                if game is not None and game.poll() is None:
                    if not reported:
                        self.emit('game_started',pid=game.pid,invitation_run=None);reported=True
                    self.emit('warning',code='crossplay_service_failed')
                    self.operation_state('live_game');game.wait(close=False)
            raise
        finally:
            if window:window.close()
            if observer:observer.close()
            if game:
                finished=game.poll();game.close()
                if reported and finished is not None:self.emit('game_finished',exit_code=finished)
            self.operation_state('cancellable')
        if code:raise RuntimeError('game_failed')
        return {'exit_code':code}

    def play_gold(self,request=None,*,internet=None,startup=None):
        if request is not None or internet is not None:raise ValueError('gold_single_player_only')
        self.package_manifest()
        if self.installed()['package_id']!=self.package_id():raise ValueError('update_required')
        root=self.game();self.progress('verify');self.verify_instance(root)
        graphics.ensure_closed();self.check_start_memory();self.progress('starting')
        import gold_support,platform_standalone,memory_guard
        environment={k:v for k,v in os.environ.items() if not k.upper().startswith(('DXVK_','S7_','WEPLAY_'))}
        environment.update(DXVK_LOG_PATH=str(root/'LocalData'),DXVK_STATE_CACHE_PATH=str(root/'LocalData'))
        import gold_graphics
        renderer=self.launch_renderer()
        policy=renderer[0] if renderer else 'dxvk'
        with graphics.session_lock(root), ExitStack() as cleanup:
            cleanup.callback(self.operation_state,'cancellable')
            preferred=choice._load_choice(root)
            selected=self.default_choice(root,'dxvk',self.cached_graphics_probe(),preferred)
            if renderer is not None:
                selected['choice']=preferred['choice']
            if selected!=preferred:
                choice.save_choice(root,selected,renderer=policy)
            environment=choice.saved_launch_environment(root,environment,renderer=policy,
                renderer_package=renderer[1] if renderer else None)
            arguments=choice.game_arguments(root,renderer=policy)
            game=platform_standalone.launch(root,environment=environment,arguments=arguments,
                graphics_options=gold_graphics.launch_options(root) if renderer is None else None,renderer=renderer)
            cleanup.callback(game.close)
            window=launch_local.try_window_mode(root,game)
            if window:
                cleanup.callback(window.close)
            observer=memory_guard.Sentinel(game,root/'LocalData/desktop-memory-summary.json',
                memory_guard.Probe(game,root/gold_support.EXE),
                warn=lambda level:self.emit('warning',code='memory_warning',level=level))
            cleanup.callback(observer.close)
            observer.start();self.emit('game_started',pid=game.pid,invitation_run=None);self.operation_state('live_game')
            if startup is not None:startup.observe(game)
            code=game.wait(close=False)
        self.emit('game_finished',exit_code=code)
        if code:raise RuntimeError('game_failed')
        return {'exit_code':code}

    @verified_operation
    def play(self,request=None,*,internet=None,invitation_ready=None):
        if request is not None:self.validate_lan(request)
        root=self.game()
        with self.graphics_startup(root) as startup:
            if self.is_gold():return self.play_gold(request,internet=internet,startup=startup)
            return self.play_history(root,request,internet=internet,invitation_ready=invitation_ready,startup=startup)

    def play_history(self,root,request,*,internet,invitation_ready,startup):
        directory_bridge=[None]
        invitation_bridge=[None]
        self.progress('verify')
        self.package_manifest()
        if self.installed()['package_id']!=self.package_id():raise ValueError('update_required')
        # The binary/ABI checks and full game manifest checks remain in place.
        if request is None:content.verify(root)
        graphics.ensure_closed()
        import memory_guard
        def before():
            graphics.ensure_closed();self.check_start_memory();self.progress('starting')
        def monitor(game):
            observer=memory_guard.Sentinel(game,root/'LocalData/desktop-memory-summary.json',
                memory_guard.Probe(game,root/'Base/Bin/Settlers7R.exe'),
                warn=lambda level:self.emit('warning',code='memory_warning',level=level))
            try:observer.start()
            except BaseException:
                if game.poll() is None:game.terminate();game.wait(timeout=10)
                raise
            invitation_run=None
            if self.package_manifest().get('game_directory',False):
                from ingame_invitation import Bridge as InvitationBridge,configuration
                try:
                    mode,public_code=configuration(request,internet,self.state)
                    invitation_bridge[0]=InvitationBridge(root,mode,
                        lambda **value:self.emit('invitation_requested',**value),public_code=public_code).start()
                    invitation_run=invitation_bridge[0].nonce
                except (OSError,ValueError):self.emit('warning',code='invitation_unavailable')
            self.emit('game_started',pid=game.pid,invitation_run=invitation_run)
            self.operation_state('live_game')
            if self.package_manifest().get('game_directory',False):
                from game_directory import configured
                from ingame_directory import Bridge
                try:
                    url=configured(self.state)
                    if url:
                        allowed={self.network_id(root),*self.alternatives(root).values()}
                        directory_bridge[0]=Bridge(root,url,allowed).start()
                except (OSError,ValueError):self.emit('warning',code='directory_unavailable')
            desktop=self
            class GameObserver:
                def close(self):
                    try:observer.close()
                    finally:
                        try:
                            if invitation_bridge[0] is not None:invitation_bridge[0].close()
                        finally:
                            try:
                                if directory_bridge[0] is not None:directory_bridge[0].close()
                            finally:
                                if internet is not None:desktop.emit('game_finished',exit_code=game.poll())
            return GameObserver()
        # Repair saved monitor selections after a real hotplug, before launch.
        probe=self.cached_graphics_probe()
        if request is None or internet is not None:mode=self.renderer(root,'dxvk')
        else:
            discovery_port=self.lan_discovery_port()
            self.align_lan(request,discovery_port=discovery_port);mode=choice._renderer(root,{})[0]
        renderer=self.launch_renderer()
        with graphics.session_lock(root):
            preferred=choice._load_choice(root)
            selected=self.default_choice(root,mode,probe,preferred)
            if renderer is not None:selected['choice']=preferred['choice']
            choice.save_choice(root,selected,renderer=renderer[0] if renderer else 'dxvk')
        if request is None:
            args=SimpleNamespace(bundle=str(root),dotnet=str(root/'Runtime/dotnet/dotnet.exe'),
                server=str(root/'Server/S7Patcher.dll'),local_connect_probe=True,local_session_display_probe=False,
                launch_game=True,saved_graphics=True,before_game=before,game_monitor=monitor,
                game_startup=startup.observe,renderer=renderer)
            code=launch_local.run(args)
        elif internet is not None:
            import launch_internet
            def room_ready(invitation):
                if invitation_ready:invitation_ready(invitation)
            def observe(interface,coordinator,expected,*,room_invitation):
                public=internet.get('public_lobby',internet.get('publish_discord',False))
                if not public:return None
                if internet.get('publish_discord',False):from discord_directory import Reporter
                else:from public_directory import PublicReporter as Reporter
                reporter=None
                try:
                    extra={}
                    if self.package_manifest().get('game_directory',False):
                        if not room_invitation:raise ValueError('directory_unavailable')
                        extra=dict(room_invitation=room_invitation,version=self.read(self.app/'desktop-release.json',{}).get('version'),helper_root=self.package)
                    reporter=Reporter(self.state,interface,coordinator,expected,
                        warn=lambda code:self.emit('warning',code=code),**extra)
                    # Confirm hosting access before starting S7. A failed
                    # registration must not silently open an unlisted game.
                    reporter.poll()
                    return reporter.start()
                except (OSError,ValueError,http.client.HTTPException):
                    if reporter:reporter.close()
                    raise ValueError('discord_not_connected' if internet.get('publish_discord',False) else 'directory_unavailable') from None
            code=launch_internet.run(SimpleNamespace(bundle=root,check_only=False,saved_graphics=True,renderer=renderer,
                player_name=request['player_name'],progress=self.progress,
                cancel_check=lambda:self.progress('internet_connect'),before_game=before,game_monitor=monitor,
                game_startup=startup.observe,
                switch_requested=lambda:directory_bridge[0] is not None and directory_bridge[0].pending is not None,
                public_observer=observe,**internet),
                invitation_ready=room_ready,
                policy_ready=lambda mode:self.emit('internet_policy',mode=mode),
                network_ready=lambda interface,coordinator,host:self.align_internet(root,interface,coordinator,host))
        else:
            code=launch_auto_lan.run(SimpleNamespace(bundle=root,check_only=False,saved_graphics=True,renderer=renderer,
                player_name=request['player_name'],discovery_port=discovery_port,progress=self.progress,
                cancel_check=lambda:self.progress('lan_search'),before_game=before,game_monitor=monitor,
                game_startup=startup.observe))
        self.emit('game_finished',exit_code=code)
        self.operation_state('cancellable')
        if code:raise RuntimeError('game_failed')
        if directory_bridge[0] is not None and directory_bridge[0].pending:
            # The previous transport is already closed. A fresh UI consent is
            # required before connecting to the newly selected host's room.
            self.emit('directory_switch',game_id=directory_bridge[0].pending)
            return {'exit_code':code,'directory_game_id':directory_bridge[0].pending}
        return {'exit_code':code}

    def verify(self):
        self.progress('verify');self.verify_instance(self.game());return {'verified':True}

    def configure_firewall(self):
        if self.is_gold():
            import platform_standalone
            self.verify_instance(self.game());platform_standalone.configure(self.game())
            self.remember_network_setup(True)
            return {'configured':True}
        import windows_system
        self.package_manifest()
        root=self.game();content.verify(root)
        try:windows_system.lan_firewall(root)
        except (OSError,ValueError,subprocess.TimeoutExpired):raise ValueError('permissionFailed')
        self.remember_network_setup(True)
        return {'configured':True}

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
        import windows_system
        try:paths=windows_system.process_paths()
        except (OSError,ValueError,subprocess.TimeoutExpired):raise ValueError('managed_copy_busy')
        if any(p and Path(p).is_relative_to(root) for p in paths):raise ValueError('managed_copy_busy')
        marker=self.read(root/'LocalData/desktop-firewall.json')
        if marker is not None:
            key=hashlib.sha256(str(root).lower().encode('utf-8')).hexdigest()[:16].upper()
            if marker!={'format':'s7-private-firewall-v1','key':key}:raise ValueError('ownership_invalid')
            # Use the current app's helper, including cleanup of legacy copies.
            # Never execute an old script from a user-writable previous game copy.
            try:windows_system.lan_firewall(root,remove=True)
            except (OSError,ValueError,subprocess.TimeoutExpired):raise ValueError('firewall_remove_failed')
        kept=0
        owned_folders=set()
        for index,(path,digest) in enumerate(validated):
            self.progress('cleanup',index,len(validated))
            if path.is_file():
                if content.digest(path)==digest:path.unlink()
                else:kept+=1
            owned_folders.update(parent for parent in path.parents if parent!=root and parent.is_relative_to(root))
        # Prune only directories covered by the owned inventory. Empty
        # personal folders outside it are just as much user data as files.
        for directory in sorted(owned_folders,key=lambda p:len(p.parts),reverse=True):
            try:graphics._plain(directory).rmdir()
            except OSError:pass
        return {'kept_modified':kept}

    def retire_old_copies(self):
        value=self.installed()
        if not value or not value['previous']:return {'cleaned':0}
        try:
            graphics.ensure_closed()
            self.package_manifest()
            # A GUI-only update can reuse its active game. Verify that copy
            # before reclaiming old versions; do not allocate another copy.
            if value['package_id']!=self.package_id():return {'cleaned':0}
            self.verify_instance(self.game())
            self.finalizing=True
            return self.clean_old(automatic=True)
        except (OSError,ValueError,subprocess.TimeoutExpired):
            self.emit('warning',code='cleanup_deferred')
            return {'cleaned':0}

    def clean_old(self,automatic=False):
        graphics.ensure_closed();self.recover_publication();self.cleanup_preparations();value=self.installed()
        if not value:return {'cleaned':0}
        count=0;pending=[]
        active=self.instance(value['container'],value['current'])
        for index,reference in enumerate(value['previous']):
            try:
                root=self.instance(reference['container'],reference['instance'])
                if active.is_relative_to(root):raise ValueError('ownership_invalid')
                self.progress('cleanup');self.cleanup_instance(reference['container'],reference['instance']);count+=1
            except (OSError,ValueError,subprocess.TimeoutExpired):
                pending.append(reference)
                if not automatic:
                    value['previous']=[*pending,*value['previous'][index+1:]]
                    self.write(self.state/'installation.json',value)
                    raise
        value['previous']=pending;self.write(self.state/'installation.json',value)
        if pending:self.emit('warning',code='cleanup_deferred')
        return {'cleaned':count}

    def uninstall(self):
        graphics.ensure_closed();value=self.installed()
        if value:
            references=[{'container':value['container'],'instance':value['current']},*value['previous']]
            for reference in references:self.cleanup_instance(reference['container'],reference['instance'])
            self.write(self.state/'retained-profile.json',{'container':value['container'],'instance':value['current']})
            graphics._plain(self.state/'installation.json').unlink()
        return {'personal_data_kept':True}

    def diagnostics(self):
        collection={}
        def section(name, read, fallback=None):
            try:
                value=read()
                collection[name]='ok' if value is not None else 'not_available'
                return value if value is not None else fallback
            except (OSError,ValueError,RuntimeError,TypeError,KeyError):
                collection[name]='unreadable'
                return fallback
        installed=section('installation',self.installed)
        root=section('game_copy',lambda:self.game() if installed else None)
        mode=installed['renderer_policy'] if installed else None
        def read_probe():
            value=self.read(self.state/'graphics-probe.json',limit=65536)
            return self.validate_probe(value) if value is not None else None
        probe=section('graphics_probe',read_probe,{'devices':[],'failure':None})
        def diagnostic_display():
            if root is None:
                return None
            def size(selected):
                value = display.validate(selected.get('display')) if selected is not None else None
                return {key:value[key] for key in ('mode','width','height')} if value is not None else None
            backup = desktop_settings.graphics_backup(self,root)
            return {
                'selected': size(choice._load_choice(root)),
                'last_confirmed': size(backup[1]['choice']) if backup and backup[0] else None,
            }
        display_summary=section('display_preferences',diagnostic_display)
        raw=section('memory',lambda:self.read(root/'LocalData/desktop-memory-summary.json',limit=4096)
                    if root else None,{})
        if not isinstance(raw,dict):
            collection['memory']='unreadable'
            raw={}
        fields=('samples','max_pressure','peak_private_mib','min_free_mib','min_largest_mib','monitor_failed',
                'recovery_required','min_host_physical_free_mib','min_host_commit_free_mib')
        summary={key:raw[key] for key in fields if key in raw and
                 (raw[key] is None or type(raw[key]) in (bool,int) and 0<=raw[key]<=2**63-1)}
        release=section('release',lambda:self.read(self.app/'desktop-release.json',limit=65536),{})
        if not isinstance(release,dict):
            collection['release']='unreadable'
            release={}
        version=release.get('version','unknown')
        if not isinstance(version,str) or not re.fullmatch('[0-9A-Za-z.+-]{1,64}',version):version='unknown'
        source=release.get('source_commit')
        if not isinstance(source,str) or not re.fullmatch('[0-9a-f]{40}',source):source=None
        revision=release.get('network_revision')
        if type(revision) is not int or not 0<=revision<=100000:revision=None
        neo=release.get('neo_wire_revision')
        if type(neo) is not int or not 0<=neo<=100000:neo=None
        from game_directory import NETWORK_REVISION
        try:
            last_lan=self.read(self.state/'lan-start.json',{},limit=1024)
            last_lan=last_lan.get('code') if isinstance(last_lan,dict) else None
            if not isinstance(last_lan,str) or not re.fullmatch('[a-z_]{1,64}',last_lan):last_lan=None
        except (OSError,ValueError):last_lan=None
        report={'format':'s7-desktop-diagnostics-v1','release':version,'source_commit':source,
                'release_metadata_network_revision':revision,'content_contract':content.NETWORK_FORMAT,
                'directory_protocol_revision':NETWORK_REVISION,'neo_wire_revision':neo,
                'game_bits':32,'renderer':mode,
                'graphics_devices':probe['devices'],'probe_status':probe['failure'],
                'display_preferences':display_summary,
                'last_lan_start':last_lan,'lan_discovery_port':self.lan_discovery_port(),
                'memory_summary':summary,
                'collection_status':collection,
                'known_lan_desync_resolved':False}
        if (self.state/desktop_settings.NAME).exists() or (self.state/desktop_settings.DETACH).exists():
            status=self.recovery_status()
            report['recovery']={name:status[name] for name in ('pending','recoverable','can_detach','conflicts')}
        return report

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

    def migrate_record(self):
        """Publish a validated legacy record; preserve the original for recovery."""
        graphics.ensure_closed()
        path=self.state/'installation.json';before=graphics._read(path,4*1024*1024)
        value=self.installed()
        if value is None:raise ValueError('not_installed')
        if self.read(path)['format']!=INSTALL_FORMAT:
            backup=self.state/('installation-v1-backup-'+uuid.uuid4().hex+'.json')
            graphics.atomic_write(backup,before,self.state)
            if graphics._read(path,4*1024*1024)!=before:raise ValueError('operation_busy')
            self.write(path,value)
        return self.snapshot()


def dispatch(desktop,request):
    # These read-only/UI requests must not create a game verification scope or
    # enter publication/settings recovery. Their callers need no game object.
    if (isinstance(request,dict) and set(request)=={'format','action','data'}
            and request['format']==REQUEST and request['data']=={}):
        if request['action']=='update-check':return desktop.check_updates()
        if request['action']=='directory-list':return desktop.directory_list()
        if request['action']=='recovery-status':return desktop.recovery_status()
        if request['action']=='diagnostics':return desktop.diagnostics()
    with desktop.verification():
        desktop.operation_state('cancellable')
        try:return _dispatch(desktop,request)
        finally:desktop.operation_state('idle')


def _dispatch(desktop,request):
    if not isinstance(request,dict) or set(request)!={'format','action','data'} or request['format']!=REQUEST:
        raise ValueError('request_invalid')
    action,data=request['action'],request['data']
    if not isinstance(data,dict):raise ValueError('request_invalid')
    # The signed-release network check never needs a game-operation lock.
    if action=='update-check' and not data:return desktop.check_updates()
    # This is an untrusted display hint only. Admission still recovers and
    # fully verifies under the mutation lock. No journal writes in this path.
    if action=='directory-list' and not data:return desktop.directory_list()
    if action=='recovery-resolve':
        with desktop.lock():return desktop.resolve_recovery(data)
    if (desktop.state/desktop_settings.DETACH).exists():raise ValueError('preferences_invalid')
    if (desktop.state/'installation-transaction.json').exists():
        desktop.operation_state('critical')
        with desktop.lock():desktop.recover_publication()
        desktop.operation_state('cancellable')
    if (desktop.state/desktop_settings.NAME).exists():
        with desktop.lock():desktop.recover_settings()
    if action=='snapshot' and not data:
        # Opening S7Launcher.exe directly must adopt a portable export just as
        # its CMD starter does. Never send an unbound export to source setup.
        root=desktop.app.parent
        if (root/'standalone.json').exists() or (root/'EXPORT-INCOMPLETE.txt').exists():
            import standalone
            try:
                if (root/'EXPORT-INCOMPLETE.txt').exists():raise ValueError('incomplete export')
                desktop.operation_state('critical')
                standalone.bind(root)
                desktop.operation_state('cancellable')
            except (OSError,ValueError,RuntimeError) as error:
                raise ValueError('standalone_invalid') from error
        previous=desktop.installed()
        if previous and (desktop.game()/graphics.RENDERER_JOURNAL).exists():
            with desktop.lock():desktop.recover_launch_renderer()
        return desktop.snapshot()
    if action=='detect' and not data:
        desktop.progress('source')
        previous=desktop.installed()
        supported=install.package_manifest(desktop.package).get('steam_history',False)
        sources=[]
        if previous:
            try:
                if desktop.is_gold():
                    import gold_support
                    gold_support.inspect(desktop.package,desktop.game(),managed=True)
                else:install.repair_source(desktop.game(),steam_supported=supported,tools=desktop.package/'Tools')
                sources.append({'path':str(desktop.game()),'name':'WePlay'})
            except (OSError,ValueError,KeyError):pass
        if sources:return {'sources':sources}
        locations=([Path(previous['source'])] if previous and previous['source'] else [])+game_detection.candidates()
        return {'sources':sources+game_detection.detect(locations,steam_supported=supported,
            gold_package=desktop.package if desktop.package_manifest().get('gold_standalone',False) else None)}
    with desktop.lock():
        desktop.recover_launch_renderer()
        if action=='update-install':return desktop.install_update(data)
        if action=='firewall' and not data:return desktop.configure_firewall()
        if action in ('install','repair') and set(data)<= {'source','container','prepare_network'}:
            previous=desktop.installed()
            source=data.get('source') or (str(desktop.game()) if previous else None)
            if not isinstance(source,str) or not source:raise ValueError('source_missing')
            container=data.get('container')
            if container is not None and not isinstance(container,str):raise ValueError('request_invalid')
            if 'prepare_network' in data:
                if type(data['prepare_network']) is not bool:raise ValueError('request_invalid')
                return desktop.install_game(source,container,prepare_network=data['prepare_network'])
            return desktop.install_game(source,container)
        if action=='save':return desktop.save_settings(data)
        if action=='restore-graphics' and not data:return desktop.restore_graphics(desktop.game())
        if action=='play' and not data:return desktop.play()
        if action=='lan-list' and not data:return desktop.lan_list()
        if action=='lan-port':return desktop.save_lan_discovery_port(data)
        if action=='lan-play':
            try:result=desktop.play(data)
            except (OSError,ValueError,RuntimeError,http.client.HTTPException,subprocess.TimeoutExpired) as error:
                desktop.remember_lan_start(error_code(error))
                raise
            desktop.remember_lan_start('ok')
            return result
        if action in ('internet-create','internet-join'):return desktop.internet_play(data,host=action=='internet-create')
        if action in ('crossplay-create','crossplay-join'):return desktop.crossplay_play(data,host=action=='crossplay-create')
        if action=='discord-connect':return desktop.connect_discord(data)
        if action=='discord-disconnect' and not data:return desktop.disconnect_discord()
        if action=='directory-connect':return desktop.directory_connect(data)
        if action=='directory-join':return desktop.directory_join(data)
        if action=='probe' and not data:return desktop.probe(refresh=True)
        if action=='verify' and not data:return desktop.verify()
        if action=='cleanup' and not data:return desktop.clean_old()
        if action=='cleanup-automatic' and not data:return desktop.retire_old_copies()
        if action=='uninstall' and not data:return desktop.uninstall()
        if action=='diagnostics' and not data:return desktop.diagnostics()
        if action=='reset-installation' and not data:return desktop.reset_installation()
        if action=='migrate-record' and not data:return desktop.migrate_record()
        raise ValueError('request_invalid')


def error_code(error):
    if isinstance(error,content.InventoryMismatch):
        return 'game_copy_changed'
    if isinstance(error,NativeStartupError) and error.native_start_stage==11:
        return 'network_isolation_failed'
    code=getattr(error,'code','')
    if isinstance(code,str) and re.fullmatch(r'(directory|update|discord|internet)_[a-z_]{1,48}',code):
        return code
    if getattr(error,'code',None) in ('lan_unreachable','lan_version','lan_multiple','lan_full','lan_changing',
                                    'memory_commit_low','memory_status_failed','source_steam_unsupported',
                                    'source_gold_adapter_unavailable'):
        return error.code
    text=str(error)
    known={'request_invalid','not_installed','installation_invalid','package_invalid','operation_busy','cleanup_required',
        'renderer_unsupported','renderer_payload_invalid','destination_invalid','destination_too_long','preferences_invalid','recovery_changed','quality_invalid','quality_ini_invalid','standalone_invalid',
        'reshade_download_timeout','reshade_download_failed','reshade_download_invalid','reshade_preparation_changed',
        'quality_ini_changed','lobby_gone','version_mismatch','network_changed','ownership_invalid',
        'managed_copy_busy','source_missing','game_failed','cancelled','probe_invalid','update_required','firewall_remove_failed','internet_consent_required',
                'lan_discovery_port_busy','lan_discovery_port_invalid','lan_discovery_socket_failed','lan_coordinator_start_failed','lan_coordinator_timeout','lan_discovery_timeout',
                'settings_saved_refresh_failed','graphics_start_reverted','platform_isolation_required','gold_single_player_only','gold_game_settings',
                'gold_graphics_invalid','gold_maximum_unavailable','gold_dxvk_required',
                'crossplay_unavailable','crossplay_configuration_invalid','crossplay_edition_mismatch',
                'crossplay_invitation_invalid','crossplay_content_changed','crossplay_service_failed'}
    known.update(('discord_unavailable','discord_not_connected','update_unavailable','update_parent_invalid','permissionFailed'))
    known.update(('directory_unavailable','directory_not_connected','directory_invalid','directory_join_failed','directory_join_unavailable','directory_code_invalid'))
    if text in known:return text
    if '4 GiB' in text:return 'memory_low'
    if 'laeuft' in text or 'running' in text or 'schliessen' in text:return 'game_running'
    if 'Speicher' in text and 'genug' in text:return 'disk_low'
    if 'Spielversion' in text:return 'source_invalid'
    if 'LAN' in text or 'Netzwerk' in text or 'Lobby' in text:return 'lan_failed'
    return 'operation_failed'


def diagnostic_failure(error):
    """Keep technical categories and source locations out of free-form logs."""
    tool_root=Path(__file__).resolve().parents[1]
    result={'kind':'unexpected','component':None,'line':None,'system_code':None}
    seen=set()
    for _ in range(4):
        if error is None or id(error) in seen:break
        seen.add(id(error))
        if isinstance(error,Cancelled):kind='cancelled'
        elif isinstance(error,(TimeoutError,subprocess.TimeoutExpired)):kind='timeout'
        elif isinstance(error,http.client.HTTPException):kind='http_error'
        elif isinstance(error,OSError):kind='os_error'
        elif isinstance(error,content.InventoryMismatch):
            kind='inventory_mismatch'
            result['inventory']=error.inventory
        elif isinstance(error,(ValueError,KeyError,TypeError)):kind='invalid_input'
        elif isinstance(error,RuntimeError):kind='runtime_error'
        else:kind='unexpected'
        result['kind']=kind
        number=getattr(error,'winerror',None)
        if number is None:number=getattr(error,'errno',None)
        if type(number) is int and -(2**31)<=number<2**31:result['system_code']=number
        if isinstance(error,NativeStartupError):
            result.update(native_start_stage=error.native_start_stage,native_exit_code=error.native_exit_code)
            if error.network_probe_failure is not None:
                probe,step,number=error.network_probe_failure
                result.update(network_probe=probe,network_probe_step=step,winsock_code=number)
        if isinstance(error,LanHubError) and error.winsock_code is not None:
            result['winsock_code']=error.winsock_code
        frame=error.__traceback__
        while frame:
            path=Path(frame.tb_frame.f_code.co_filename).absolute()
            if path.suffix=='.py' and path.is_relative_to(tool_root):
                result.update(component=path.stem,line=frame.tb_lineno)
            frame=frame.tb_next
        error=error.__cause__ or error.__context__
    return result


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--app',required=True);parser.add_argument('--operation',required=True)
    args=parser.parse_args()
    output=sys.stdout;lock=threading.Lock()
    def emit(kind,**fields):
        with lock:
            output.write(json.dumps({'kind':kind,**fields},ensure_ascii=True)+'\n');output.flush()
    desktop=None;request=None
    try:
        app=graphics._plain(args.app)
        if Path(__file__).resolve()!=app/'Package/Tools/installer/desktop_backend.py':raise ValueError('package_invalid')
        desktop=Desktop(app,emit,args.operation)
        request=json_record(sys.stdin.buffer.read(32769),32768)
        with redirect_stdout(Console(emit)):
            result=dispatch(desktop,request)
        emit('result',data=result);return 0
    except Exception as error:
        private=isinstance(request,dict) and request.get('action') in ('internet-create','internet-join','crossplay-create','crossplay-join','discord-connect')
        emit('error',code=error_code(error),detail='' if private else str(error)[:512],
             diagnostic=diagnostic_failure(error));return 2
    finally:
        if desktop is not None and desktop.cancel.exists():desktop.cancel.unlink()


if __name__=='__main__':raise SystemExit(main())
