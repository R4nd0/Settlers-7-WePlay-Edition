import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from tools.graphics_lab import game, load_hook


class GraphicsControllerTests(unittest.TestCase):
    def test_cleanup_failures_do_not_skip_options_service_or_shared_profile_checks(self):
        calls = []
        def action(label, fail=False):
            def run():
                calls.append(label)
                if fail:
                    raise OSError('fixture failure')
            return run
        errors = game.cleanup_steps([(label, action(label, fail)) for label, fail in (
            ('game-stop', False), ('memory-close', True), ('game-close', True),
            ('service-stop', True), ('restore-options', False), ('restore-runtime', False),
            ('shared-profile-verification', False))])
        self.assertEqual(calls[-3:], ['restore-options', 'restore-runtime', 'shared-profile-verification'])
        self.assertEqual([e['stage'] for e in errors], ['memory-close', 'game-close', 'service-stop'])

    def test_hook_lease_denies_in_place_write_during_verified_load_window(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'fixture.dll'
            path.write_bytes(b'fixture, never loaded')
            with load_hook.file_lease(path):
                self.assertEqual(path.read_bytes(), b'fixture, never loaded')
                with self.assertRaises(PermissionError):
                    path.write_bytes(b'changed')
            path.write_bytes(b'lease released')

    def test_child_environment_removes_unrelated_render_and_lab_overrides(self):
        inherited = {'PATH': 'fixture-path', 'S7_UNKNOWN': 'bad', 'weplay_crossplay': 'bad',
                     'VK_LAYER_PATH': 'bad', 'RESHADE_BASE_PATH_OVERRIDE': 'bad',
                     'dxvk_config': 'bad', 'D3D9_FORCED': 'bad'}
        result = game.clean_environment(inherited)
        self.assertEqual(inherited['dxvk_config'], 'bad')
        self.assertEqual(result['PATH'], 'fixture-path')
        self.assertEqual(result['VK_LOADER_LAYERS_DISABLE'], '~implicit~')
        self.assertEqual(result['DXVK_CONFIG'], 'd3d9.floatEmulation = Strict; d3d9.maxFrameRate = 60')
        self.assertFalse(any(k.lower().startswith(('s7_', 'weplay_', 'reshade_')) for k in result))

    def test_configuration_preserves_fixed_x86_abi_and_exact_aspect_ratio(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'work').mkdir()
            report = root / 'work/report.json'
            with patch.object(game, 'ROOT', root):
                config = load_hook.configuration((1920, 1080), (1280, 720), report)
                self.assertEqual(len(config), 1048)
                self.assertEqual(struct.unpack_from('<6I', config), (1048, 1, 1920, 1080, 1280, 720))
                self.assertEqual(config[24:].decode('utf-16le').rstrip('\0'), str(report.resolve()))
                for render, output in (((3841, 2160), (1280, 720)), ((1920, 1080), (1280, 721)),
                                       ((1920, 1080), (640, 360)), ((True, 1080), (1280, 720))):
                    with self.subTest(render=render, output=output), self.assertRaises(ValueError):
                        load_hook.configuration(render, output, report)
                report.write_bytes(b'prior diagnostic')
                with self.assertRaises(ValueError):
                    load_hook.configuration((1920, 1080), (1280, 720), report)

    def test_build_identity_rejects_binary_source_and_manifest_drift(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'native').mkdir()
            build = root / 'work/build'
            build.mkdir(parents=True)
            for name in load_hook.SOURCES:
                (root / 'native' / name).write_bytes(name.encode())
            script = root / 'native/supersampling_hook_build.ps1'
            script.write_bytes(b'fixture build script')
            dll = build / load_hook.DLL_NAME
            dll.write_bytes(b'fixture DLL, never loaded')
            fixture = build / 'supersampling_hook_fixture.exe'
            fixture.write_bytes(b'fixture EXE, never run')
            manifest = {'format': 's7-supersampling-hook-build-v1',
                        'build_sha256': load_hook.digest(script), 'files': {
                            name: load_hook.digest(root / 'native' / name) for name in load_hook.SOURCES}}
            manifest['files'].update({dll.name: load_hook.digest(dll), fixture.name: load_hook.digest(fixture)})
            path = build / 'supersampling-hook-build.json'
            path.write_text(json.dumps(manifest), encoding='utf-8')
            with patch.object(game, 'ROOT', root), patch.object(load_hook, 'ROOT', root):
                self.assertEqual(load_hook.verify_build(dll)[0], dll.resolve())
                for changed in (dll, fixture, script, root / 'native/supersampling_hook.h'):
                    original = changed.read_bytes()
                    changed.write_bytes(original + b' drift')
                    with self.subTest(changed=changed.name), self.assertRaises(ValueError):
                        load_hook.verify_build(dll)
                    changed.write_bytes(original)
                with self.assertRaises(ValueError):
                    load_hook.verify_build(build / 'unrelated.dll')

    def test_fixed_worker_calls_target_iat_and_uses_stdcall_thread_exit(self):
        code = load_hook.worker(0x100000, 0x200000, 0x400100, 0x400200, 64, 128, 160, 8)
        self.assertLess(len(code), 512)
        self.assertTrue(code.endswith(b'\x61\x9d\x31\xc0\xc2\x04\x00'))
        self.assertEqual(code.count(b'\xff\x15' + struct.pack('<I', 0x400100)), 2)
        self.assertEqual(code.count(b'\xff\x15' + struct.pack('<I', 0x400200)), 1)
        for i in range(len(code) - 6):
            if code[i:i+2] == b'\x0f\x84':
                destination = i + 6 + struct.unpack_from('<i', code, i + 2)[0]
                self.assertEqual(code[destination:destination+2], b'\xc7\x05')


if __name__ == '__main__':
    unittest.main()
