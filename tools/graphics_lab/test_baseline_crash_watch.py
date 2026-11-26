"""Authored CPU fixtures only: never enumerate/attach/start/stop a real process."""
import ast
from contextlib import ExitStack,nullcontext
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile
import unittest
from unittest.mock import Mock,patch

ROOT=Path(__file__).resolve().parents[2]
from tools.graphics_lab import crash_watch as b

# Pre-baseline graphics admission and CDB semantics; changes require explicit review.
GUARD_AST_PINS = {'_case': '33f2b684ce356938ed5730f65d08b09394cd2ca2a26241b838dcc025feee60c3', 'commands': '09de568e7c9218c9cb9aa1b0ffaee3668717e480f1812ff4aec01409caf7c681', 'arguments': '17b889e68a3d6ff245d8c3b227ecc1b8a24557634e833e70f4de54021a6124de', 'environment': '394a7c892babc91f99ad644c4184dabe1cff73b69c7f247d369d200a0e8bc9bb', 'Native': '9ffd0f5cd9568eff5a335090f7926f8eec93b881aeea642d19ab84bf271d28f5', 'verify_cdb': '755b46d369c46185e0ce2fb53f26f1c41d2902880b6efa8df44fe5f51e282b06', '_hook': 'cbb987c85a40860d852c0f477af94a742e0295c37806ec9d4f1c2ec40ba7f481', '_match': 'cbbd790bbcf6790b14e00590edc33ba65b69ad7b5c25faa0cc100ce56d6c9878', 'admit': '1e65363d2eb08b6bac9ff8331368e5ab30bd69e889db0c231f3b3f9090397046', '_admitted': '5fb956683690668bac9878d3f7b6dc782ad625749d62f07a9b3dba974f4bc18f', 'av_event_counts': 'e2cdc633cc7593f15b73b6494f0e98133c98df3007f876e741f432e48fa43441', 'stop_debugger': 'c62c4be14d0114fcbafdde4d77d7c8c8dc2d0a826cbe2e8efbd7f57766b84e46', 'final_target': 'fc9721b9f5f6975c349943a0b25eb5673e5c1d76aa9c6a0ca6cc8bac7a546eeb'}


def pe(export=None):
    data=bytearray(0x600);data[:2]=b'MZ';struct.pack_into('<I',data,0x3c,0x80)
    data[0x80:0x84]=b'PE\0\0';struct.pack_into('<HH',data,0x84,0x14c,1);struct.pack_into('<H',data,0x94,224)
    optional=0x98;struct.pack_into('<H',data,optional,0x10b);struct.pack_into('<I',data,optional+60,0x200)
    struct.pack_into('<4I',data,optional+224+8,0x400,0x1000,0x400,0x200)
    if export:
        struct.pack_into('<II',data,optional+96,0x1000,0x200);struct.pack_into('<I',data,0x200+24,1)
        struct.pack_into('<I',data,0x200+32,0x1040);struct.pack_into('<I',data,0x240,0x1080)
        raw=export.encode()+b'\0';data[0x280:0x280+len(raw)]=raw
    return bytes(data)


class Case:
    def __enter__(self):
        self.stack=ExitStack();folder=self.stack.enter_context(tempfile.TemporaryDirectory(prefix='baseline-cpu-',dir=ROOT/'work'))
        base=Path(folder);self.case=base/'case';self.case.mkdir();self.root=base/'game';self.root.mkdir()
        self.raw=pe();self.pin=hashlib.sha256(self.raw).hexdigest();names=('Base/Bin/Settlers7R.exe','Base/Bin/uplay_r1_loader.dll','Base/Bin/d3d9.dll','Base/Bin/Vision90.dll','Base/Bin/vBase90.dll')
        self.critical={n:self.pin for n in names}
        for name in names:
            path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(self.raw)
        self.exe=self.root/names[0];self.adapter=self.root/names[1]
        manifest=self.root/'Launcher/platform-standalone.json';manifest.parent.mkdir();manifest.write_text(json.dumps(dict(format='weplay-platform-independent-v1',edition='history',files={names[1]:self.pin})))
        self.platform=json.loads(manifest.read_text())
        self.stack.enter_context(patch.object(b.recover,'EXES',{self.pin}));self.stack.enter_context(patch.object(b.recover,'DXVK',self.pin))
        self.stack.enter_context(patch.object(b,'_platform',return_value=(self.platform,self.exe,self.adapter)))
        original={'exists':False};targets={'options':{'relative':'LocalData/ProfileRoot/THE SETTLERS 7 - History Edition/Options.ini','original':original},
            'runtime':{'relative':'LocalData/s7-local-runtime.ini','original':original},'config':{'relative':'Base/Bin/dxvk.conf','original':original}}
        self.write(b.recover.JOURNAL,dict(format=b.recover.FORMAT,state='prepared',root=str(self.root),critical=self.critical,targets=targets,shared={},attempts=0))
        # Empty shared roots are not accepted in production: use one authored,
        # absent profile with an actual unchanged snapshot instead of bypassing.
        from tools.shadow_detail.profile_guard import snapshot_profile
        journal=self.read(b.recover.JOURNAL);shared=base/'shared-profile'
        journal['shared']={'shared-fixture':{'root':str(shared),'snapshot':snapshot_profile(shared)}};self.write(b.recover.JOURNAL,journal)
        sources={name:b.digest(ROOT/name)for name in b.baseline_source_paths()}
        for name in sources:
            out=self.case/'source'/name;out.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,out)
        self.checkpoint=dict(files=self.critical,sources=sources,baseline_mode=b.BASELINE_MODE,
            baseline_crash_watch_allowed=True,crash_watch_allowed=False,native_graphics_hook_requested=False,
            comparison_uv_requested=False,detailed_shadows_requested=False,material_effects_requested=[])
        self.write('checkpoint.private.json',self.checkpoint)
        self.record=dict(pid=123,parent=os.getpid(),path=str(self.exe),sha256=self.pin,native_initialized=True)
        self.write('owned.private.json',self.record)
        self.child={**{k:self.record[k]for k in ('pid','parent','path','sha256')},'creation_time':1000}
        self.parent=dict(pid=os.getpid(),parent=44,path=str(ROOT/'python-owner.exe'),sha256='a'*64,creation_time=900)
        self.native=Mock();self.native.modules.return_value=[self.root/n for n in names]
        self.native.module_rows=[dict(pid=123,base=0x400000+index*0x10000,image_bytes=4096,
            reported_path=str(self.root/name),resolved_path=str(self.root/name))for index,name in enumerate(names)]
        self.native.alias_proofs=[]
        self.native.identity.side_effect=lambda pid,handle:dict(self.child if pid==123 else self.parent)
        self.owned=Mock(pid=123,_handle=987,executable=self.exe,expected=self.pin,native_initialized=True,_weplay_owned=True)
        self.owned.poll.return_value=None
        self.stack.enter_context(patch.object(b,'_baseline_native',return_value=self.native))
        return self
    def __exit__(self,*args):return self.stack.__exit__(*args)
    def write(self,name,value):(self.case/name).write_text(json.dumps(value))
    def read(self,name):return json.loads((self.case/name).read_text())
    def admit(self):return b.baseline_admit(self.owned,self.case,load_and_new_save_confirmed=True)
    def alias(self):
        windows=self.root/'Windows';suffix=Path('DriverStore/FileRepository/vendor/driver.dll')
        reported=windows/'SysWOW64'/suffix;resolved=windows/'System32'/suffix
        resolved.parent.mkdir(parents=True);resolved.write_bytes(pe())
        row=dict(pid=123,base=0x20000000,image_bytes=4096,reported_path=str(reported),resolved_path=str(resolved))
        mapped=r'\Device\HarddiskVolume4\Windows\System32\DriverStore\FileRepository\vendor\driver.dll'
        proof=dict(original_identity=dict(self.child),module_base='0x20000000',image_bytes=4096,
            reported_path=str(reported),resolved_path=str(resolved),mapped_nt=mapped,final_nt=mapped,
            file_sha256=b.digest(resolved),file_identity=dict(volume_serial=4,file_index=123,bytes=resolved.stat().st_size),
            method='original-map-equals-opened-file-VOLUME_NAME_NT',read_only=True)
        self.native.module_rows.append(row);self.native.alias_proofs.append(proof)
        self.native.modules.return_value.append(resolved)
        return row,proof,resolved


class BaselineTests(unittest.TestCase):
    def test_ordinary_case_only_module_path_requires_no_alias(self):
        with Case()as c:
            row=c.native.module_rows[0];raw=row['reported_path'].upper();row['reported_path']=raw
            self.assertNotEqual(row['reported_path'],row['resolved_path'])
            self.assertEqual(b.recover.plain(row['reported_path']),b.recover.plain(row['resolved_path']))
            proof=b._baseline_module_proof(c.native,c.child)
            self.assertEqual(proof['module_alias_proofs'],[])
            self.assertEqual(proof['module_rows'][0]['reported_path'],raw)

    def test_real_alias_raw_path_case_matches_only_existing_nt_proof(self):
        with Case()as c:
            row,proof,_=c.alias();row['reported_path']=row['reported_path'].upper()
            self.assertEqual(b._baseline_module_proof(c.native,c.child)['module_alias_proofs'],[proof])
            c.native.alias_proofs=[]
            with self.assertRaises(ValueError):b._baseline_module_proof(c.native,c.child)

    def test_distinct_drive_folder_and_file_are_not_case_aliases(self):
        for change in ('drive','folder','file'):
            with self.subTest(change=change),Case()as c:
                row=c.native.module_rows[0];original=Path(row['reported_path'])
                changed=(Path(('C'if original.drive[0].upper()!='C'else'Z')+':/distinct/Settlers7R.exe')
                    if change=='drive'else original.parent/'distinct'/original.name if change=='folder'
                    else original.with_name('different.dll'))
                row['reported_path']=str(changed)
                with self.assertRaises((ValueError,OSError)):b._baseline_module_proof(c.native,c.child)

    def test_real_alias_with_case_variation_still_checks_nt_hash_creation(self):
        for change in ('nt-volume','nt-file','hash','creation'):
            with self.subTest(change=change),Case()as c:
                row,proof,_=c.alias();row['reported_path']=row['reported_path'].upper()
                if change=='nt-volume':proof['final_nt']=proof['final_nt'].replace('Volume4','Volume5')
                elif change=='nt-file':proof['final_nt']=proof['final_nt'].replace('driver.dll','different.dll')
                elif change=='hash':proof['file_sha256']='f'*64
                else:proof['original_identity']['creation_time']+=1
                with self.assertRaises(ValueError):b._baseline_module_proof(c.native,c.child)

    def test_dedicated_provider_checkpoint_is_required_before_process_queries(self):
        with Case()as c:
            self.assertIn('tools/graphics_lab/baseline_modules.py',b.baseline_source_paths())
            checkpoint=c.read('checkpoint.private.json');checkpoint['sources'].pop('tools/graphics_lab/baseline_modules.py')
            c.write('checkpoint.private.json',checkpoint)
            with self.assertRaises(ValueError):c.admit()
            c.native.bind_original.assert_not_called();c.native.open.assert_not_called()

    def test_alias_proof_retained_and_bound_to_original_owner_handle(self):
        with Case()as c:
            row,proof,path=c.alias();admission=c.admit()
            self.assertEqual(admission['module_rows'][-1],row);self.assertEqual(admission['module_alias_proofs'],[proof])
            _,value,child,parent=b._baseline_admitted(c.case,c.native)
            self.assertEqual(value['baseline_alias_paths'],[str(path)])
            self.assertEqual(c.native.bind_original.call_args_list[0].args,(c.child,987))
            self.assertEqual(c.native.bind_original.call_args_list[-1].args,(c.child,child))

    def test_alias_record_identity_mapping_hash_and_unproved_resolution_rejected(self):
        for mutation in ('nt','child','hash','missing','extra-key','missing-row'):
            with self.subTest(mutation=mutation),Case()as c:
                _,proof,_=c.alias()
                if mutation=='nt':proof['final_nt']=proof['final_nt'].replace('Volume4','Volume5')
                elif mutation=='child':proof['original_identity']['creation_time']+=1
                elif mutation=='hash':proof['file_sha256']='f'*64
                elif mutation=='missing':c.native.alias_proofs=[]
                elif mutation=='extra-key':proof['arbitrary_pid']=456
                else:c.native.module_rows.pop()
                with self.assertRaises(ValueError):c.admit()

    def test_saved_alias_proof_cannot_change_before_watcher_admission(self):
        with Case()as c:
            _,proof,_=c.alias();c.admit()
            proof['file_identity']['file_index']+=1
            with self.assertRaises(RuntimeError):b._baseline_admitted(c.case,c.native)

    def test_native_module_shapes_and_only_pinned_main_exe_duplicate(self):
        with Case() as case:
            paths=list(case.native.modules.return_value)
            case.native.modules.return_value=set(paths)
            self.assertEqual(set(b._baseline_modules(case.native,case.owned.pid,case.root,case.critical)),set(paths))
            verified=set()
            b._baseline_absence(case.native,case.owned.pid,verified)
            self.assertEqual(verified,set(paths))
            case.native.modules.return_value=paths+[case.exe]
            self.assertEqual(len(b._baseline_modules(case.native,case.owned.pid,case.root,case.critical)),len(paths)+1)
            b._baseline_absence(case.native,case.owned.pid,verified)
            case.native.modules.return_value=paths+[case.exe,case.exe]
            with self.assertRaises(RuntimeError):b._baseline_modules(case.native,case.owned.pid,case.root,case.critical)
            case.native.modules.return_value=paths+[case.adapter]
            with self.assertRaises(RuntimeError):b._baseline_modules(case.native,case.owned.pid,case.root,case.critical)
            with self.assertRaises(RuntimeError):b._baseline_absence(case.native,case.owned.pid,verified)
            case.native.modules.return_value=paths[1:]+[case.adapter]
            with self.assertRaises(RuntimeError):b._baseline_modules(case.native,case.owned.pid,case.root,case.critical)
    def test_original_graphics_and_cdb_guards_remain_ast_identical(self):
        def definitions(path):return {n.name:ast.dump(n,include_attributes=False)for n in ast.parse(path.read_text(encoding='utf-8')).body if isinstance(n,(ast.FunctionDef,ast.ClassDef))}
        current=definitions(ROOT/'tools/graphics_lab/crash_watch.py')
        for name,pin in GUARD_AST_PINS.items():
            with self.subTest(name=name):self.assertEqual(hashlib.sha256(current[name].encode()).hexdigest(),pin)
    def test_nonbaseline_watch_branch_remains_ast_identical(self):
        class Projection(ast.NodeTransformer):
            def visit_Name(self,node):
                return ast.Constant(False) if node.id=='baseline' and isinstance(node.ctx,ast.Load) else node
            def visit_IfExp(self,node):
                node=self.generic_visit(node)
                return node.orelse if isinstance(node.test,ast.Constant) and node.test.value is False else node
            def visit_If(self,node):
                node=self.generic_visit(node)
                return node.orelse if isinstance(node.test,ast.Constant) and node.test.value is False else node
            def visit_BoolOp(self,node):
                node=self.generic_visit(node)
                return ast.Constant(False) if isinstance(node.op,ast.And) and any(isinstance(value,ast.Constant) and value.value is False for value in node.values) else node
        tree=ast.parse((ROOT/'tools/graphics_lab/crash_watch.py').read_text(encoding='utf-8'))
        function=next(node for node in tree.body if isinstance(node,ast.FunctionDef) and node.name=='watch')
        value=ast.dump(Projection().visit(function),include_attributes=False)
        # Recorded before provider integration, with baseline=False branches removed.
        self.assertEqual(hashlib.sha256(value.encode()).hexdigest(),
            'c3f7159ca5c4ac42aea7c82446417acb630af884d8329f5efb1602a31f0f5bb1')
    def test_explicit_options_reject_all_graphics_paths_before_process_work(self):
        values=dict(baseline=True,crash_watch=True,hook=None,callback=None,observer=None,capture=None,materials=(),comparison=False,detailed_shadows=False,initially_off=False,config_version=1)
        b.baseline_options(**values)
        for changes in ({'baseline':False},{'baseline':1},{'crash_watch':False},{'hook':'hook.dll'},{'callback':Mock()},
                        {'observer':'catalog'},{'capture':'input'},{'materials':['improved_water']},{'comparison':True},
                        {'detailed_shadows':True},{'initially_off':True},{'config_version':2},{'config_version':True}):
            with self.subTest(changes=changes),self.assertRaises(ValueError):b.baseline_options(**{**values,**changes})
    def test_only_separate_owner_admission_command_no_pid_or_export_selector(self):
        raw=b'{"operation":"baseline-crash-watch-admit","sequence":2}'
        self.assertEqual(b.baseline_command(raw,1)['sequence'],2);self.assertIsNone(b.baseline_command(raw,2))
        for raw in (b'{"operation":"crash-watch-admit","sequence":2}',b'{"operation":"capture","sequence":2}',
                    b'{"operation":"baseline-crash-watch-admit","sequence":true}',b'{"operation":"baseline-crash-watch-admit","sequence":2,"pid":42}'):
            with self.subTest(raw=raw),self.assertRaises(ValueError):b.baseline_command(raw,0)
    def test_pe_exports_catch_renamed_graphics_module(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'work')as directory:
            p=Path(directory)/'innocent.dll';p.write_bytes(pe());self.assertFalse(b.graphics_export_present(p))
            for name in ('WePlaySupersamplingInitialize','_WePlayNativeGraphicsConfigureEffects@4'):
                p.write_bytes(pe(name));self.assertTrue(b.graphics_export_present(p))
            p.write_bytes(b'notPE')
            with self.assertRaises(ValueError):b.graphics_export_present(p)
    def test_explicit_baseline_case_and_original_retained_handle_admission(self):
        with Case()as c:
            admission=c.admit();self.assertEqual(admission['mode'],b.BASELINE_MODE);self.assertTrue(admission['native_graphics_modules_absent'])
            c.native.identity.assert_any_call(123,987);self.assertFalse((c.case/b.ADMISSION).exists())
            c.native.bind_original.assert_called_once_with(c.child,987)
            case,value,child,parent=b._baseline_admitted(c.case,c.native)
            self.assertEqual(case,c.case);self.assertEqual(value['required_binaries'],c.critical);self.assertEqual(value['game_root'],str(c.root))
            self.assertNotIn('hook',value);self.assertEqual(len(value['baseline_required_paths']),5)
    def test_legacy_graphics_admission_does_not_accept_baseline_checkpoint(self):
        with Case()as c:
            c.admit()
            with self.assertRaises(ValueError):b._admitted(c.case,c.native)
    def test_mode_and_source_flags_are_mandatory_not_a_hook_absence_fallback(self):
        for change in ({'baseline_crash_watch_allowed':False},{'baseline_crash_watch_allowed':1},{'crash_watch_allowed':True},
                       {'baseline_mode':'graphics'},{'native_graphics_hook_requested':True},{'comparison_uv_requested':True},
                       {'material_effects_requested':['improved_terrain']}):
            with self.subTest(change=change),Case()as c:
                c.write('checkpoint.private.json',{**c.checkpoint,**change})
                with self.assertRaises(ValueError):c.admit()
                c.native.open.assert_not_called()
    def test_graphics_initialized_marker_is_not_turned_into_baseline_by_unload(self):
        with Case()as c:
            c.write('hook-initialized.private.json',{'initialized':True})
            with self.assertRaises(ValueError):c.admit()
    def test_all_required_loaded_module_paths_pins_and_renamed_exports_guarded(self):
        for mode in ('missing-critical','named-graphics','nativegraphics-name','renamed-graphics','unknownPE'):
            with self.subTest(mode=mode),Case()as c:
                modules=list(c.native.modules.return_value)
                if mode=='missing-critical':modules.pop()
                else:
                    p=c.root/('WePlaySupersamplingLab.dll'if mode=='named-graphics'else'NativeGraphics.dll'if mode=='nativegraphics-name'else'innocent.dll')
                    p.write_bytes(pe('WePlayNativeGraphicsRequestCapture')if mode=='renamed-graphics'else pe()if mode in ('named-graphics','nativegraphics-name')else b'bad')
                    modules.append(p)
                c.native.modules.return_value=modules
                with self.assertRaises((ValueError,RuntimeError)):c.admit()
    def test_journal_critical_binary_and_source_snapshot_tamper_refused(self):
        for kind in ('journal','binary','source'):
            with self.subTest(kind=kind),Case()as c:
                if kind=='journal':j=c.read(b.recover.JOURNAL);j['state']='restored';c.write(b.recover.JOURNAL,j)
                elif kind=='binary':(c.root/'Base/Bin/d3d9.dll').write_bytes(b'tampered')
                else:(c.case/'source/tools/graphics_lab/crash_watch.py').write_bytes(b'tampered')
                with self.assertRaises((ValueError,RuntimeError)):c.admit()
    def test_parent_creation_identity_and_pid_reuse_refused(self):
        for kind in ('parent','creation','reuse'):
            with self.subTest(kind=kind),Case()as c:
                if kind=='parent':c.child['parent']+=1
                elif kind=='creation':c.parent['creation_time']=1001
                else:
                    c.admit();c.child['creation_time']+=1
                    with self.assertRaises(RuntimeError):b._baseline_admitted(c.case,c.native)
                    continue
                with self.assertRaises(RuntimeError):c.admit()
    def test_runtime_module_absence_does_not_require_teardown_modules_stay_loaded(self):
        with Case()as c:
            verified=set(c.native.modules.return_value);c.native.modules.return_value=verified.copy()
            c.native.modules.return_value=list(verified)[:1]
            b._baseline_absence(c.native,123,verified)
            p=c.root/'renamed.dll';p.write_bytes(pe('WePlaySupersamplingInitialize'));c.native.modules.return_value.append(p)
            with self.assertRaises(RuntimeError):b._baseline_absence(c.native,123,verified)
    def test_closed_case_rejected_before_reopening_any_process(self):
        with Case()as c:
            c.admit();c.write('result.private.json',{'exit_code':0})
            with self.assertRaises(ValueError):b._baseline_admitted(c.case,c.native)
            c.native.open.assert_called_once() # parent at admission only

    def watch_fixture(self,directory):
        case=Path(directory)/'case';case.mkdir();root=Path(directory)/'game';root.mkdir()
        child=dict(pid=11,parent=12,path=str(root/'Settlers7R.exe'),sha256='b'*64,creation_time=100)
        parent=dict(pid=12,parent=13,path=str(root/'owner.exe'),sha256='c'*64,creation_time=90)
        cdb=Path(directory)/'cdb.exe';debugger=dict(pid=99,parent=os.getpid(),path=str(cdb),sha256=b.CDB_PINS['cdb.exe'],creation_time=110)
        rows=[dict(pid=11,base=0x400000,image_bytes=4096,reported_path=str(root/'Settlers7R.exe'),resolved_path=str(root/'Settlers7R.exe'))]
        admission=dict(child=child,parent=parent,game_root=str(root),required_binaries={},baseline_required_paths=[],modules=[],
            module_rows=rows,module_alias_proofs=[],baseline_alias_paths=[])
        native=Mock();native.state.return_value=dict(alive=True,exit_code=None)
        native.module_rows=rows;native.alias_proofs=[]
        native.identity.side_effect=lambda pid,handle:{11:child,12:parent,99:debugger}[pid]
        proc=Mock(pid=99,_handle=88,returncode=None);proc.poll.side_effect=lambda:proc.returncode
        def waited(timeout):proc.returncode=0;return 0
        proc.wait.side_effect=waited
        return case,cdb,admission,native,proc

    def test_baseline_watch_timeout_detaches_only_pinned_debugger(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'work')as directory:
            case,cdb,admission,native,proc=self.watch_fixture(directory);output=Path(directory)/'watch'
            with patch.object(b,'_baseline_native',return_value=native),patch.object(b,'_baseline_admitted',return_value=(case,admission,11,12)),\
                 patch.object(b,'_baseline_case'),patch.object(b,'_baseline_modules'),patch.object(b,'verify_cdb',return_value=cdb),\
                 patch('tools.graphics_lab.load_hook.file_lease',side_effect=lambda p:nullcontext()),\
                 patch.object(b.subprocess,'Popen',return_value=proc)as launch,patch.object(b.time,'monotonic',side_effect=[0,301]):
                result=b.watch(case,output,baseline=True)
            self.assertEqual(result['baseline_mode'],b.BASELINE_MODE);self.assertFalse(result['game_terminated_by_watcher'])
            self.assertEqual(result['stop_reason'],'timeout');proc.stdin.write.assert_called_once_with(b'qd\n');proc.terminate.assert_not_called()
            args,kwargs=launch.call_args;self.assertEqual(args[0][-2:],['-p','11']);self.assertIn('-pd',args[0]);self.assertFalse(kwargs['shell'])
            native.close.assert_any_call(11);native.close.assert_any_call(12)

    def test_baseline_alias_file_is_leased_and_reproved_before_debugger_spawn(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'work')as directory:
            case,cdb,admission,native,proc=self.watch_fixture(directory);output=Path(directory)/'watch'
            alias=Path(directory)/'driver.dll';alias.write_bytes(pe());admission['baseline_alias_paths']=[str(alias)]
            leases=[];proof_calls=[]
            def proof(provider,child):
                self.assertIn(alias,leases);proof_calls.append(True)
                return {'module_rows':admission['module_rows'],'module_alias_proofs':[]}
            def spawn(*args,**kwargs):self.assertTrue(proof_calls);return proc
            with patch.object(b,'_baseline_native',return_value=native),patch.object(b,'_baseline_admitted',return_value=(case,admission,11,12)),\
                 patch.object(b,'_baseline_case'),patch.object(b,'_baseline_modules'),patch.object(b,'_baseline_module_proof',side_effect=proof),\
                 patch.object(b,'verify_cdb',return_value=cdb),\
                 patch('tools.graphics_lab.load_hook.file_lease',side_effect=lambda path:(leases.append(path),nullcontext())[1]),\
                 patch.object(b.subprocess,'Popen',side_effect=spawn),patch.object(b.time,'monotonic',side_effect=[0,301]):
                result=b.watch(case,output,baseline=True)
            self.assertTrue(result['baseline_aliases_reproved_under_lease']);self.assertEqual(len(proof_calls),1)
            proc.terminate.assert_not_called()

    def test_changed_alias_mapping_under_lease_refuses_before_debugger_spawn(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'work')as directory:
            case,cdb,admission,native,proc=self.watch_fixture(directory);output=Path(directory)/'watch'
            with patch.object(b,'_baseline_native',return_value=native),patch.object(b,'_baseline_admitted',return_value=(case,admission,11,12)),\
                 patch.object(b,'_baseline_case'),patch.object(b,'_baseline_modules'),\
                 patch.object(b,'_baseline_module_proof',return_value={'module_rows':admission['module_rows'],'module_alias_proofs':[{'changed':True}]}),\
                 patch.object(b,'verify_cdb',return_value=cdb),\
                 patch('tools.graphics_lab.load_hook.file_lease',side_effect=lambda path:nullcontext()),\
                 patch.object(b.subprocess,'Popen')as spawn:
                with self.assertRaises(RuntimeError):b.watch(case,output,baseline=True)
            spawn.assert_not_called();native.close.assert_any_call(11);native.close.assert_any_call(12)

    def test_new_graphics_module_during_watch_refuses_without_stopping_game(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'work')as directory:
            case,cdb,admission,native,proc=self.watch_fixture(directory);output=Path(directory)/'watch'
            with patch.object(b,'_baseline_native',return_value=native),patch.object(b,'_baseline_admitted',return_value=(case,admission,11,12)),\
                 patch.object(b,'_baseline_case'),patch.object(b,'_baseline_modules'),patch.object(b,'verify_cdb',return_value=cdb),\
                 patch('tools.graphics_lab.load_hook.file_lease',side_effect=lambda p:nullcontext()),\
                 patch.object(b.subprocess,'Popen',return_value=proc),patch.object(b.time,'monotonic',side_effect=[0,0,1]),\
                 patch.object(b,'_baseline_absence',side_effect=RuntimeError('graphics appeared')):
                with self.assertRaises(RuntimeError):b.watch(case,output,baseline=True)
            proc.stdin.write.assert_called_once_with(b'qd\n');proc.terminate.assert_not_called()
            self.assertTrue((output/'watch-error.private.json').exists());native.close.assert_any_call(11)

    def test_public_baseline_gate_is_explicit_and_preserves_ordinary_graphics_route(self):
        from tools.graphics_lab import game
        with patch.object(game.os,'name','fixture-not-windows'),patch.object(game.platform,'load')as platform_load,\
                patch.object(game.platform,'launch')as launch,patch.object(b,'baseline_options',wraps=b.baseline_options)as options:
            with self.assertRaisesRegex(ValueError,'requires Windows'):game.run(Path('absent'),Path('absent'))
            options.assert_not_called()
            with self.assertRaisesRegex(ValueError,'Explicit baseline'):game.run(Path('absent'),Path('absent'),baseline_crash_watch=True)
            with self.assertRaisesRegex(ValueError,'requires Windows'):
                game.run(Path('absent'),Path('absent'),baseline_crash_watch=True,crash_watch=True)
        platform_load.assert_not_called();launch.assert_not_called()


if __name__=='__main__':
    unittest.main()
