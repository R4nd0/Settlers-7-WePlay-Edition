"""Native temporal admission; no GPU or game process is created."""
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from . import on12_native_temporal as temporal,on12_runtime_build as build,on12_temporal_patch as edits


class Tests(unittest.TestCase):
    def test_only_native_temporal_variant_runs(self):
        with patch.object(temporal.on12_runtime,'verify_build',return_value=(Path('unused'),{'variant':'water-worker'},b'')), patch.object(temporal.subprocess,'run') as run:
            with self.assertRaises(ValueError):temporal.gpu(Path('unused'),Path('unused'),Path('unused'))
            run.assert_not_called()

    def test_running_game_blocks_before_payload_or_launch(self):
        with TemporaryDirectory(dir=build.ROOT/'work') as temporary:
            case=Path(temporary);source=case/'source';source.mkdir()
            with patch.object(temporal.on12_runtime,'verify_build',return_value=(source,{'variant':'temporal-worker'},b'')), patch.object(temporal.on12_runtime,'validate_runtime'), patch.object(temporal.recover,'_no_games',side_effect=RuntimeError('KI active')), patch.object(temporal.dxwrapper,'artifacts') as payload, patch.object(temporal.subprocess,'run') as run:
                with self.assertRaisesRegex(RuntimeError,'KI active'):temporal.gpu(source,case/'package',case/'result')
                self.assertFalse((case/'result').exists())
                payload.assert_not_called();run.assert_not_called()

    def test_new_variant_uses_its_own_driver_fixture(self):
        directory=build.ROOT/'work/unused-native-temporal-test'
        native=build.project_cmake(directory,'temporal-worker')
        old=build.project_cmake(directory,'water-worker')
        self.assertIn('on12_native_temporal_gpu.cpp',native)
        self.assertIn('on12_constant_reuse_gpu.cpp',old)
        self.assertNotIn('on12_native_temporal_gpu.cpp',old)
        self.assertIn('native/on12_native_temporal.cpp',build.project_inputs('temporal-worker'))

    def test_source_edits_reject_unknown_original(self):
        for name in edits.EDITS:
            with self.subTest(name=name),self.assertRaises(ValueError):edits.patched(name,b'unknown source')


if __name__=='__main__':unittest.main()
