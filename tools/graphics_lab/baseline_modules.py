"""Private proposal: prove the observed missing WOW64 DriverStore aliases.

No PID selector or automatic admission. Bind the exact retained original handle
after the owner's normal admission checks. Graphics continues using Native.
"""
from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
from pathlib import Path,PureWindowsPath
import stat

from tools.graphics_lab.crash_watch import Native,_match,digest,recover

MODULE_READ_ACCESS=0x100410  # SYNCHRONIZE|QUERY_INFORMATION|VM_READ, never WRITE/TERMINATE
MAX_MODULES=512
BUFFER_CHARS=32768


def driverstore_candidate(reported,windows):
    reported,windows=PureWindowsPath(str(reported)),PureWindowsPath(str(windows))
    prefix=windows/'SysWOW64'/'DriverStore'
    try:relative=reported.relative_to(prefix)
    except ValueError:raise ValueError('Not the narrow Windows SysWOW64 DriverStore alias.')
    if (not relative.parts or reported.suffix.casefold()!='.dll'
            or any(part in ('.','..')or ':'in part for part in relative.parts)):
        raise ValueError('Invalid DriverStore DLL alias shape.')
    return Path(str(windows/'System32'/'DriverStore'/relative))


def same_nt_file(mapped,final):
    if (not isinstance(mapped,str)or not isinstance(final,str)or not mapped or not final
            or '\0'in mapped or '\0'in final or len(mapped)>=BUFFER_CHARS or len(final)>=BUFFER_CHARS
            or not mapped.casefold().startswith('\\device\\')or not final.casefold().startswith('\\device\\')):
        return False
    return mapped.casefold()==final.casefold()


class MappingCoverageLost(OSError):
    """One unverified mapped-file query; never permission to skip a module."""
    def __init__(self,record):
        self.record=record
        super().__init__('Baseline mapped-file coverage was lost.')


class BaselineNative(Native):
    def __init__(self):
        super().__init__()
        self.expected=None;self.retained=None;self.alias_proofs=[];self.module_rows=[]
        for name,args,result in (
            ('K32GetMappedFileNameW',[W.HANDLE,C.c_void_p,W.LPWSTR,W.DWORD],W.DWORD),
            ('GetFinalPathNameByHandleW',[W.HANDLE,W.LPWSTR,W.DWORD,W.DWORD],W.DWORD),
            ('GetSystemWindowsDirectoryW',[W.LPWSTR,W.UINT],W.UINT),
            ('CreateFileW',[W.LPCWSTR,W.DWORD,W.DWORD,C.c_void_p,W.DWORD,W.DWORD,W.HANDLE],W.HANDLE),
        ):
            function=getattr(self.k,name);function.argtypes=args;function.restype=result

    def _identity(self,pid,handle):return super().identity(pid,handle)

    def bind_original(self,expected,retained):
        _match(expected,expected)
        if self.expected is not None and self.expected!=expected:
            raise RuntimeError('Module provider cannot be retargeted to another child.')
        _match(expected,self._identity(expected['pid'],retained))
        self.expected=dict(expected);self.retained=retained

    def _check_original(self,pid,query=None):
        if not self.expected or type(pid)is not int or pid!=self.expected['pid']:
            raise RuntimeError('Module reads require the exact bound original PID/handle.')
        _match(self.expected,self._identity(pid,self.retained))
        if query is not None:_match(self.expected,self._identity(pid,query))

    def _open_query(self,pid):return self.api.check(self.k.OpenProcess(MODULE_READ_ACCESS,False,pid))
    def _close(self,handle):self.close(handle)
    def _plain(self,path):return recover.plain(path)
    def _kind(self,path):
        try:mode=path.stat().st_mode
        except FileNotFoundError:return 'missing'
        return 'file'if stat.S_ISREG(mode)else'other'
    def _windows(self):
        buffer=C.create_unicode_buffer(BUFFER_CHARS);count=self.k.GetSystemWindowsDirectoryW(buffer,len(buffer))
        if not count or count>=len(buffer)or len(buffer.value)!=count:raise OSError('Cannot obtain bounded actual Windows directory.')
        return self._plain(buffer.value)
    def _mapped(self,handle,base):
        buffer=C.create_unicode_buffer(BUFFER_CHARS);count=self.k.K32GetMappedFileNameW(handle,C.c_void_p(base),buffer,len(buffer))
        if not count or count>=len(buffer)or len(buffer.value)!=count:raise OSError('Missing/truncated original mapped module filename.')
        return buffer.value
    def _candidate_open(self,path):
        # Existing regular file only; read/share-read denies changes during proof.
        handle=self.k.CreateFileW(str(path),0x80000000,1,None,3,0x200000,None)
        if handle in (None,C.c_void_p(-1).value):raise OSError('Cannot open alias candidate read-only.')
        return handle
    def _final(self,handle):
        buffer=C.create_unicode_buffer(BUFFER_CHARS);count=self.k.GetFinalPathNameByHandleW(handle,buffer,len(buffer),2)
        if not count or count>=len(buffer)or len(buffer.value)!=count:raise OSError('Missing/truncated final NT candidate filename.')
        return buffer.value
    def _candidate_info(self,handle):
        class Info(C.Structure):
            _fields_=[('attributes',W.DWORD),('created',W.FILETIME),('accessed',W.FILETIME),('written',W.FILETIME),
                      ('volume',W.DWORD),('size_high',W.DWORD),('size_low',W.DWORD),('links',W.DWORD),('index_high',W.DWORD),('index_low',W.DWORD)]
        info=Info();function=self.k.GetFileInformationByHandle;function.argtypes=[W.HANDLE,C.POINTER(Info)];function.restype=W.BOOL
        self.api.check(function(handle,C.byref(info)))
        if info.attributes&(0x400|0x10):raise ValueError('Alias candidate is a reparse point/directory.')
        return dict(volume_serial=info.volume,file_index=info.index_low|(info.index_high<<32),bytes=info.size_low|(info.size_high<<32))
    def _hash(self,path):return digest(path)

    def _rows(self,pid):
        class Entry(C.Structure):
            _fields_=[('size',W.DWORD),('id',W.DWORD),('pid',W.DWORD),('global_usage',W.DWORD),('process_usage',W.DWORD),
                      ('base',C.c_void_p),('image_size',W.DWORD),('module',W.HMODULE),('name',W.WCHAR*256),('path',W.WCHAR*260)]
        snapshot=self.k.CreateToolhelp32Snapshot(0x18,pid)
        if snapshot==C.c_void_p(-1).value:self.api.check(False)
        result=[]
        try:
            entry=Entry();entry.size=C.sizeof(entry);found=self.k.Module32FirstW(snapshot,C.byref(entry))
            while found:
                if len(result)>=MAX_MODULES:raise RuntimeError('Original module snapshot exceeds bound.')
                result.append(dict(pid=entry.pid,base=int(entry.base or 0),image_bytes=entry.image_size,reported_path=entry.path))
                found=self.k.Module32NextW(snapshot,C.byref(entry))
            if C.get_last_error()!=18:self.api.check(False)
        finally:self._close(snapshot)
        return result

    def modules(self,pid):
        self.alias_proofs=[];self.module_rows=[]
        self._check_original(pid)  # Reject dead/reused/foreign original before opening VM_READ.
        query=self._open_query(pid)
        proofs=[];records=[];paths=[]
        try:
            self._check_original(pid,query);rows=self._rows(pid)
            if not rows or len(rows)>MAX_MODULES:raise RuntimeError('Empty/excessive original module rows.')
            bases={}
            for row in rows:
                if (not isinstance(row,dict)or set(row)!={'pid','base','image_bytes','reported_path'}or row['pid']!=pid
                        or type(row['base'])is not int or not 0<row['base']<=0xffffffffffffffff
                        or type(row['image_bytes'])is not int or not 0<row['image_bytes']<=0xffffffff
                        or not isinstance(row['reported_path'],str)or not row['reported_path']or len(row['reported_path'])>=260):
                    raise RuntimeError('Invalid/ambiguous module base/path snapshot.')
                reported=self._plain(row['reported_path'])
                if row['base']in bases:
                    previous,count=bases[row['base']]
                    if (reported!=self._plain(self.expected['path'])or previous!=row or count!=1):
                        raise RuntimeError('Duplicate module base is not the two identical original EXE views.')
                    bases[row['base']]=(previous,2)
                else:bases[row['base']]=(dict(row),1)
                kind=self._kind(reported)
                if kind=='file':resolved=reported
                elif kind=='missing':
                    candidate=self._plain(driverstore_candidate(reported,self._windows()))
                    if self._kind(candidate)!='file':raise ValueError('The one permitted DriverStore counterpart is absent/not regular.')
                    self._check_original(pid,query);file=self._candidate_open(candidate)
                    try:
                        try:mapped=self._mapped(query,row['base'])
                        except OSError as failure:
                            raise MappingCoverageLost(dict(original_identity=dict(self.expected),
                                module_row=dict(row),candidate_path=str(candidate),
                                phase='K32GetMappedFileNameW',message=str(failure)[:512],
                                no_valid_mapping_for_failed_row=True))from failure
                        final=self._final(file)
                        if not same_nt_file(mapped,final):raise RuntimeError('Module mapping does not prove this DriverStore counterpart.')
                        info=self._candidate_info(file);pin=self._hash(candidate)
                        self._check_original(pid,query)
                        proofs.append(dict(original_identity=dict(self.expected),module_base=hex(row['base']),image_bytes=row['image_bytes'],
                            reported_path=str(reported),resolved_path=str(candidate),mapped_nt=mapped,final_nt=final,
                            file_sha256=pin,file_identity=info,method='original-map-equals-opened-file-VOLUME_NAME_NT',read_only=True))
                    finally:self._close(file)
                    resolved=candidate
                else:raise ValueError('Reported module is neither a regular file nor the permitted missing alias.')
                records.append({**row,'resolved_path':str(resolved)});paths.append(resolved)
            self._check_original(pid,query)
            self.alias_proofs=proofs;self.module_rows=records
            return paths  # Keep duplicate EXE rows; caller retains its exact two-map rule.
        finally:self._close(query)
