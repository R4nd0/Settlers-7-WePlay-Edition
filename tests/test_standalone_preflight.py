"""CLI exit categories using synthetic paths; never binds a game or launches UI."""
from contextlib import redirect_stdout
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock,patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/installer'))
import standalone


class PreflightExitTests(unittest.TestCase):
    def call(self,error=None,check=True):
        with tempfile.TemporaryDirectory() as name:
            argv=['standalone.py','--root',name]+(['--check-only'] if check else [])
            desktop=Mock();desktop.app=Path(name)/'App';desktop.game.return_value=Path(name)/'owned'
            output=io.StringIO()
            with patch.object(sys,'argv',argv),patch.object(standalone,'bind',return_value=desktop,side_effect=error), \
                    patch.object(standalone,'modules') as modules,patch.object(standalone.subprocess,'call') as launch, \
                    redirect_stdout(output):
                code=standalone.main()
            launch.assert_not_called()
            return code,output.getvalue(),desktop,modules

    def test_parallel_preflight_has_distinct_busy_exit_and_retains_readable_detail(self):
        code,text,*_=self.call(RuntimeError('operation_busy'))
        self.assertEqual(code,standalone.PREFLIGHT_BUSY)
        self.assertIn('operation_busy',text)

    def test_integrity_failure_is_not_reported_as_busy(self):
        code,text,*_=self.call(ValueError('corrupt fixture'))
        self.assertEqual(code,2)
        self.assertNotEqual(code,standalone.PREFLIGHT_BUSY)

    def test_general_launcher_failure_keeps_existing_exit_contract(self):
        code,*_=self.call(RuntimeError('operation_busy'),check=False)
        self.assertEqual(code,2)

    def test_success_requires_verification_without_starting_launcher(self):
        code,text,desktop,modules=self.call()
        self.assertEqual(code,0)
        desktop.package_id.assert_called_once_with(full=True)
        modules.return_value.content.verify.assert_called_once_with(desktop.game.return_value)
        self.assertIn('gueltig',text)


if __name__=='__main__':unittest.main()
