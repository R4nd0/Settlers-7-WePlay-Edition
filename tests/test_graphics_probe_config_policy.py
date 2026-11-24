from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/installer'), str(ROOT / 'tools/lan')]
import desktop_backend as backend
import graphics_profile as graphics


@unittest.skipUnless(sys.platform == 'win32', 'Windows DXVK graphics probe')
class ProbeConfigPolicyTests(unittest.TestCase):
    def test_local_dxvk_file_does_not_change_the_hardware_probe_configuration(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            state, package = root / 'State', root / 'Package'
            state.mkdir()
            local_config = state / 'dxvk.conf'
            local_config.write_bytes(b'd3d9.customDeviceDesc = "Other GPU"\n')
            payload = package / 'Payload/Graphics'
            payload.mkdir(parents=True)
            probe, dll = payload / 'S7GraphicsProbe.exe', payload / 'd3d9.dll'
            probe.write_bytes(b'fixture probe')
            dll.write_bytes(b'fixture renderer')
            manifest = {'files': {'payload/graphics/s7graphicsprobe.exe': graphics.content.digest(probe)}}
            desktop = Mock(state=state, package=package, displays=Mock(return_value=[]),
                           package_manifest=Mock(return_value=manifest),
                           validate_probe=backend.Desktop.validate_probe)
            used_config = []

            def run(_command, *, cwd, env, **_kwargs):
                # DXVK 3.x uses cwd/dxvk.conf if the override is absent.
                config = Path(env.get('DXVK_CONFIG_FILE', Path(cwd) / 'dxvk.conf'))
                used_config.append(config)
                self.assertEqual(config.read_bytes(), graphics.CONFIG)
                return SimpleNamespace(returncode=0, stdout=b'{"format":"s7-render-probe-v1","devices":["Fixture GPU"]}')

            with patch.object(graphics, 'DXVK_SHA256', graphics.content.digest(dll)), \
                 patch.object(graphics, 'ensure_closed'), \
                 patch.object(subprocess, 'run', side_effect=run):
                value = backend.Desktop.probe(desktop, refresh=True)
            self.assertEqual(value['devices'], ['Fixture GPU'])
            self.assertEqual(local_config.read_bytes(), b'd3d9.customDeviceDesc = "Other GPU"\n')
            self.assertFalse(used_config[0].exists())
