"""Opt-in local On12 runtime; never installs or replaces Windows components."""
import hashlib
import json
from pathlib import Path
import re

from . import recover,on12_runtime_build as build

ROOT=Path(__file__).resolve().parents[2]
PAYLOAD=('d3d9on12.dll','dxbcSigner.dll')
JOURNAL='on12-runtime.private.json'
SNAPSHOT='on12-runtime-build.private.json'
SIGNER='5f291153682f021e88bffcdb350949153188bac22b7342041a489c86455aeb14'
RUNTIME='da332f721a59dc73f8fdb32acd6755dd0ddae726f78342d8d73c32b14abb21e5'

def pin(value):
    return isinstance(value,str) and re.fullmatch('[0-9a-f]{64}',value) is not None

def shape(value,*,current=True):
    keys={'format','variant','configuration','revisions','source_archives','packages','builder_sha256','project_inputs','inputs','files'}
    if (not isinstance(value,dict) or set(value)!=keys or value['format']!='s7-on12-runtime-build-v1'
            or value['variant']!='stock' or value['configuration']!=build.CONFIG or value['revisions']!=build.REVISIONS
            or value['source_archives']!=build.SOURCE_PINS or value['packages']!={name:p for name,(_,p) in build.PACKAGES.items()}
            or not pin(value['builder_sha256']) or not isinstance(value['project_inputs'],dict)
            or set(value['project_inputs'])!=set(build.PROJECT_INPUTS) or not all(pin(x) for x in value['project_inputs'].values())
            or not isinstance(value['files'],dict) or set(value['files'])!=set(build.FILES)
            or not all(pin(x) for x in value['files'].values()) or value['files']['dxbcSigner.dll']!=SIGNER
            or not isinstance(value['inputs'],dict) or not 1<=len(value['inputs'])<=15000):
        raise ValueError('Unknown local On12 build manifest.')
    for name,p in value['inputs'].items():
        parts=build.relative(name).parts
        if parts[0] not in ('sources','dependencies','project') or not pin(p):raise ValueError('Invalid On12 build input.')
    if current and (value['builder_sha256']!=build.digest(Path(build.__file__))
            or any(build.digest(ROOT/name)!=p for name,p in value['project_inputs'].items())):
        raise ValueError('The local On12 builder or GPU fixture changed; rebuild.')
    return value

def verify_build(directory):
    directory=recover.lab(directory);raw,_=recover._read(directory/build.MANIFEST,4*1024*1024)
    value=shape(json.loads(raw))
    for name,p in value['files'].items():
        if build.digest(directory/name)!=p:raise ValueError('The local On12 binary changed.')
    if build.expected_inputs(directory)!=value['inputs'] or build.inputs(directory)!=value['inputs']:
        raise ValueError('The local On12 sources or dependencies changed.')
    return directory,value,raw

def validate_runtime():
    """Admit only the D3D9 runtime used for the measured private DDI pairing."""
    import ctypes as c
    k=c.WinDLL('kernel32',use_last_error=True)
    # Some hosts return a nonzero count with an empty WOW64 path. Resolve the
    # documented Windows directory instead; never accept an empty relative path.
    k.GetWindowsDirectoryW.argtypes=[c.c_wchar_p,c.c_uint];k.GetWindowsDirectoryW.restype=c.c_uint
    path=c.create_unicode_buffer(512);length=k.GetWindowsDirectoryW(path,512)
    if not length or length>=512 or not path.value:raise RuntimeError('Windows directory unavailable.')
    runtime=Path(path.value)/'SysWOW64/d3d9.dll'
    if build.digest(runtime)!=RUNTIME:raise ValueError('This local On12 build is not admitted for the installed D3D9 runtime.')
    return runtime

class Session:
    def __init__(self,root,case,directory):
        from . import game,water_on12_probe
        self.root,self.case=recover.lab(root),recover.lab(case)
        if self.case.is_relative_to(self.root) or self.root.is_relative_to(self.case):raise ValueError('On12 game and case must be separate.')
        value,self.executable,_=game.platform.load(self.root)
        if value['edition']!='history' or build.digest(self.executable)!=water_on12_probe.loader.UBI:
            raise ValueError('The source-built On12 trial requires the exact History laboratory original.')
        self.directory,self.manifest,self.raw=verify_build(directory)
        if any(a.is_relative_to(z) or z.is_relative_to(a) for a,z in ((self.directory,self.root),(self.directory,self.case))):
            raise ValueError('On12 build, game and case must be separate.')
        self.payload={name:recover._read(self.directory/name,16*1024*1024)[0] for name in PAYLOAD}
        if any(hashlib.sha256(raw).hexdigest()!=self.manifest['files'][name] for name,raw in self.payload.items()):
            raise RuntimeError('The local On12 payload changed during preparation.')
        self.runtime=validate_runtime()
        self.observed=False

    def apply(self):
        recover._no_games()
        if (self.case/JOURNAL).exists() or (self.case/SNAPSHOT).exists():raise ValueError('Recover the existing local On12 case first.')
        for name in PAYLOAD:
            if self.executable.with_name(name).exists():raise ValueError('An existing local runtime must not be overwritten.')
        verify_build(self.directory);validate_runtime()
        recover._write_new(self.case/SNAPSHOT,self.raw)
        record=dict(format='s7-on12-runtime-session-v1',state='prepared',root=str(self.root),
            executable_sha256=build.digest(self.executable),manifest_sha256=build.digest(self.case/SNAPSHOT))
        recover._atomic(self.case/JOURNAL,json.dumps(record,indent=2).encode())
        for name,raw in self.payload.items():
            if build.digest(self.directory/name)!=self.manifest['files'][name]:raise RuntimeError('On12 staging input changed.')
            recover._atomic(recover.plain(self.executable.with_name(name)),raw)
        record['state']='applied';recover._atomic(self.case/JOURNAL,json.dumps(record,indent=2).encode())

    def observe(self,owned,paths):
        if self.observed:return
        owned.owned()
        candidates=list(dict.fromkeys(path for path in paths if path.name.casefold()=='d3d9on12.dll'))
        if not candidates:return
        if len(candidates)!=1 or candidates[0]!=self.executable.with_name('d3d9on12.dll') or build.digest(candidates[0])!=self.manifest['files']['d3d9on12.dll']:
            raise RuntimeError('The requested local On12 runtime was not selected.')
        try:report=json.loads(recover._read(self.case/'water-probe-live.private.json',32768)[0])
        except (OSError,ValueError):return
        if not report.get('attached'):return
        if report.get('on12_query_hr')!=0 or report.get('failure')!=0:raise RuntimeError('The source-built runtime did not create the admitted On12 device.')
        self.observed=True
        recover._write_new(self.case/'on12-runtime-observed.private.json',json.dumps(dict(
            format='s7-on12-runtime-observed-v1',pid=owned.pid,runtime_sha256=self.manifest['files']['d3d9on12.dll'],
            d3d9_sha256=RUNTIME,on12_query_hr=0,source_variant='stock'),indent=2).encode())

    def restore(self):
        if (self.case/JOURNAL).exists():return restore(self.case)

def restore(case):
    from . import game,water_on12_probe
    case=recover.lab(case);recover._no_games()
    value=json.loads(recover._read(case/JOURNAL,16384)[0])
    if (not isinstance(value,dict) or set(value)!={'format','state','root','executable_sha256','manifest_sha256'}
            or value['format']!='s7-on12-runtime-session-v1' or value['state']not in('prepared','applied','restored')
            or value['executable_sha256']!=water_on12_probe.loader.UBI or not pin(value['manifest_sha256'])):
        raise ValueError('Invalid local On12 recovery journal.')
    root=recover.lab(value['root'])
    if root.is_relative_to(case) or case.is_relative_to(root):raise ValueError('Invalid local On12 recovery separation.')
    profile,executable,_=game.platform.load(root)
    if profile['edition']!='history' or build.digest(executable)!=value['executable_sha256']:raise RuntimeError('Original game identity changed.')
    raw,_=recover._read(case/SNAPSHOT,4*1024*1024)
    if build.digest(case/SNAPSHOT)!=value['manifest_sha256']:raise RuntimeError('Local On12 recovery snapshot changed.')
    manifest=shape(json.loads(raw),current=False)
    for name in PAYLOAD:
        target=recover.plain(executable.with_name(name))
        if target.exists() and (value['state']=='restored' or build.digest(target)!=manifest['files'][name]):
            raise RuntimeError('A local runtime file changed; refusing to overwrite newer data.')
    for name in PAYLOAD:
        target=recover.plain(executable.with_name(name))
        if target.exists():target.unlink()
    value['state']='restored';recover._atomic(case/JOURNAL,json.dumps(value,indent=2).encode())
    return {'on12_runtime_restored':True,'saves_restored':False}
