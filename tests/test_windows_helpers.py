"""Read-only Windows API/validation tests; never change machine security policy."""
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import winreg
from unittest.mock import Mock,patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/installer'))
import windows_system
from lan_interfaces import parse_interfaces

HELPER=os.environ.get('S7_WINDOWS_HELPER','')

def policies():
    result=[]
    for hive in (winreg.HKEY_CURRENT_USER,winreg.HKEY_LOCAL_MACHINE):
        for key,names in [('SOFTWARE\\Microsoft\\PowerShell\\1\\ShellIds\\Microsoft.PowerShell',('ExecutionPolicy',)),
                          ('SOFTWARE\\Policies\\Microsoft\\Windows\\PowerShell',('ExecutionPolicy','EnableScripts'))]:
            for name in names:
                try:
                    with winreg.OpenKey(hive,key) as handle:value=winreg.QueryValueEx(handle,name)
                except FileNotFoundError:value=None
                result.append(value)
    return result

@unittest.skipUnless(os.name=='nt' and HELPER,'opt-in compiled Windows API helper')
class WindowsHelperTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(dir=ROOT/'work/test-temp',prefix='Windows helper paths ')
        self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        for name in ('Base/Bin/Settlers7R.exe','Runtime/dotnet/dotnet.exe','Runtime/python/python.exe','Runtime/easytier/easytier-core.exe'):
            path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'MZ benign unexecuted fixture')
        (self.root/'compatibility.json').write_text('{"format":"s7-content-v1"}',encoding='utf-8')
        self.environment=dict(os.environ,PSExecutionPolicyPreference='Restricted')
        self.before=policies();self.addCleanup(lambda:self.assertEqual(policies(),self.before))

    def run_helper(self,*args,success=True):
        result=subprocess.run([HELPER,*map(str,args)],env=self.environment,capture_output=True,timeout=15)
        if success:self.assertEqual(result.returncode,0,result.stderr)
        else:self.assertNotEqual(result.returncode,0)
        return result

    def test_interfaces_use_windows_api_with_inherited_restricted_script_policy(self):
        result=self.run_helper('interfaces');rows=json.loads(result.stdout)
        self.assertIsInstance(rows,list)
        for row in rows:self.assertEqual(set(row),{'address','prefix_length','name','default_route'})
        self.assertTrue(parse_interfaces(result.stdout))

    def test_processes_are_structured_and_do_not_return_command_lines(self):
        result=self.run_helper('processes');rows=json.loads(result.stdout)
        self.assertTrue(any(row['pid']==os.getpid() for row in rows),'read-only helper sees its Python caller')
        for row in rows:
            self.assertEqual(set(row),{'pid','name','path','s7_service'})
            self.assertIsInstance(row['pid'],int);self.assertIsInstance(row['s7_service'],bool)

    def test_lan_plan_has_only_six_owned_private_local_subnet_rules(self):
        rows=json.loads(self.run_helper('plan-lan',self.root,'apply').stdout)
        self.assertEqual(len(rows),6)
        for row in rows:
            self.assertTrue(row['Name'].startswith('S7LAN-'))
            self.assertEqual(row['Profiles'],2);self.assertEqual(row['Remote'],'LocalSubnet')
            self.assertIn(row['Protocol'],(6,17));self.assertTrue(Path(row['Program']).is_relative_to(self.root))
        self.assertFalse((self.root/'LocalData').exists())

    def test_internet_removal_uses_only_exact_owned_rule_names(self):
        key='a'*32;rows=json.loads(self.run_helper('plan-internet',self.root,key,'remove').stdout)
        self.assertEqual(len(rows),8)
        self.assertTrue(all(row['Name'].startswith('S7ET-'+key+'-') for row in rows))

    def test_bad_network_parameters_and_nonexistent_overlay_fail_before_elevation(self):
        for key,address,name in [('not-a-key','10.203.77.1','WePlay-12345678'),
                                 ('a'*32,'8.8.8.8','WePlay-12345678'),
                                 ('a'*32,'10.203.77.1','Ethernet'),
                                 ('a'*32,'10.203.77.1','WePlay-12345678')]:
            with self.subTest(key=key,address=address,name=name):
                self.run_helper('plan-internet',self.root,key,'apply',address,name,success=False)

    def test_no_generic_command_or_arbitrary_firewall_prefix_is_accepted(self):
        for args in [('execute','cmd.exe'),('inspect-firewall','*'),('plan-lan',str(self.root),'shell'),
                     ('plan-lan',str(self.root)+':stream','apply')]:
            self.run_helper(*args,success=False)

    def test_direct_process_name_guard_works_without_any_helper_or_shell(self):
        self.assertIn('python.exe',windows_system.process_names())

    def test_python_helper_launch_rejects_external_clr_hooks_without_mutating_parent_environment(self):
        with patch.dict(os.environ,{'DOTNET_STARTUP_HOOKS':'untrusted','CORECLR_ENABLE_PROFILING':'1','COMPlus_test':'untrusted',
                                    'PATH':'untrusted','PSExecutionPolicyPreference':'Restricted'}), \
                patch.object(windows_system.subprocess,'run',return_value=Mock(returncode=0,stdout=b'[]')) as run:
            windows_system.command('interfaces')
            environment=run.call_args.kwargs['env']
            self.assertFalse(any(key.upper().startswith(('DOTNET_','CORECLR_','COR_','COMPLUS_')) for key in environment))
            self.assertNotIn('untrusted',environment['PATH'])
            self.assertEqual({key.upper():value for key,value in environment.items()}['PSEXECUTIONPOLICYPREFERENCE'],'Restricted')
            self.assertEqual(os.environ['DOTNET_STARTUP_HOOKS'],'untrusted')

if __name__=='__main__':unittest.main()
