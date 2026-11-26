"""Own CPU fixtures only; no process, device, game or GPU is accessed."""
from contextlib import nullcontext
import hashlib
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch
from . import game,recover,water_on12_probe as p


class Tests(unittest.TestCase):
    def setUp(self):
        temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup);self.root=Path(temp.name)
        self.build=self.root/'work/build';self.build.mkdir(parents=True)
        self.case=self.root/'work/case';self.case.mkdir()
        native=self.root/'native';native.mkdir()
        for name in p.SOURCES:
            (native/name).write_bytes(name.encode());(self.build/name).write_bytes(name.encode())
        (native/'water_on12_probe_build.ps1').write_bytes(b'authored build script')
        for name in p.BINARIES:(self.build/name).write_bytes(name.encode())
        self.bundle=self.root/'work/game';self.exe=self.bundle/'Base/Bin/Settlers7R.exe'
        self.exe.parent.mkdir(parents=True);self.exe.write_bytes(b'authored fixture exe, never run')
        self.stub=b'fixture stub';self.wrapper=b'fixture wrapper'
        (self.exe.parent/'d3d9.dll').write_bytes(self.stub);(self.exe.parent/'dxwrapper.dll').write_bytes(self.wrapper)
        for obj,name,value in ((p,'ROOT',self.root),(recover,'ROOT',self.root),(p.loader,'UBI',hashlib.sha256(self.exe.read_bytes()).hexdigest())):
            mocked=patch.object(obj,name,value);mocked.start();self.addCleanup(mocked.stop)
        mocked=patch.object(game.platform,'load',return_value=({'edition':'history'},self.exe,self.exe.with_name('adapter.dll')))
        mocked.start();self.addCleanup(mocked.stop)
        self.manifest=dict(format='s7-water-on12-probe-build-v2',config_bytes=1036,supports_pow_cap=True,
            build_sha256=p.loader.digest(native/'water_on12_probe_build.ps1'),files={
                name:p.loader.digest(self.build/name) for name in (*p.SOURCES,*p.BINARIES)})
        (self.build/'water-probe-build.json').write_text(json.dumps(self.manifest))

    def test_build_source_binary_header_and_manifest_drift_refused(self):
        self.assertEqual(p.verify_build(self.build)[0],self.build/p.DLL)
        for path in (self.build/p.DLL,self.root/'native/water_on12_identity.h',self.build/'water_on12_probe_fixture.exe'):
            raw=path.read_bytes();path.write_bytes(raw+b'drift')
            with self.subTest(path=path),self.assertRaises(ValueError):p.verify_build(self.build)
            path.write_bytes(raw)
        self.manifest['supports_pow_cap']=False
        (self.build/'water-probe-build.json').write_text(json.dumps(self.manifest))
        with self.assertRaises(ValueError):p.verify_build(self.build)

    def test_report_configuration_has_fixed_read_only_abi(self):
        report=self.case/'water-probe.private.json';raw=p.configuration(report)
        self.assertEqual(len(raw),1036);self.assertEqual(struct.unpack_from('<3I',raw),(1036,1,0))
        self.assertEqual(raw[12:].decode('utf-16le').rstrip('\0'),str(report.resolve()))
        report.write_bytes(b'newer diagnostic')
        for target in (report,self.case/'save.s7s',self.root/'outside/water-probe.private.json'):
            with self.subTest(target=target),self.assertRaises(ValueError):p.configuration(target)

    def test_owned_isolation_checks_and_source_bound_initialization(self):
        probe=p.Probe(self.build,self.bundle,self.case)
        owned=SimpleNamespace(executable=self.exe,native_initialized=True,kernel_isolation=7)
        with patch.object(p.loader,'invoke_water')as invoke:
            with self.assertRaises(RuntimeError):probe.initialize(owned)
            invoke.assert_not_called()
        with patch('tools.graphics_lab.load_hook.file_lease',return_value=nullcontext()), \
                patch.object(game.graphics_profile,'DXVK_SHA256','unused'), \
                patch('tools.graphics_lab.dxwrapper.STUB_SHA256',hashlib.sha256(self.stub).hexdigest()), \
                patch('tools.graphics_lab.dxwrapper.DLL_SHA256',hashlib.sha256(self.wrapper).hexdigest()):
            probe.prepare()
        owned.kernel_isolation=1
        with patch.object(p.loader,'invoke_water')as invoke:
            with self.assertRaises(RuntimeError):probe.initialize(owned)
            invoke.assert_not_called()
        owned.kernel_isolation=7
        with patch.object(p.loader,'invoke_water',return_value={'succeeded':True})as invoke:
            probe.initialize(owned)
            invoke.assert_called_once_with(owned,self.build/p.DLL,probe.config,pow_cap=False)
        result=json.loads((self.case/'water-probe-initialized.private.json').read_text())
        self.assertTrue(result['read_only']);probe.close();self.assertFalse(probe.prepared)

    def test_unknown_initializer_is_rejected_before_any_process_access(self):
        owned=SimpleNamespace(owned=Mock())
        with self.assertRaises(ValueError):p.loader._invoke_fixed(owned,Path('unused.dll'),b'','unrelated')
        owned.owned.assert_not_called()

    def test_cap_is_explicit_and_excludes_other_renderers(self):
        raw=p.configuration(self.case/'water-probe.private.json',pow_cap=True)
        self.assertEqual(struct.unpack_from('<3I',raw),(1036,1,1))
        with self.assertRaises(ValueError):p.configuration(self.case/'water-probe.private.json',pow_cap=1)
        for kwargs in ({},{'water_probe':self.build},{'water_probe':self.build,'laboratory_renderer':'dxwrapper-d3d9'}):
            with self.subTest(kwargs=kwargs),patch.object(game.platform,'load')as load:
                with self.assertRaises(ValueError):game.run('never-read','never-created',water_pow_cap=True,**kwargs)
                load.assert_not_called()

    def test_observer_cannot_enter_native_renderer_or_mixed_capture_or_existing_hook(self):
        for kwargs in ({},{'laboratory_renderer':'native'}):
            with self.subTest(kwargs=kwargs),patch.object(game.platform,'load')as load:
                with self.assertRaises(ValueError):game.run('never-read','never-created',water_probe=self.build,**kwargs)
                load.assert_not_called()
        with self.assertRaisesRegex(ValueError,'excludes native hooks'):
            game.run('never-read','never-created',water_probe=self.build,laboratory_renderer='dxwrapper-d3d9on12',hook=Path('never-read.dll'))


if __name__=='__main__':unittest.main()
