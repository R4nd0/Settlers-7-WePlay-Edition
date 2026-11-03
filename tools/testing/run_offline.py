"""Source-only fixture regressions, without running games or probing the PC.

Windows development helper. Each run uses a new ignored work directory. Real
subprocess, socket, registry and ctypes DLL entry points are blocked during tests.
This is not a security sandbox for arbitrary third-party test modules.
"""
import argparse
from contextlib import ExitStack,redirect_stdout
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import uuid

SUITES = ('test_update_recovery','test_standalone','test_installer','test_update_backend','test_network_content','test_standalone_preflight','test_game_directory',
          'test_lan_diagnostics','test_lan_launcher','test_auto_lan_launcher',
          'test_lan_autoconnect','test_graphics_launcher','test_desktop_backend',
          'test_internet_backend','test_ingame_directory','test_easytier_runtime')
FOCUSED = ('test_network_content','test_standalone_preflight','test_easytier_runtime')
CHILD_IMPORT_CHECKS = frozenset((
    'test_auto_lan_launcher.AutomaticLauncherTests.test_hub_entrypoint_boots_with_isolated_python_module_search',
    'test_graphics_launcher.GraphicsLauncherTests.test_installed_graphics_helpers_import_with_isolated_python'))


def cases(suite):
    for item in suite:
        if isinstance(item,unittest.TestSuite):yield from cases(item)
        else:yield item


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    group=parser.add_mutually_exclusive_group()
    group.add_argument('--focused',action='store_true')
    group.add_argument('--updates',action='store_true')
    args=parser.parse_args(argv)
    if os.name!='nt':parser.error('This fixture suite requires the Windows development runtime.')
    repo=Path(__file__).resolve().parents[2]
    output=repo/'work/offline-source-tests'/uuid.uuid4().hex
    output.mkdir(parents=True,exist_ok=False)
    tempfile.tempdir=str(output)
    os.environ.update(TEMP=str(output),TMP=str(output),S7_TEST_TEMP_ROOT=str(output))
    os.environ.pop('S7_INSTALLATION',None)
    sys.path.insert(0,str(repo/'tests'))
    # The allowlisted modules have definition-only imports. No integration
    # module or user-supplied test name is accepted here.
    suites=('test_update_recovery','test_standalone','test_installer','test_update_backend') if args.updates else (FOCUSED if args.focused else SUITES)
    suite=unittest.defaultTestLoader.loadTestsFromNames(suites)
    omitted=[];selected=unittest.TestSuite()
    for case in cases(suite):
        if case.id() in CHILD_IMPORT_CHECKS:omitted.append(case.id())
        else:selected.addTest(case)
    attempts=[]
    def deny(name):
        def forbidden(*args,**kwargs):
            attempts.append(name)
            raise AssertionError('Real operation forbidden in source-only tests: '+name)
        return forbidden
    guarded=('subprocess.Popen','socket.socket','ctypes.WinDLL','winreg.OpenKey',
             'winreg.CreateKey','winreg.CreateKeyEx','winreg.DeleteKey','winreg.SetValueEx')
    with ExitStack() as guards:
        for name in guarded:guards.enter_context(patch(name,side_effect=deny(name)))
        with (output/'tests.log').open('w',encoding='utf-8') as log,redirect_stdout(log):
            result=unittest.TextTestRunner(stream=log,verbosity=1).run(selected)
    summary={'passed':result.wasSuccessful() and not attempts,'tests':result.testsRun,
             'failures':len(result.failures),'errors':len(result.errors),'skipped':len(result.skipped),
             'blocked_entry_points':list(guarded),'forbidden_access_attempts':attempts,
             'child_import_checks_omitted':omitted,'live_verification':False}
    (output/'result.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'result':str(output/'result.json'),**summary}))
    return 0 if summary['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
