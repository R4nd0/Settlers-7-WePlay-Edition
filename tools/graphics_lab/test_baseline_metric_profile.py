"""Authored CPU fixtures; no real process, game, GPU or debugger is opened."""
import ast
from contextlib import ExitStack
import ctypes as C
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import sys
import tempfile
import unittest
from unittest.mock import Mock,patch

REAL_ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(REAL_ROOT))
SPEC=importlib.util.spec_from_file_location('private_metric_profile',Path(__file__).with_name('baseline_metric_profile.py'))
m=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(m)

class Fixture:
    def __enter__(self):
        self.stack=ExitStack();self.root=Path(self.stack.enter_context(tempfile.TemporaryDirectory(dir=REAL_ROOT/'work',prefix='metric-cpu-')))
        self.case=self.root/'work/case';self.case.mkdir(parents=True)
        self.game=self.root/'work/game/Base/Bin/Settlers7R.exe';self.game.parent.mkdir(parents=True);self.game.write_bytes(b'authored non-game fixture')
        self.child=dict(pid=123,parent=456,path=str(self.game),sha256=m.EXE_SHA256,creation_time=789)
        paths=m.watcher.baseline_source_paths()|{m.SOURCE};pins={}
        for relative in paths:
            source=Path(m.__file__)if relative==m.SOURCE else REAL_ROOT/relative
            current=self.root/relative;current.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,current)
            retained=self.case/'source'/relative;retained.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,retained)
            pins[relative]=hashlib.sha256(current.read_bytes()).hexdigest()
        self.checkpoint=dict(sources=pins,baseline_metric_profile_requested=True,baseline_mode=m.watcher.BASELINE_MODE,
            baseline_crash_watch_allowed=True,crash_watch_allowed=False,native_graphics_hook_requested=False,material_effects_requested=[],
            comparison_uv_requested=False,detailed_shadows_requested=False,water_program_requested=False,terrain_macro_requested=False,
            leaf_hemisphere_requested=False,shadow_filter_requested=False,ambient_occlusion_requested=False)
        self.save();self.owned={**{key:self.child[key]for key in ('pid','parent','path','sha256')},'native_initialized':True}
        (self.case/'owned.private.json').write_text(json.dumps(self.owned),encoding='utf8')
        self.stack.enter_context(patch.object(m,'ROOT',self.root))
        self.stack.enter_context(patch.object(m,'__file__',str(self.root/m.SOURCE)))
        self.native=FakeNative(self.child)
        return self
    def save(self):(self.case/'checkpoint.private.json').write_text(json.dumps(self.checkpoint),encoding='utf8')
    def __exit__(self,*args):return self.stack.__exit__(*args)

class FakeNative(m.BaselineNative):
    def __init__(self,child):
        self.expected=dict(child);self.live=dict(child);self.retained=900;self.closed=[];self.opens=[];self.reads=[]
        self.module_rows=[dict(pid=child['pid'],base=m.IMAGE_BASE,image_bytes=0x1106000,resolved_path=child['path'])]
        self.blocks={m.IMAGE_BASE+rva:data for rva,data,_ in m.SITES};self.short=False;self.fail=False;self.change_after=False
        self.k=Mock();self.k.ReadProcessMemory=Mock(side_effect=self._read)
    def _check_original(self,pid,query=None):
        if self.expected!=self.live or pid!=self.expected['pid']:raise RuntimeError('authored identity change')
    def _open_query(self,pid):self.opens.append((pid,m.MODULE_READ_ACCESS));return 901
    def _close(self,handle):self.closed.append(handle)
    def _read(self,handle,address,output,size,got):
        self.reads.append((handle,address.value,size))
        if self.fail:return False
        raw=self.blocks[address.value];C.memmove(output,raw,len(raw));C.cast(got,C.POINTER(C.c_size_t))[0]=15 if self.short else len(raw)
        if self.change_after:self.live['creation_time']+=1
        return True

def event(stage,pair=1,thread=0x44,user=0x20001000,guard=0,available=True):
    lines=[f'S7_METRIC_{stage} pair={pair} tid={thread:x} user={user:08x}','S7_METRIC_HEADER']
    if available:
        words=[guard]+[0]*12
        for i in range(0,13,4):lines.append(f'{user-0x34+4*i:08x}  '+' '.join(f'{v:08x}'for v in words[i:i+4]))
    else:lines.append('S7_METRIC_HEADER_UNAVAILABLE')
    lines.extend(['S7_METRIC_USER_PREFIX',f'{user:08x}  00000000 00000000 00000000 00000000',
        'S7_METRIC_COUNTER_VTABLE',f'{user+0x70:08x}  '+('010fb340'if stage=='BEFORE'else'010fb334'),
        'S7_METRIC_REGISTRY','013756b0  20002000','20002000  20003000 20004000 20005000 00000001'])
    return '\n'.join(lines)+'\n'

class MetricTests(unittest.TestCase):
    def test_fixed_source11_and_exact_code_queries(self):
        with Fixture()as f:
            proof=m.prepare_owner_profile(f.case,f.native,f.child)
            self.assertEqual(proof['source_count'],11);self.assertEqual(proof['profile'],m.PROFILE)
            self.assertEqual(f.native.opens,[(123,0x100410)])
            self.assertEqual(f.native.reads,[(901,m.IMAGE_BASE+rva,16)for rva,_,_ in m.SITES])
            self.assertEqual(f.native.closed,[901]);self.assertNotIn(900,f.native.closed)
    def test_fixed_query_rights_never_write_or_terminate(self):
        self.assertFalse(m.MODULE_READ_ACCESS&(1|8|32))
    def test_old_closed_case_cannot_gain_metric_permission(self):
        with Fixture()as f:
            (f.case/'result.private.json').write_text('{}')
            with self.assertRaises(ValueError):m.prepare_owner_profile(f.case,f.native,f.child)
            self.assertFalse(f.native.opens)
    def test_explicit_intent_and_all_graphics_modes_refused_before_reads(self):
        for key,value in ((m.INTENT,False),(m.INTENT,1),('native_graphics_hook_requested',True),
                          ('shadow_filter_requested',True),('ambient_occlusion_requested',True)):
            with self.subTest(key=key,value=value),Fixture()as f:
                f.checkpoint[key]=value;f.save()
                with self.assertRaises(ValueError):m.prepare_owner_profile(f.case,f.native,f.child)
                self.assertFalse(f.native.opens)
    def test_unknown_metric_pid_script_or_address_selector_refused(self):
        for key in ('baseline_metric_pid','baseline_metric_commands','baseline_metric_rva'):
            with self.subTest(key=key),Fixture()as f:
                f.checkpoint[key]='arbitrary';f.save()
                with self.assertRaises(ValueError):m.prepare_owner_profile(f.case,f.native,f.child)
                self.assertFalse(f.native.opens)
    def test_missing_changed_current_or_retained_source_refused_before_reads(self):
        for mode in ('missing','current','retained'):
            with self.subTest(mode=mode),Fixture()as f:
                if mode=='missing':f.checkpoint['sources'].pop(m.SOURCE);f.save()
                else:(f.root/m.SOURCE if mode=='current'else f.case/'source'/m.SOURCE).write_bytes(b'changed')
                with self.assertRaises(ValueError):m.prepare_owner_profile(f.case,f.native,f.child)
                self.assertFalse(f.native.opens)
    def test_private_unpromoted_helper_cannot_admit_runtime(self):
        with Fixture()as f,patch.object(m,'__file__',str(Path(__file__).with_name('baseline_metric_profile.py'))):
            with self.assertRaises(ValueError):m.prepare_owner_profile(f.case,f.native,f.child)
            self.assertFalse(f.native.opens)
    def test_wrong_pid_creation_hash_and_module_base_refused(self):
        for mode in ('pid','creation','hash','base'):
            with self.subTest(mode=mode),Fixture()as f:
                if mode=='base':f.native.module_rows[0]['base']+=0x10000
                elif mode=='hash':f.native.expected['sha256']='a'*64
                elif mode=='pid':f.native.expected['pid']+=1
                else:f.native.live['creation_time']+=1
                with self.assertRaises((ValueError,RuntimeError)):m.prepare_owner_profile(f.case,f.native,f.child)
                self.assertFalse(f.native.opens)
    def test_changed_short_failed_code_and_postread_identity_refuse_close_query(self):
        for mode in ('bytes','short','failed','identity'):
            with self.subTest(mode=mode),Fixture()as f:
                if mode=='bytes':f.native.blocks[m.IMAGE_BASE+m.SITES[0][0]]=bytes(16)
                elif mode=='short':f.native.short=True
                elif mode=='failed':f.native.fail=True
                else:f.native.change_after=True
                with self.assertRaises((ValueError,RuntimeError,OSError)):m.prepare_owner_profile(f.case,f.native,f.child)
                self.assertEqual(f.native.closed,[901])
    def test_script_fixed_sites_limits_and_no_target_register_writes(self):
        script=m.commands()
        self.assertEqual(script.count('bp20 (Settlers7R+0xa6db80)'),1)
        self.assertEqual(script.count('bp21 (Settlers7R+0xa6db85)'),1)
        self.assertIn('if (@$t10 == 3)',script);self.assertIn('if (@$t19 >= 0n16)',script)
        self.assertIn('if (@$tid == @$t12)',script);self.assertIn('if ((@edi & 0xffffffff) == @$t11)',script)
        self.assertIn('r @$t11 = (@edi & 0xffffffff)',script)
        self.assertIn('L0xd',script);self.assertIn('$vvalid(@$t15,0x10)',script)
        writes=re.findall(r'\br\s+([^=;]+)=',script)
        self.assertTrue(all(word.strip()in {'@$t0','@$t10','@$t11','@$t12','@$t13','@$t15','@$t19'}for word in writes))
        for command in ('.call ','.fnret ','ew ','ed ','eb ','eq ','bc *','bp /w'):
            self.assertNotIn(command,script)
        self.assertEqual(len(script.splitlines()),6)
        self.assertLessEqual(max(map(len,script.splitlines())),4096)
    def test_bp_quoted_commands_roundtrip_without_physical_newline(self):
        for body in (m._before(),m._after()):
            self.assertNotIn('\n',body)
            self.assertEqual(json.loads('"'+m._quoted(body)+'"'),body)
            self.assertIn('\\n',body)
    def test_original_av_handlers_are_unchanged_tails(self):
        base=m.watcher.commands(steam_heap=True);script=m.commands()
        restored=script.replace(m._cleanup()+'.echo S7_FIRST_CHANCE_AV;','.echo S7_FIRST_CHANCE_AV;')
        restored=restored.replace(m._cleanup()+'.echo S7_SECOND_CHANCE_AV;','.echo S7_SECOND_CHANCE_AV;')
        self.assertIn(base.splitlines()[0],restored)
        self.assertIn(base.splitlines()[1],restored)
        tree=ast.parse(Path(m.watcher.__file__).read_text(encoding='utf8'))
        function=next(node for node in tree.body if isinstance(node,ast.FunctionDef)and node.name=='commands')
        self.assertEqual(hashlib.sha256(ast.dump(function,include_attributes=False).encode()).hexdigest(),
            '09de568e7c9218c9cb9aa1b0ffaee3668717e480f1812ff4aec01409caf7c681')
    def test_parser_bad_before_and_changed_after(self):
        parsed=m.parse_records((event('BEFORE')+event('AFTER',guard=0xdeadb0b0)).encode())
        self.assertTrue(parsed['pairs'][0]['bad_guard_before']);self.assertTrue(parsed['pairs'][0]['header_changed'])
        self.assertFalse(parsed['cause_identified'])
    def test_parser_unavailable_header_and_unpaired_before_never_infer_bad(self):
        parsed=m.parse_records((event('BEFORE',available=False)+event('AFTER')).encode())
        self.assertIsNone(parsed['pairs'][0]['bad_guard_before'])
        self.assertFalse(parsed['pairs'][0]['header_before_after_available'])
        self.assertEqual(m.parse_records(event('BEFORE').encode())['pairs'],[])
    def test_parser_rejects_cross_thread_cross_user_duplicates_and_overbudget(self):
        for bad in (event('BEFORE')+event('AFTER',thread=0x45),event('BEFORE')+event('AFTER',user=0x20002000),
                    event('BEFORE')+event('BEFORE'),event('AFTER'),
                    ''.join(event(stage,pair=min(i,3))for i in range(1,5)for stage in ('BEFORE','AFTER'))):
            with self.subTest(prefix=bad[:100]),self.assertRaises(ValueError):m.parse_records(bad.encode())
    def test_parser_ignores_echoed_commands_but_rejects_unbounded_words(self):
        echoed='0:000> bp20 addr "S7_METRIC_BEFORE pair=1 tid=44 user=20001000"\n'
        self.assertEqual(m.parse_records((echoed+event('BEFORE')+event('AFTER')).encode())['pairs'][0]['pair'],1)
        bad=event('BEFORE').replace('20000fcc','30000fcc')+event('AFTER')
        with self.assertRaises(ValueError):m.parse_records(bad.encode())
        with self.assertRaises(ValueError):m.parse_records(b'x'*(m.MAX_LOG+1))
    def test_parser_three_full_pairs(self):
        raw=''.join(event(stage,pair=i)for i in range(1,4)for stage in ('BEFORE','AFTER')).encode()
        self.assertEqual(len(m.parse_records(raw)['pairs']),3)
        with self.assertRaises(ValueError):m.parse_records(raw+event('BEFORE',pair=4).encode())
    def test_high_bit_original_x86_user_pointer_is_preserved(self):
        # Same high-bit range as actualSteam27, not a sign-extended64-bit pointer.
        raw=(event('BEFORE',user=0x92ea3d8c)+event('AFTER',user=0x92ea3d8c)).encode()
        pair=m.parse_records(raw)['pairs'][0]
        self.assertEqual(pair['user'],'0x92ea3d8c');self.assertTrue(pair['header_before_after_available'])

if __name__=='__main__':unittest.main(verbosity=2)
