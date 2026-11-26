"""Fixed, explicit Steam SDK baseline lifetime profile; no CLI or debugger launch.

Private preparation. The host must integrate a fresh owner intent/source11 route
before using this helper. Existing graphics/generic AV command builders stay intact.
"""
from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
from pathlib import Path
import re

from tools.graphics_lab import crash_watch as watcher
from tools.graphics_lab.baseline_modules import BaselineNative,MODULE_READ_ACCESS

ROOT=Path(__file__).resolve().parents[2]
SOURCE='tools/graphics_lab/baseline_metric_profile.py'
PROFILE='steam-perfcounter-pre-post-v1'
INTENT='baseline_metric_profile_requested'
EXE_SHA256='8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410'
IMAGE_BASE=0x400000
REGISTRY_RVA=0xf756b0
MAX_PAIRS=3
MAX_BREAKPOINT_HITS=16
MAX_LOG=1024*1024
SITES=(
    (0xa6db80,bytes.fromhex('e8ebfdffff57e8a599feff8b35b05637'),'aa280da47ac3e84a4aa6af12f7b6004f07b51e6d542ba09e3d7508cce7f0da56'),
    (0xa6db85,bytes.fromhex('57e8a599feff8b35b056370183c40483'),'3424b99028661bea50a479db78901b17626ccabedba405ccaef312d229388370'),
)


def _sha(path):
    with Path(path).open('rb')as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def validate_owner_checkpoint(case,child):
    """No live read until this fresh, explicit source11 checkpoint is verified."""
    case=watcher.recover.lab(case)
    if (case/'result.private.json').exists():raise ValueError('Closed case cannot enable metric diagnostics.')
    executing=watcher.recover.plain(Path(__file__))
    if executing!=watcher.recover.plain(ROOT/SOURCE):raise ValueError('Private helper is not a promoted controller source.')
    checkpoint=watcher._json(case/'checkpoint.private.json')
    if (checkpoint.get(INTENT)is not True or checkpoint.get('baseline_mode')!=watcher.BASELINE_MODE
            or checkpoint.get('baseline_crash_watch_allowed')is not True or checkpoint.get('crash_watch_allowed')is not False
            or checkpoint.get('native_graphics_hook_requested')is not False or checkpoint.get('material_effects_requested')!=[]):
        raise ValueError('Missing explicit fixed baseline metric intent.')
    if any(key.startswith('baseline_metric_')and key!=INTENT for key in checkpoint):
        raise ValueError('No arbitrary metric profile selector is accepted.')
    for key in ('comparison_uv_requested','detailed_shadows_requested','water_program_requested',
                'terrain_macro_requested','leaf_hemisphere_requested','shadow_filter_requested','ambient_occlusion_requested'):
        if checkpoint.get(key)is not False:raise ValueError('Metric profile cannot accompany graphics experiments.')
    sources=checkpoint.get('sources')
    if not isinstance(sources,dict)or set(sources)!=(watcher.baseline_source_paths()|{SOURCE}):
        raise ValueError('Metric profile requires its fresh exact eleven-source checkpoint.')
    for relative,pin in sources.items():
        if (not isinstance(pin,str)or not re.fullmatch('[0-9a-f]{64}',pin)
                or _sha(watcher.recover.plain(ROOT/relative))!=pin
                or _sha(watcher.recover.plain(case/'source'/relative))!=pin):
            raise ValueError('Current/retained metric source checkpoint changed.')
    if not isinstance(child,dict)or set(child)!={'pid','parent','path','sha256','creation_time'}:
        raise ValueError('Original retained child identity required.')
    if (type(child['pid'])is not int or not 1<=child['pid']<=0xffffffff
            or type(child['parent'])is not int or not 1<=child['parent']<=0xffffffff
            or type(child['creation_time'])is not int or child['creation_time']<=0
            or child['sha256']!=EXE_SHA256):
        raise ValueError('Only the fixed original Steam profile is supported.')
    executable=watcher.recover.plain(child['path'])
    if not executable.is_relative_to(ROOT/'work')or executable.name.casefold()!='settlers7r.exe':
        raise ValueError('Metric diagnostics require the admitted owned test copy.')
    owned=watcher._json(case/'owned.private.json')
    if any(owned.get(key)!=child[key]for key in ('pid','parent','path','sha256'))or owned.get('native_initialized')is not True:
        raise ValueError('Metric checkpoint does not name the original initialized owner child.')
    return {'profile':PROFILE,'source_sha256':sources[SOURCE],'source_count':11}


def verify_runtime_sites(native,child):
    """Read exactly two fixed 16-byte windows through the bound original provider."""
    if (not isinstance(native,BaselineNative)or native.expected!=child or native.retained is None
            or child.get('sha256')!=EXE_SHA256):
        raise ValueError('Metric reads require the original bound baseline provider.')
    native._check_original(child['pid'])
    rows=[row for row in native.module_rows
          if watcher.recover.plain(row['resolved_path'])==watcher.recover.plain(child['path'])]
    if (not 1<=len(rows)<=2 or any(row['pid']!=child['pid']or row['base']!=IMAGE_BASE
            or row['image_bytes']<SITES[-1][0]+16 for row in rows)):
        raise ValueError('Original fixed-base EXE module mapping is unavailable.')
    query=native._open_query(child['pid'])
    proof=[]
    try:
        native._check_original(child['pid'],query)
        function=native.k.ReadProcessMemory
        function.argtypes=[W.HANDLE,C.c_void_p,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)]
        function.restype=W.BOOL
        for rva,expected,pin in SITES:
            data=(C.c_ubyte*16)();got=C.c_size_t()
            if not function(query,C.c_void_p(IMAGE_BASE+rva),data,16,C.byref(got))or got.value!=16:
                raise OSError('The fixed original metric code window is unavailable.')
            raw=bytes(data)
            if raw!=expected or hashlib.sha256(raw).hexdigest()!=pin:
                raise ValueError('Loaded original metric callsite bytes changed.')
            proof.append({'rva':hex(rva),'bytes':16,'sha256':pin})
        native._check_original(child['pid'],query)
    finally:native._close(query)
    return {'format':'s7-baseline-metric-original-code-v1','profile':PROFILE,'original':dict(child),
            'image_base':IMAGE_BASE,'sites':proof,'read_access':MODULE_READ_ACCESS,
            'process_write_or_terminate_requested':False,'code_guard_reads_only':True,
            'software_breakpoint_stage_is_separate_and_intrusive':True}


def prepare_owner_profile(case,native,child):
    source=validate_owner_checkpoint(case,child)
    return {**source,**verify_runtime_sites(native,child)}


def _cleanup():
    return '.if (@$bp20 != 0) { bc 20 }; .if (@$bp21 != 0) { bc 21 }; '


def _snapshot():
    # Verified original ABI: allocation header=user-0x34; Counter member=user+0x70.
    # User prefix and vtable are raw words, not assumed allocation ownership.
    return (
        '.echo S7_METRIC_HEADER; '
        '.if ($vvalid(@$t11-0x34,0x34)) { dd (@$t11-0x34) L0xd } .else { .echo S7_METRIC_HEADER_UNAVAILABLE }; '
        '.echo S7_METRIC_USER_PREFIX; '
        '.if ($vvalid(@$t11,0x10)) { dd @$t11 L4 } .else { .echo S7_METRIC_USER_UNAVAILABLE }; '
        '.echo S7_METRIC_COUNTER_VTABLE; '
        '.if ($vvalid(@$t11+0x70,4)) { dd (@$t11+0x70) L1 } .else { .echo S7_METRIC_COUNTER_UNAVAILABLE }; '
        '.echo S7_METRIC_REGISTRY; '
        '.if ($vvalid(Settlers7R+0xf756b0,4)) { '
        'dd (Settlers7R+0xf756b0) L1; r @$t15 = poi(Settlers7R+0xf756b0); '
        '.if (@$t15 != 0) { .if ($vvalid(@$t15,0x10)) { dd @$t15 L4 } '
        '.else { .echo S7_METRIC_REGISTRY_UNAVAILABLE } } '
        '.else { .echo S7_METRIC_REGISTRY_UNAVAILABLE } } '
        '.else { .echo S7_METRIC_REGISTRY_UNAVAILABLE }; '
    )


def _before():
    return (
        'r @$t19 = @$t19+1; '
        '.if (@$t19 >= 0n16) { .echo S7_METRIC_HIT_LIMIT; '+_cleanup()+'} '
        '.else { .if (@$t10 < 3) { .if (@$t13 == 0) { '
        'r @$t11 = (@edi & 0xffffffff); r @$t12 = @$tid; r @$t13 = 1; '
        '.printf "S7_METRIC_BEFORE pair=%u tid=%x user=%p\\n", @$t10+1, @$t12, @$t11; '
        +_snapshot()+'} } }; gc'
    )


def _after():
    return (
        'r @$t19 = @$t19+1; '
        '.if (@$t19 >= 0n16) { .echo S7_METRIC_HIT_LIMIT; '+_cleanup()+'} '
        '.else { .if (@$t13 != 0) { .if (@$tid == @$t12) { .if ((@edi & 0xffffffff) == @$t11) { '
        '.printf "S7_METRIC_AFTER pair=%u tid=%x user=%p\\n", @$t10+1, @$t12, @$t11; '
        +_snapshot()+'r @$t10 = @$t10+1; r @$t13 = 0; '
        '.if (@$t10 == 3) { .echo S7_METRIC_THREE_PAIRS_COMPLETE; '+_cleanup()+'} '
        '} } } }; gc'
    )


def _quoted(command):return command.replace('\\','\\\\').replace('"','\\"')


def _code_condition(rva,expected):
    return ' && '.join(f'dwo(Settlers7R+0x{rva+index:x}) == 0x{int.from_bytes(expected[index:index+4],"little"):08x}'
                       for index in range(0,16,4))


def commands():
    """Fixed script only. Requires source-bound owner/runtime guard before use."""
    base=watcher.commands(steam_heap=True)
    # Preserve each original AV handler byte for byte as a tail. Only remove
    # this profile's owned breakpoints before the original diagnostic handler.
    for marker in ('.echo S7_FIRST_CHANCE_AV;', '.echo S7_SECOND_CHANCE_AV;'):
        if base.count(marker)!=1:raise ValueError('Generic AV command layout changed.')
        base=base.replace(marker,_cleanup()+marker)
    ready='.echo S7_OWNED_CRASH_WATCH_READY\ng\n'
    if not base.endswith(ready):raise ValueError('Generic debugger readiness layout changed.')
    reject='.echo S7_METRIC_CODE_GUARD_REJECTED; qd'
    body=('r @$t10 = 0; r @$t11 = 0; r @$t12 = 0; r @$t13 = 0; r @$t15 = 0; r @$t19 = 0; '
          f'bp20 (Settlers7R+0xa6db80) "{_quoted(_before())}"; '
          f'bp21 (Settlers7R+0xa6db85) "{_quoted(_after())}"; '
          '.if (@$bp20 == (Settlers7R+0xa6db80)) { '
          '.if (@$bp21 == (Settlers7R+0xa6db85)) { .echo S7_METRIC_CODE_GUARDS_ACCEPTED } '
          '.else { '+_cleanup()+reject+' } } .else { '+_cleanup()+reject+' }; ')
    for rva,expected,_ in reversed(SITES):
        body=(f'.if ($vvalid(Settlers7R+0x{rva:x},0x10)) {{ '
              f'.if ({_code_condition(rva,expected)}) {{ '+body+'} .else { '+reject+' } '
              '} .else { '+reject+' }; ')
    body='.if ((@$bp20 != 0) || (@$bp21 != 0)) { '+reject+' } .else { '+body+'}\n'
    script='.expr /s masm\n'+base[:-len(ready)]+body+ready
    if any(len(line)>4096 for line in script.splitlines()):raise ValueError('Fixed metric debugger command exceeds parser bound.')
    return script


def parse_records(raw):
    """Parse emitted fixed tags only; never treat command echoes as observations."""
    if not isinstance(raw,bytes)or len(raw)>MAX_LOG:raise ValueError('Bounded metric log required.')
    events=[];current=None;part=None
    for line in raw.decode('utf8',errors='replace').splitlines():
        match=re.fullmatch(r'S7_METRIC_(BEFORE|AFTER) pair=([1-3]) tid=([0-9a-fA-F]+) user=(?:0x)?([0-9a-fA-F`]+)',line.strip())
        if match:
            if len(events)>=6:raise ValueError('More than three metric pairs.')
            user=int(match[4].replace('`',''),16)
            if not 0x34<=user<=0xffffff8b:raise ValueError('Invalid original x86 user pointer.')
            current={'stage':match[1].lower(),'pair':int(match[2]),'thread':int(match[3],16),'user':user,
                     'header':{},'prefix':{},'counter':{},'registry':{}}
            events.append(current);part=None;continue
        if line.strip().startswith(('S7_METRIC_BEFORE ','S7_METRIC_AFTER ')):
            raise ValueError('Invalid or over-budget metric marker.')
        if current is None:continue
        tags={'S7_METRIC_HEADER':'header','S7_METRIC_USER_PREFIX':'prefix',
              'S7_METRIC_COUNTER_VTABLE':'counter','S7_METRIC_REGISTRY':'registry'}
        if line.strip()in tags:part=tags[line.strip()];continue
        if line.strip().startswith('S7_METRIC_'):
            part=None;continue
        match=re.fullmatch(r'([0-9a-fA-F]{8})\s+((?:[0-9a-fA-F]{8}\s*){1,4})',line.strip())
        if part and match:
            address=int(match[1],16)
            for index,value in enumerate(match[2].split()):
                where=address+4*index
                allowed=(part=='header'and current['user']-0x34<=where<current['user']
                    or part=='prefix'and current['user']<=where<current['user']+0x10
                    or part=='counter'and where==current['user']+0x70
                    or part=='registry'and (where==IMAGE_BASE+REGISTRY_RVA
                        or (IMAGE_BASE+REGISTRY_RVA in current['registry']
                            and 0<current['registry'][IMAGE_BASE+REGISTRY_RVA]<=0xfffffff0
                            and current['registry'][IMAGE_BASE+REGISTRY_RVA]<=where<current['registry'][IMAGE_BASE+REGISTRY_RVA]+0x10)))
                if not allowed:raise ValueError('Metric log includes an unbounded/unexpected word.')
                if where in current[part]:raise ValueError('Duplicate metric word.')
                current[part][where]=int(value,16)
    paired=[];pending=None
    for event in events:
        if event['stage']=='before':
            if pending is not None:raise ValueError('Overlapping metric before records.')
            pending=event
        else:
            if pending is None or any(event[key]!=pending[key]for key in ('pair','thread','user')):
                raise ValueError('Metric post record does not match original thread/user.')
            expected={event['user']-0x34+4*i for i in range(13)}
            available=set(pending['header'])==expected and set(event['header'])==expected
            paired.append({'pair':event['pair'],'thread':event['thread'],'user':hex(event['user']),
                'header_before_after_available':available,'before':pending,'after':event,
                'bad_guard_before':pending['header'].get(event['user']-0x34)!=0xdeadb0b0 if available else None,
                'header_changed':pending['header']!=event['header']if available else None})
            pending=None
    if [pair['pair']for pair in paired]!=list(range(1,len(paired)+1)):raise ValueError('Nonsequential metric pairs.')
    return {'format':'s7-baseline-metric-phase-records-v1','profile':PROFILE,'pairs':paired,
            'unpaired_before':pending,'runtime_qualified':False,'cause_identified':False,
            'software_breakpoints_change_timing':True}
