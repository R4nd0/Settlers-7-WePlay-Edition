"""Authored policy backends only; no process or graphics API access."""
from types import SimpleNamespace
import ctypes as C
import unittest
from unittest.mock import Mock
from . import on12_worker as p,game


class Backend:
    def __init__(self):
        self.worker_tid=234;self.active=True;self.closed=False;self.writes=[];self.fail=False;self.drift=False
        self.current=dict(process_mask=5,process_class=32,worker_mask=5,worker_group=0,render_mask=1,render_group=0,
                          worker_created=111,render_created=112,worker_priority=0,render_priority=2)
    def alive(self):return self.active
    def state(self):return dict(self.current)
    def set_priority(self,level):
        if self.fail:raise OSError('authored failure')
        self.writes.append(level);self.current['worker_priority']=level
        if self.drift and level==2:self.current['process_mask']=85
    def close(self):self.closed=True


class Tests(unittest.TestCase):
    def setUp(self):
        self.backend=Backend();self.factory=Mock(return_value=self.backend);self.policy=p.Policy(self.factory)
        self.owned=SimpleNamespace(pid=123,expected=p.UBI,native_initialized=True,kernel_isolation=7,owned=Mock())
        self.live=dict(format='s7-on12-water-live-v1',snapshot_done=True,on12_query_hr=0,failure=0,render_thread_id=345)

    def test_apply_matches_render_priority_and_keeps_all_budget_fields(self):
        report=self.policy.observe(self.owned,self.live)
        self.assertEqual(report['status'],'applied');self.assertEqual(self.backend.writes,[2])
        for key in p.INVARIANTS:self.assertEqual(report['before'][key],report['after'][key])
        self.assertLessEqual(report['after']['worker_priority'],report['after']['render_priority'])
        self.assertIsNone(self.policy.observe(self.owned,self.live));self.factory.assert_called_once_with(self.owned,345)
        self.policy.close();self.assertEqual(self.backend.writes,[2,0]);self.assertTrue(self.backend.closed)
        self.policy.close();self.assertEqual(self.backend.writes,[2,0])

    def test_only_identical_module_records_are_deduplicated(self):
        a=SimpleNamespace(base=C.c_void_p(0x400000),image_size=0x200000,path='C:/game.exe')
        same=SimpleNamespace(base=C.c_void_p(0x400000),image_size=0x200000,path='C:/GAME.EXE')
        other_base=SimpleNamespace(base=C.c_void_p(0x500000),image_size=0x200000,path='C:/game.exe')
        other_size=SimpleNamespace(base=C.c_void_p(0x400000),image_size=0x300000,path='C:/game.exe')
        self.assertEqual(len(p.unique_modules([a,same])),1)
        self.assertEqual(len(p.unique_modules([a,same,other_base,other_size])),3)

    def test_no_policy_before_real_on12_snapshot_and_healthy_native_identity(self):
        for changes in ({'snapshot_done':False},{'on12_query_hr':-1},{'on12_query_hr':False},{'failure':1},
                        {'render_thread_id':False},{'render_thread_id':0},{'format':'other'}):
            with self.subTest(changes=changes):self.assertIsNone(self.policy.observe(self.owned,{**self.live,**changes}))
        self.assertIsNone(self.policy.observe(self.owned,[]));self.factory.assert_not_called()

    def test_owner_and_kernel_guards_precede_thread_access(self):
        for field,value in (('kernel_isolation',1),('native_initialized',False),('expected','other')):
            old=getattr(self.owned,field);setattr(self.owned,field,value)
            with self.subTest(field=field),self.assertRaises(ValueError):self.policy.observe(self.owned,self.live)
            setattr(self.owned,field,old)
        self.factory.assert_not_called()

    def test_unsupported_runtime_and_failed_setter_do_not_change_original_priority(self):
        self.factory.side_effect=ValueError('Unqualified runtime identity')
        self.assertEqual(self.policy.observe(self.owned,self.live)['status'],'unavailable');self.assertEqual(self.backend.writes,[])
        self.policy=p.Policy(lambda *_:self.backend);self.backend.fail=True
        self.assertEqual(self.policy.observe(self.owned,self.live)['status'],'unavailable');self.assertEqual(self.backend.writes,[])
        self.assertEqual(self.backend.current['worker_priority'],0);self.assertTrue(self.backend.closed)

    def test_budget_drift_rolls_back_priority_without_changing_affinity(self):
        self.backend.drift=True
        report=self.policy.observe(self.owned,self.live)
        self.assertEqual(report['status'],'unavailable');self.assertEqual(self.backend.writes,[2,0])
        self.assertEqual(self.backend.current['process_mask'],85);self.assertTrue(self.backend.closed)

    def test_newer_priority_and_exited_thread_are_preserved_on_close(self):
        self.policy.observe(self.owned,self.live);self.backend.current['worker_priority']=1
        self.assertEqual(self.policy.close()['restoration'],'newer_priority_preserved');self.assertEqual(self.backend.writes,[2])
        self.backend=Backend();self.policy=p.Policy(lambda *_:self.backend);self.policy.observe(self.owned,self.live);self.backend.active=False
        self.assertEqual(self.policy.close()['restoration'],'thread_exited');self.assertEqual(self.backend.writes,[2])

    def test_never_promotes_beyond_supported_render_priority_or_lowers_a_worker(self):
        for render in (-15,-2,-1,0,1,2,15,2147483647,False):
            for worker in (-15,-2,-1,0,1,2,15,2147483647,True):
                result=p.target_priority(render,worker)
                if result is not None:self.assertTrue(type(result)is int and worker<result<=render<=2)
        self.backend.current['worker_priority']=2
        self.assertEqual(self.policy.observe(self.owned,self.live)['status'],'unchanged');self.assertEqual(self.backend.writes,[])

    def test_entrypoint_rejects_policy_mixed_with_other_renderers_or_benchmarks(self):
        for kwargs in ({},{'water_worker_priority':1},{'water_worker_priority':True},
                       {'water_worker_priority':True,'water_benchmark':True}):
            if not kwargs:continue
            with self.subTest(kwargs=kwargs),self.assertRaises(ValueError):game.run('never-read','never-created',**kwargs)


if __name__=='__main__':unittest.main()
