import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/installer'), str(ROOT / 'tools/lan')]
import graphics_profile as graphics
import install
import isolated_process
import platform_standalone as platform
import reshade_runtime as reshade


class LegacyConfigTests(unittest.TestCase):
    def test_async_game_receives_shader_settings_in_a_file_it_can_read(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'LocalData').mkdir()
            executable = root / graphics.EXE
            adapter = root / graphics.LOADER
            executable.parent.mkdir(parents=True)
            canonical = executable.with_name('dxvk.conf')
            canonical.write_bytes(graphics.CONFIG)
            record = {'edition': 'history', 'files': {
                graphics.LOADER: 'fixture-adapter', platform.POLICY_FILE: platform.POLICY}}
            with patch.object(platform, 'load', return_value=(record, executable, adapter)), \
                 patch.object(subprocess, 'run', return_value=SimpleNamespace(returncode=0)), \
                 patch.object(graphics, 'apply_launch_renderer'), \
                 patch.object(isolated_process, 'launch', return_value=SimpleNamespace()) as child:
                platform.launch(root, renderer=('async', root / 'Package'), environment={})
            config = Path(child.call_args.kwargs['environment']['DXVK_CONFIG_FILE'])
            self.assertIn(graphics.SHADER_COMPATIBILITY.encode(), config.read_bytes())
            self.assertIn(graphics.CONFIG, config.read_bytes())
            self.assertEqual(canonical.read_bytes(), graphics.CONFIG)
            self.assertTrue(config.is_relative_to(root / 'LocalData'))

    @unittest.skipUnless(sys.platform == 'win32', 'Windows DXVK probe path')
    def test_async_smaa_probe_uses_the_same_shader_settings_file(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'LocalData').mkdir()
            package = root / 'Package'
            payload = package / 'Payload/Graphics'
            dll = payload / 'async/d3d9.dll'
            dll.parent.mkdir(parents=True)
            dll.write_bytes(b'fixture dll')
            probe_exe = payload / 'S7GraphicsProbe.exe'
            probe_exe.write_bytes(b'fixture probe')
            bundle = package / reshade.RUNTIME_BUNDLE
            bundle.parent.mkdir(parents=True, exist_ok=True)
            bundle.write_bytes(b'fixture bundle')
            manifest = {'compat_smaa_probe': True, 'files': {
                'payload/graphics/s7graphicsprobe.exe': hashlib.sha256(probe_exe.read_bytes()).hexdigest()}}

            def run(_command, *, cwd, env, **_kwargs):
                config = Path(env['DXVK_CONFIG_FILE'])
                self.assertTrue(config.is_relative_to(cwd))
                self.assertIn(graphics.SHADER_COMPATIBILITY.encode(), config.read_bytes())
                folder = reshade._folder(cwd, {'enhanced_aa': True})
                folder.mkdir(parents=True)
                (folder / 'ReShade.log').write_bytes(b'Successfully compiled SMAA.fx')
                return SimpleNamespace(returncode=0, stdout=b'{"format":"s7-render-probe-v1","devices":["Fixture GPU"]}')

            with patch.object(install, 'package_metadata', return_value=manifest), \
                 patch.dict(graphics.ASYNC_RENDERERS, {'async': hashlib.sha256(dll.read_bytes()).hexdigest()}), \
                 patch.object(reshade, 'launch_environment', return_value={}), \
                 patch.object(subprocess, 'run', side_effect=run):
                self.assertEqual(reshade.probe_compatibility_smaa(root, package, 'async'), 'Fixture GPU')
