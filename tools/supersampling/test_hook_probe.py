import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.supersampling import hook_probe, probe


class HookBuildIdentityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        native = self.root / 'native'
        self.build = self.root / 'build'
        native.mkdir()
        self.build.mkdir()
        for name in hook_probe.SOURCES:
            (native / name).write_bytes(('fixture source: ' + name).encode())
        script = native / 'supersampling_hook_build.ps1'
        script.write_bytes(b'fixture build script')
        for name in hook_probe.BINARIES:
            (self.build / name).write_bytes(('fixture binary, never executed: ' + name).encode())
        self.manifest = {
            'format': 's7-native-graphics-build-v2',
            'source_set': 'effects-v2',
            'diagnostic_abi': {'comparison': 12},
            'compiled_capabilities': 15,
            'abi': hook_probe.BUILD_ABI,
            'build_sha256': probe.sha256(script),
            'files': {name: probe.sha256(native / name) for name in hook_probe.SOURCES},
        }
        self.manifest['files'].update({name: probe.sha256(self.build / name) for name in hook_probe.BINARIES})
        self.manifest_path = self.build / 'supersampling-hook-build.json'
        self.manifest_path.write_text(json.dumps(self.manifest), encoding='utf-8')
        self.patcher = patch.object(probe, 'REPOSITORY', self.root)
        self.patcher.start()
        self.addCleanup(self.patcher.stop)

    def test_accepts_exact_header_cpp_script_dll_and_fixture_binding(self):
        self.assertEqual(hook_probe.verify_build(self.build), self.manifest)

    def test_modern_build_requires_all_three_extra_sources_and_v2_abi(self):
        for name in set(hook_probe.MODERN_SOURCES)-set(hook_probe.SOURCES):
            source=self.root/'native'/name;source.write_bytes(('modern source: '+name).encode())
            self.manifest['files'][name]=probe.sha256(source)
        self.manifest.update(source_set='effects-v3',abi=hook_probe.BUILD_ABI_V2)
        self.manifest_path.write_text(json.dumps(self.manifest),encoding='utf-8')
        self.assertEqual(hook_probe.verify_build(self.build),self.manifest)
        self.manifest['abi']=hook_probe.BUILD_ABI
        self.manifest_path.write_text(json.dumps(self.manifest),encoding='utf-8')
        with self.assertRaisesRegex(ValueError,'Stale'):hook_probe.verify_build(self.build)

    def test_water_build_binds_program_module_and_explicit_water_abi(self):
        for name in set(hook_probe.WATER_SOURCES)-set(hook_probe.SOURCES):
            source=self.root/'native'/name;source.write_bytes(('water source: '+name).encode())
            self.manifest['files'][name]=probe.sha256(source)
        self.manifest.update(source_set='effects-v4',abi=hook_probe.BUILD_ABI_WATER)
        self.manifest_path.write_text(json.dumps(self.manifest),encoding='utf-8')
        self.assertEqual(hook_probe.verify_build(self.build),self.manifest)
        self.manifest['files'].pop('water_shader_effects.cpp')
        self.manifest_path.write_text(json.dumps(self.manifest),encoding='utf-8')
        with self.assertRaisesRegex(ValueError,'Stale'):hook_probe.verify_build(self.build)
        self.manifest['abi']=hook_probe.BUILD_ABI_V2
        self.manifest['files'].pop('native_effects_wire.h')
        self.manifest_path.write_text(json.dumps(self.manifest),encoding='utf-8')
        with self.assertRaisesRegex(ValueError,'Stale'):hook_probe.verify_build(self.build)

    def test_rejects_changed_header_even_if_dll_and_cpp_are_unchanged(self):
        (self.root / 'native/supersampling_hook.h').write_bytes(b'changed ABI')
        with self.assertRaisesRegex(ValueError, 'Stale'):
            hook_probe.verify_build(self.build)

    def test_rejects_changed_dll_without_executing_anything(self):
        (self.build / 'WePlaySupersamplingLab.dll').write_bytes(b'changed DLL')
        with self.assertRaisesRegex(ValueError, 'Stale'):
            hook_probe.verify_build(self.build)

    def test_rejects_extra_source_or_binary_in_manifest(self):
        self.manifest['files']['unreviewed.dll'] = '0' * 64
        self.manifest_path.write_text(json.dumps(self.manifest), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'Stale'):
            hook_probe.verify_build(self.build)


class StartupReceiptTests(unittest.TestCase):
    def test_requires_clipped_before_geometry_and_unscaled_input_proof(self):
        receipt = dict(reset_successes=6, startup_clipped_size_preserved_before_geometry=True,
                       startup_input_unscaled=True)
        self.assertTrue(hook_probe.startup_receipt_valid(receipt))
        for key in ('startup_clipped_size_preserved_before_geometry', 'startup_input_unscaled'):
            missing = dict(receipt)
            missing.pop(key)
            self.assertFalse(hook_probe.startup_receipt_valid(missing))
            for malformed in (False, None, 1, 'true', []):
                with self.subTest(key=key, malformed=malformed):
                    self.assertFalse(hook_probe.startup_receipt_valid({**receipt, key: malformed}))
        for wrong in (4, 5, 7, 6.0, '6', True, None):
            with self.subTest(reset_successes=wrong):
                self.assertFalse(hook_probe.startup_receipt_valid({**receipt, 'reset_successes': wrong}))


if __name__ == '__main__':
    unittest.main()
