"""Authored controller fixtures; never open or start a real process/debugger."""
from contextlib import nullcontext
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

from tools.graphics_lab import crash_watch as w,baseline_metric_profile as metric,game
from tools.graphics_lab.test_baseline_crash_watch import Case,BaselineTests,ROOT

PROOF={'format':'s7-baseline-metric-original-code-v1','profile':metric.PROFILE,'source_count':11,'fixture_only':True}


def enable(case):
    case.checkpoint['baseline_metric_profile_requested']=True
    case.checkpoint['sources']={name:w.digest(ROOT/name)for name in w.baseline_source_paths(metric=True)}
    for name in case.checkpoint['sources']:
        target=case.case/'source'/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,target)
    case.write('checkpoint.private.json',case.checkpoint)


class IntegrationTests(unittest.TestCase):
    def test_source11_owner_guard_runs_only_after_original_binding_and_is_repeated(self):
        with Case()as case:
            enable(case)
            def prepared(directory,native,child):
                self.assertEqual(directory,case.case);self.assertEqual(child,case.child)
                self.assertEqual(native.bind_original.call_args.args[0],child)
                return dict(PROOF)
            with patch.object(metric,'prepare_owner_profile',side_effect=prepared)as guard:
                value=case.admit();self.assertEqual(value['baseline_metric_profile'],PROOF)
                w._baseline_admitted(case.case,case.native)
            self.assertEqual(guard.call_count,2)
            self.assertEqual(len(w.baseline_source_paths()),10);self.assertEqual(len(w.baseline_source_paths(metric=True)),11)

    def test_default_source10_baseline_never_selects_metric_profile(self):
        with Case()as case,patch.object(metric,'prepare_owner_profile')as guard:
            value=case.admit();self.assertNotIn('baseline_metric_profile',value)
            w._baseline_admitted(case.case,case.native);guard.assert_not_called()

    def test_missing_source11_checkpoint_or_forged_old_admission_refused(self):
        with Case()as case,patch.object(metric,'prepare_owner_profile')as guard:
            case.checkpoint['baseline_metric_profile_requested']=True;case.write('checkpoint.private.json',case.checkpoint)
            with self.assertRaises(ValueError):case.admit()
            guard.assert_not_called()
        with Case()as case:
            value=case.admit();value['baseline_metric_profile']=PROOF;case.write(w.BASELINE_ADMISSION,value)
            with self.assertRaises(ValueError):w._baseline_admitted(case.case,case.native)

    def test_code_guard_failure_writes_no_owner_admission(self):
        with Case()as case:
            enable(case)
            with patch.object(metric,'prepare_owner_profile',side_effect=ValueError('fixed bytes mismatch')):
                with self.assertRaises(ValueError):case.admit()
            self.assertFalse((case.case/w.BASELINE_ADMISSION).exists())
            self.assertTrue(case.owned.poll()is None)

    def test_watch_reproves_metric_under_source11_leases_before_spawn(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'work')as directory:
            case,cdb,admission,native,proc=BaselineTests().watch_fixture(directory)
            admission['baseline_metric_profile']=dict(PROOF);leases=[];guarded=[]
            def prepared(*args):
                self.assertIn(ROOT/metric.SOURCE,leases);guarded.append(True);return dict(PROOF)
            def spawn(*args,**kwargs):self.assertTrue(guarded);return proc
            with patch.object(w,'_baseline_native',return_value=native),patch.object(w,'_baseline_admitted',return_value=(case,admission,11,12)),\
                 patch.object(w,'_baseline_case'),patch.object(w,'_baseline_modules'),patch.object(w,'verify_cdb',return_value=cdb),\
                 patch.object(metric,'prepare_owner_profile',side_effect=prepared),\
                 patch('tools.graphics_lab.load_hook.file_lease',side_effect=lambda path:(leases.append(path),nullcontext())[1]),\
                 patch.object(w.subprocess,'Popen',side_effect=spawn),patch.object(w.time,'monotonic',side_effect=[0,301]):
                result=w.watch(case,Path(directory)/'watch',baseline=True)
            self.assertTrue(result['baseline_metric_reproved_under_lease'])
            self.assertIn('bp20 (Settlers7R+0xa6db80)',(Path(directory)/'watch/watch.cmd').read_text(encoding='ascii'))
            proc.terminate.assert_not_called()

    def test_changed_metric_code_proof_under_lease_blocks_debugger(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'work')as directory:
            case,cdb,admission,native,proc=BaselineTests().watch_fixture(directory);admission['baseline_metric_profile']=dict(PROOF)
            with patch.object(w,'_baseline_native',return_value=native),patch.object(w,'_baseline_admitted',return_value=(case,admission,11,12)),\
                 patch.object(w,'_baseline_case'),patch.object(w,'_baseline_modules'),patch.object(w,'verify_cdb',return_value=cdb),\
                 patch.object(metric,'prepare_owner_profile',return_value={'changed':True}),\
                 patch('tools.graphics_lab.load_hook.file_lease',side_effect=lambda path:nullcontext()),\
                 patch.object(w.subprocess,'Popen')as spawn:
                with self.assertRaises(ValueError):w.watch(case,Path(directory)/'watch',baseline=True)
            spawn.assert_not_called()

    def test_owner_metric_flag_requires_explicit_baseline_and_cli_is_fixed(self):
        with patch.object(game.os,'name','fixture-not-windows'):
            for changes in ({'baseline_metric_profile':1},{'baseline_metric_profile':True},
                            {'baseline_metric_profile':True,'baseline_crash_watch':True}):
                with self.assertRaisesRegex(ValueError,'Metric'):game.run(Path('absent'),Path('absent'),**changes)
            with self.assertRaisesRegex(ValueError,'Windows'):
                game.run(Path('absent'),Path('absent'),baseline_metric_profile=True,baseline_crash_watch=True,crash_watch=True)
        with patch.object(game.sys,'argv',['game','--bundle','work/game','--output','work/case',
            '--baseline-crash-watch','--crash-watch','--baseline-metric-profile']),patch.object(game,'run')as run:
            game.main()
        self.assertTrue(run.call_args.kwargs['baseline_metric_profile'])


if __name__=='__main__':unittest.main()
