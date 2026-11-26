import importlib.util
from pathlib import Path
import unittest
from unittest.mock import Mock

FILE=Path(__file__).with_name('baseline_modules.py')
spec=importlib.util.spec_from_file_location('private_baseline_module_provider',FILE)
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
REPORTED=Path(r'C:\Windows\SysWOW64\DriverStore\FileRepository\amd.inf\amdvlk32.dll')
CANDIDATE=Path(r'C:\Windows\System32\DriverStore\FileRepository\amd.inf\amdvlk32.dll')
NT=r'\Device\HarddiskVolume4\Windows\System32\DriverStore\FileRepository\amd.inf\amdvlk32.dll'
ID=dict(pid=123,parent=456,path=r'C:\owned\Settlers7R.exe',sha256='a'*64,creation_time=789)

class Fake(m.BaselineNative):
    def __init__(self):
        self.expected=None;self.retained=None;self.alias_proofs=[];self.module_rows=[];self.closed=[];self.opens=[]
        self.rows=[dict(pid=123,base=0x10000000,image_bytes=4096,reported_path=str(REPORTED))]
        self.kinds={REPORTED:'missing',CANDIDATE:'file'};self.current=dict(ID);self.mapped=NT;self.final=NT
        self.mapped_error=None;self.final_error=None;self.mapped_calls=[]
    def _identity(self,pid,handle):return dict(self.current)
    def _open_query(self,pid):self.opens.append((pid,m.MODULE_READ_ACCESS));return 901
    def _close(self,handle):self.closed.append(handle)
    def _plain(self,path):return Path(path)
    def _kind(self,path):return self.kinds.get(path,'missing')
    def _windows(self):return Path(r'C:\Windows')
    def _mapped(self,handle,base):
        self.mapped_calls.append((handle,base))
        if self.mapped_error:raise self.mapped_error
        return self.mapped
    def _candidate_open(self,path):return 902
    def _final(self,handle):
        if self.final_error:raise self.final_error
        return self.final
    def _candidate_info(self,handle):return dict(volume_serial=3,file_index=44,bytes=8192)
    def _hash(self,path):return 'b'*64
    def _rows(self,pid):return self.rows

class Tests(unittest.TestCase):
    def bound(self):
        provider=Fake();provider.bind_original(ID,900);return provider
    def test_only_missing_driverstore_candidate_is_generated(self):
        self.assertEqual(m.driverstore_candidate(REPORTED,Path(r'C:\Windows')),CANDIDATE)
        for value in (r'C:\Game\DriverStore\a.dll',r'C:\Windows\System32\DriverStore\a.dll',
                      r'C:\Windows2\SysWOW64\DriverStore\a.dll',r'C:\Windows\SysWOW64\a.dll',
                      r'C:\Windows\SysWOW64\DriverStore\..\a.dll',r'C:\Windows\SysWOW64\DriverStore\a.exe'):
            with self.subTest(value=value),self.assertRaises(ValueError):m.driverstore_candidate(value,Path(r'C:\Windows'))
    def test_exact_nt_mapping_proof_returns_physical_path_and_records_identity(self):
        p=self.bound();self.assertEqual(p.modules(123),[CANDIDATE]);self.assertEqual(p.mapped_calls,[(901,0x10000000)])
        proof=p.alias_proofs[0];self.assertEqual(proof['original_identity'],ID);self.assertEqual(proof['mapped_nt'],proof['final_nt'])
        self.assertEqual(proof['file_sha256'],'b'*64);self.assertEqual(proof['file_identity']['file_index'],44)
        self.assertEqual(p.closed,[902,901]);self.assertEqual(p.opens,[(123,0x100410)])
    def test_existing_reported_file_never_substitutes(self):
        p=self.bound();p.kinds[REPORTED]='file';self.assertEqual(p.modules(123),[REPORTED]);self.assertFalse(p.alias_proofs);self.assertFalse(p.mapped_calls)
    def test_wrong_process_or_reused_original_refused_before_vmread_open(self):
        p=self.bound()
        with self.assertRaises(RuntimeError):p.modules(124)
        self.assertFalse(p.opens);p.current['creation_time']+=1
        with self.assertRaises(RuntimeError):p.modules(123)
        self.assertFalse(p.opens)
    def test_binding_cannot_retarget_and_unbound_modules_refuse(self):
        p=Fake()
        with self.assertRaises(RuntimeError):p.modules(123)
        p.bind_original(ID,900)
        with self.assertRaises(RuntimeError):p.bind_original({**ID,'pid':124},999)
    def test_mismatched_nt_volume_leaf_or_dos_path_refused_and_handles_closed(self):
        for value in (NT.replace('Volume4','Volume5'),NT.replace('amdvlk32','other'),r'C:\Windows\System32\DriverStore\a.dll','',NT+'\0'):
            with self.subTest(value=value):
                p=self.bound();p.final=value
                with self.assertRaises(RuntimeError):p.modules(123)
                self.assertEqual(p.closed,[902,901]);self.assertFalse(p.alias_proofs)
    def test_missing_or_directory_candidate_never_accepted(self):
        for kind in ('missing','other'):
            p=self.bound();p.kinds[CANDIDATE]=kind
            with self.assertRaises(ValueError):p.modules(123)
            self.assertEqual(p.closed,[901]);self.assertFalse(p.mapped_calls)
    def test_query_errors_never_fall_back_to_filename_substitution(self):
        for field in ('mapped_error','final_error'):
            p=self.bound();setattr(p,field,OSError('denied'))
            with self.assertRaises(OSError):p.modules(123)
            self.assertEqual(p.closed,[902,901]);self.assertFalse(p.alias_proofs)
    def test_original_identity_is_rechecked_after_mapping(self):
        p=self.bound();original=p._final
        def changed(handle):p.current['creation_time']+=1;return original(handle)
        p._final=changed
        with self.assertRaises(RuntimeError):p.modules(123)
        self.assertEqual(p.closed,[902,901]);self.assertFalse(p.alias_proofs)
    def test_duplicate_bases_invalid_sizes_and_pid_are_refused(self):
        for change in ({'pid':124},{'base':0},{'base':True},{'image_bytes':0},{'reported_path':'x'*260}):
            p=self.bound();p.rows=[{**p.rows[0],**change}]
            with self.subTest(change=change),self.assertRaises(RuntimeError):p.modules(123)
        p=self.bound();p.rows*=2
        with self.assertRaises(RuntimeError):p.modules(123)
    def test_duplicate_exe_paths_preserved_for_original_caller_rule(self):
        p=self.bound();exe=Path(ID['path']);p.kinds[exe]='file';p.rows=[dict(pid=123,base=0x400000,image_bytes=8192,reported_path=str(exe)),dict(pid=123,base=0x70000000,image_bytes=8192,reported_path=str(exe))]
        self.assertEqual(p.modules(123),[exe,exe]);self.assertEqual(len(p.module_rows),2)
        p.rows[1]['base']=p.rows[0]['base'];self.assertEqual(p.modules(123),[exe,exe])
        p.rows.append(dict(p.rows[0]))
        with self.assertRaises(RuntimeError):p.modules(123)
    def test_readonly_masks_and_actual_open_arguments(self):
        self.assertFalse(m.MODULE_READ_ACCESS&(1|8|32));p=Fake();p.k=Mock();p.api=Mock();p.api.check.side_effect=lambda value:value
        p.k.OpenProcess.return_value=901;self.assertEqual(m.BaselineNative._open_query(p,123),901)
        p.k.OpenProcess.assert_called_once_with(0x100410,False,123)
        p.k.CreateFileW.return_value=902;self.assertEqual(m.BaselineNative._candidate_open(p,CANDIDATE),902)
        args=p.k.CreateFileW.call_args.args;self.assertEqual(args[1:3],(0x80000000,1));self.assertEqual(args[4:6],(3,0x200000))

if __name__=='__main__':unittest.main()
