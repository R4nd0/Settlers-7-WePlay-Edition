"""Match the proven On12 batch worker to its render thread within the same budget."""
import ctypes as C
from ctypes import wintypes as W
import hashlib
from pathlib import Path
from .capture_bootstrap import UBI

RUNTIME_SHA256='da332f721a59dc73f8fdb32acd6755dd0ddae726f78342d8d73c32b14abb21e5'
WORKER_RVA=0x1436e0
RENDER_RVA=0x622fb7
INVARIANTS=('process_mask','process_class','worker_mask','worker_group','render_mask','render_group','worker_created','render_created')


def unique_modules(rows):
    # Combined native/WOW64 snapshots can repeat the same main image.
    return list({(C.cast(m.base,C.c_void_p).value,m.image_size,str(Path(m.path)).casefold()):m for m in rows}.values())


def target_priority(render,worker):
    if type(render)is not int or type(worker)is not int or render not in (0,1,2) or not -2<=worker<=2:
        return None
    return render if worker<render else None


class Native:
    def __init__(self,owned,render_tid):
        from tools.installer import observe_game as telemetry
        self.api=telemetry.Windows();self.pid=owned.pid;self.handles=[]
        k=self.api.k
        class Affinity(C.Structure):
            _fields_=[('mask',C.c_size_t),('group',W.WORD),('reserved',W.WORD*3)]
        self.Affinity=Affinity
        for name,args,result in (
                ('GetThreadGroupAffinity',[W.HANDLE,C.POINTER(Affinity)],W.BOOL),
                ('GetProcessAffinityMask',[W.HANDLE,C.POINTER(C.c_size_t),C.POINTER(C.c_size_t)],W.BOOL),
                ('GetPriorityClass',[W.HANDLE],W.DWORD),('GetThreadPriority',[W.HANDLE],C.c_int),
                ('SetThreadPriority',[W.HANDLE,C.c_int],W.BOOL),
                ('GetExitCodeThread',[W.HANDLE,C.POINTER(W.DWORD)],W.BOOL)):
            fn=getattr(k,name);fn.argtypes=args;fn.restype=result
        native=C.WinDLL('ntdll');self.query=native.NtQueryInformationThread
        self.query.argtypes=[W.HANDLE,W.ULONG,C.c_void_p,W.ULONG,C.POINTER(W.ULONG)];self.query.restype=C.c_long
        try:
            self.process=self.keep(k.OpenProcess(0x1400,False,self.pid))
            if self.api.path(self.process)!=owned.executable:raise ValueError('Owned image changed')
            modules=unique_modules(self.api.snapshot(0x18,self.pid,telemetry.Module,'Module32FirstW','Module32NextW'))
            runtime=[m for m in modules if m.name.lower()=='d3d9.dll'and m.image_size>0x100000]
            images=[m for m in modules if Path(m.path)==owned.executable]
            if len(runtime)!=1 or len(images)!=1:raise ValueError('Ambiguous runtime')
            # Pin the actual loaded image; do not infer its path through WOW64
            # directory APIs or accept a module name as an identity proof.
            runtime_path=Path(runtime[0].path)
            if not runtime_path.is_absolute() or hashlib.sha256(runtime_path.read_bytes()).hexdigest()!=RUNTIME_SHA256:
                raise ValueError('Unqualified runtime identity')
            base=C.cast(runtime[0].base,C.c_void_p).value
            self.render=self.keep(k.OpenThread(0x40,False,render_tid));self.render_tid=render_tid
            if k.GetProcessIdOfThread(self.render)!=self.pid or self.start(self.render)!=C.cast(images[0].base,C.c_void_p).value+RENDER_RVA:
                raise ValueError('Unqualified render thread')
            self.render_created=self.api.times(self.render,thread=True)[0]
            candidates=[]
            for row in self.api.snapshot(4,0,telemetry.Thread,'Thread32First','Thread32Next'):
                if row.owner!=self.pid:continue
                h=k.OpenThread(0x40,False,row.tid)
                if not h:continue
                try:
                    if k.GetProcessIdOfThread(h)==self.pid and self.start(h)==base+WORKER_RVA:candidates.append(row.tid)
                finally:k.CloseHandle(h)
            if len(candidates)!=1:raise ValueError('Ambiguous batch worker')
            self.worker_tid=candidates[0];self.worker=self.keep(k.OpenThread(0x60,False,self.worker_tid))
            if k.GetProcessIdOfThread(self.worker)!=self.pid or self.start(self.worker)!=base+WORKER_RVA:
                raise ValueError('Worker identity changed')
            self.worker_created=self.api.times(self.worker,thread=True)[0]
        except BaseException:
            self.close();raise

    def keep(self,handle):
        if not handle:raise OSError('Thread policy handle unavailable')
        self.handles.append(handle);return handle

    def start(self,handle):
        address=C.c_void_p();size=W.ULONG()
        if self.query(handle,9,C.byref(address),C.sizeof(address),C.byref(size))<0:return None
        return address.value

    def alive(self):
        code=W.DWORD()
        return bool(self.api.k.GetExitCodeThread(self.worker,C.byref(code)))and code.value==259

    def state(self):
        k=self.api.k
        if (not self.alive() or k.GetProcessIdOfThread(self.worker)!=self.pid
                or self.api.times(self.worker,thread=True)[0]!=self.worker_created
                or self.api.times(self.render,thread=True)[0]!=self.render_created):
            raise ValueError('Original threads no longer active')
        mask=C.c_size_t();system=C.c_size_t()
        if not k.GetProcessAffinityMask(self.process,C.byref(mask),C.byref(system))or not mask.value:
            raise OSError('CPU budget query failed')
        result=dict(process_mask=mask.value,process_class=k.GetPriorityClass(self.process),
                    worker_created=self.worker_created,render_created=self.render_created,
                    worker_priority=k.GetThreadPriority(self.worker),render_priority=k.GetThreadPriority(self.render))
        for name,h in (('worker',self.worker),('render',self.render)):
            affinity=self.Affinity()
            if not k.GetThreadGroupAffinity(h,C.byref(affinity))or not affinity.mask or affinity.group!=0 or affinity.mask&~mask.value:
                raise ValueError('Thread CPU budget is not qualified')
            result[name+'_mask']=affinity.mask;result[name+'_group']=affinity.group
        if not result['process_class']:raise OSError('Process class query failed')
        return result

    def set_priority(self,level):
        if not self.api.k.SetThreadPriority(self.worker,level):raise OSError('Worker priority update failed')

    def close(self):
        for handle in reversed(self.handles):self.api.k.CloseHandle(handle)
        self.handles=[]


class Policy:
    def __init__(self,factory=Native):
        self.factory=factory;self.backend=None;self.before=None;self.target=None;self.done=False
        self.report=dict(format='s7-on12-worker-priority-v1',status='waiting')

    def observe(self,owned,live):
        if self.done:return None
        if not isinstance(live,dict):return None
        if (live.get('format')!='s7-on12-water-live-v1'or live.get('snapshot_done')is not True
                or type(live.get('on12_query_hr'))is not int or live['on12_query_hr']!=0
                or type(live.get('failure'))is not int or live['failure']!=0):return None
        tid=live.get('render_thread_id')
        if type(tid)is not int or not 0<tid<=0xffffffff:return None
        owned.owned()
        if owned.expected!=UBI or owned.native_initialized is not True or owned.kernel_isolation!=7:
            raise ValueError('Worker policy requires its owned isolated original On12 child')
        self.done=True
        try:
            self.backend=self.factory(owned,tid);self.before=dict(self.backend.state())
            self.target=target_priority(self.before['render_priority'],self.before['worker_priority'])
            self.report.update(pid=owned.pid,render_tid=tid,worker_tid=self.backend.worker_tid,before=self.before,
                               runtime_sha256=RUNTIME_SHA256,worker_start_rva=WORKER_RVA)
            if self.target is None:
                self.report['status']='unchanged';self.backend.close();self.backend=None;return self.report
            self.backend.set_priority(self.target);after=dict(self.backend.state())
            if (any(after[k]!=self.before[k] for k in INVARIANTS)
                    or after['worker_priority']!=self.target or self.target>after['render_priority']):
                raise ValueError('Priority or unchanged CPU budget did not verify')
            self.report.update(status='applied',after=after)
        except (OSError,ValueError,RuntimeError) as error:
            self.report.update(status='unavailable',error_type=type(error).__name__,reason=str(error))
            self.close()
        return self.report

    def close(self):
        if self.backend is None:return self.report
        try:
            if self.before is not None and self.target is not None and self.backend.alive():
                current=self.backend.state()
                if current['worker_priority']==self.target:
                    self.backend.set_priority(self.before['worker_priority'])
                    if self.backend.state()['worker_priority']!=self.before['worker_priority']:
                        raise OSError('Original worker priority did not restore')
                    self.report['restoration']='restored'
                else:self.report['restoration']='newer_priority_preserved'
            else:self.report['restoration']='thread_exited'
        finally:self.backend.close();self.backend=None
        return self.report
