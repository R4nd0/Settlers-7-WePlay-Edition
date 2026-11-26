import json
from pathlib import Path
import struct
import tempfile
import unittest
from contextlib import nullcontext
from unittest.mock import patch

from tools.graphics_lab import game, load_hook


class GraphicsControllerTests(unittest.TestCase):
    def test_live_memory_report_retries_a_reader_collision_without_marking_stale_data_current(self):
        from types import SimpleNamespace
        guardian=dict(samples=1,max_pressure=0,monitor_failed=False,recovery_required=False)
        collision=PermissionError('reader denied replacement');collision.winerror=5
        with tempfile.TemporaryDirectory() as root:
            path=Path(root)/'memory-live.private.json'
            report=game.LiveMemoryReport(path,123,'a'*64)
            original_replace=game.os.replace
            calls=[]
            def replace(a,b):
                calls.append(1)
                if len(calls)==1:
                    self.assertIsNone(report.last)
                    raise collision
                return original_replace(a,b)
            with patch.object(game.os,'replace',side_effect=replace),patch.object(game.time,'sleep') as sleep:
                self.assertTrue(report.publish(SimpleNamespace(snapshot=lambda:guardian.copy())))
                sleep.assert_called_once_with(.02)
            self.assertEqual(json.loads(path.read_text())['guardian'],guardian)
            self.assertEqual(report.last,(1,0,False,False))

    def test_live_memory_report_records_persistent_publication_failure_without_stopping_diagnostics(self):
        from types import SimpleNamespace
        guardian=dict(samples=1,max_pressure=0,monitor_failed=False,recovery_required=False)
        collision=PermissionError('persistent denial');collision.winerror=5
        with tempfile.TemporaryDirectory() as root:
            report=game.LiveMemoryReport(Path(root)/'memory-live.private.json',123,'a'*64)
            with patch.object(game.os,'replace',side_effect=collision) as replace,patch.object(game.time,'sleep') as sleep:
                self.assertFalse(report.publish(SimpleNamespace(snapshot=lambda:guardian.copy())))
                self.assertEqual(replace.call_count,10)
                self.assertEqual(sleep.call_count,9)
            self.assertIsNone(report.last)
            self.assertEqual(report.publication_failures,1)

    def test_failed_monitor_still_publishes_fresh_owner_heartbeat_and_truthful_failure(self):
        from types import SimpleNamespace
        guardian=dict(samples=0,max_pressure=2,monitor_failed=True,recovery_required=True)
        with tempfile.TemporaryDirectory() as root:
            path=Path(root)/'memory-live.private.json';report=game.LiveMemoryReport(path,123,'a'*64)
            with patch.object(game.time,'monotonic_ns',side_effect=(10_000_000_000,12_000_000_000)):
                self.assertTrue(report.publish(SimpleNamespace(snapshot=lambda:guardian.copy())))
                self.assertTrue(report.publish(SimpleNamespace(snapshot=lambda:guardian.copy())))
            record=json.loads(path.read_text())
            self.assertEqual(record['sampled_monotonic_ns'],12_000_000_000)
            self.assertEqual(record['guardian'],guardian)
            self.assertEqual(record['memory_policy'],'observe_only')

    def test_leaf_hemisphere_excludes_mixed_or_undeclared_material_modes(self):
        valid=dict(hook=Path('never-loaded.dll'),high_quality=True,material_config_version=2,
                   material_effects=('improved_vegetation',),leaf_hemisphere=True)
        for changes in ({'leaf_hemisphere':1},{'baseline_crash_watch':True},{'hook':None},
                        {'high_quality':False},{'material_config_version':1},{'material_effects':()},
                        {'material_effects':('improved_vegetation','improved_terrain')},
                        {'material_effects':('improved_vegetation','improved_water')}):
            with self.subTest(changes=changes),patch.object(game.platform,'load') as load:
                with self.assertRaisesRegex(ValueError,'Leaf hemisphere requires explicit'):
                    game.run(Path('never-read'),Path('never-created'),**{**valid,**changes})
                load.assert_not_called()

    def test_profile_b_has_explicit_version_and_cannot_use_profile_a_capability(self):
        with patch.object(load_hook,'verify_build',return_value=(Path('old.dll'),{'diagnostic_abi':{'ambient':12}})):
            with self.assertRaisesRegex(ValueError,'does not support the fixed ambient'):
                load_hook.invoke(None,Path('old.dll'),'ambient_v2',struct.pack('<3I',12,2,1))
        for operation,version in (('ambient',2),('ambient_v2',1),('ambient_v2',3)):
            with self.subTest(operation=operation,version=version),patch.object(load_hook,'verify_build') as verify:
                with self.assertRaises(ValueError):
                    load_hook.invoke(None,Path('never-read.dll'),operation,struct.pack('<3I',12,version,1))
                verify.assert_not_called()
        for options in ({'ambient_profile':'B'},{'ambient_profile':'C','ambient_occlusion':True}):
            with self.subTest(options=options),patch.object(game.platform,'load') as load:
                with self.assertRaisesRegex(ValueError,'Select explicit ambient profile'):
                    game.run(Path('never-read'),Path('never-created'),**options)
                load.assert_not_called()

    def test_ambient_requires_its_own_fixed_toggle_and_explicit_build_capability(self):
        raw=b'{"operation":"ambient","sequence":3,"enabled":1}'
        self.assertEqual(game.native_command(raw,2,ambient_allowed=True)['enabled'],1)
        self.assertIsNone(game.native_command(raw,3,ambient_allowed=True))
        with self.assertRaises(ValueError):game.native_command(raw,0,shadow_allowed=True)
        for value in (raw.replace(b':1}',b':true}'),raw.replace(b':1}',b':2}'),raw.replace(b'}',b',"strength":2}')):
            with self.subTest(value=value),self.assertRaises(ValueError):
                game.native_command(value,0,ambient_allowed=True)
        with self.assertRaises(ValueError):
            load_hook.invoke(None,Path('never-loaded.dll'),'ambient',struct.pack('<3I',12,1,2))
        with patch.object(load_hook,'verify_build',return_value=(Path('old.dll'),{'diagnostic_abi':{'shadow':12}})):
            with self.assertRaisesRegex(ValueError,'does not support the fixed ambient'):
                load_hook.invoke(None,Path('old.dll'),'ambient',struct.pack('<3I',12,1,1))

    def test_ambient_startup_excludes_shadow_and_unobserved_modes(self):
        valid=dict(hook=Path('never-loaded.dll'),high_quality=True,observer_catalog=Path('catalog.bin'),ambient_occlusion=True)
        for changes in ({'ambient_occlusion':1},{'baseline_crash_watch':True},{'hook':None},
                        {'high_quality':False},{'observer_catalog':None},{'shadow_filter':True}):
            with self.subTest(changes=changes),patch.object(game.platform,'load') as load:
                with self.assertRaisesRegex(ValueError,'Ambient occlusion requires exclusive'):
                    game.run(Path('never-read'),Path('never-created'),**{**valid,**changes})
                load.assert_not_called()

    def test_shadow_toggle_has_no_arbitrary_profile_or_export_and_requires_opt_in(self):
        raw=b'{"operation":"shadow","sequence":3,"enabled":1}'
        self.assertEqual(game.native_command(raw,2,shadow_allowed=True)['enabled'],1)
        self.assertIsNone(game.native_command(raw,3,shadow_allowed=True))
        with self.assertRaises(ValueError):game.native_command(raw,0)
        for value in (raw.replace(b':1}',b':true}'),raw.replace(b':1}',b':2}'),
                      raw.replace(b'}',b',"radius":3}'),raw.replace(b'}',b',"export":"other"}')):
            with self.subTest(value=value),self.assertRaises(ValueError):
                game.native_command(value,0,shadow_allowed=True)
        with self.assertRaises(ValueError):
            load_hook.invoke(None,Path('never-loaded.dll'),'shadow',struct.pack('<3I',12,1,2))
        with patch.object(load_hook,'verify_build',return_value=(Path('old.dll'),{'diagnostic_abi':{'comparison':12}})):
            with self.assertRaisesRegex(ValueError,'does not support the fixed shadow'):
                load_hook.invoke(None,Path('old.dll'),'shadow',struct.pack('<3I',12,1,1))

    def test_shadow_cannot_enter_unobserved_or_baseline_startup(self):
        valid=dict(hook=Path('never-loaded.dll'),high_quality=True,observer_catalog=Path('catalog.bin'),shadow_filter=True)
        for changes in ({'shadow_filter':1},{'baseline_crash_watch':True},{'hook':None},
                        {'high_quality':False},{'observer_catalog':None}):
            with self.subTest(changes=changes),patch.object(game.platform,'load') as load:
                with self.assertRaisesRegex(ValueError,'Shadow filter requires'):
                    game.run(Path('never-read'),Path('never-created'),**{**valid,**changes})
                load.assert_not_called()

    def test_macro_recipe_cannot_enter_baseline_or_legacy_material_modes(self):
        valid=dict(hook=Path('not-loaded.dll'),high_quality=True,
                   material_config_version=2,material_effects=('improved_terrain',),terrain_macro=True)
        for changes in ({'terrain_macro':1},{'baseline_crash_watch':True},
                        {'material_config_version':1},{'hook':None},{'high_quality':False},
                        {'material_effects':()},
                        {'material_effects':('improved_terrain','improved_water')}):
            with self.subTest(changes=changes),patch.object(game.platform,'load') as load:
                with self.assertRaisesRegex(ValueError,'Terrain macro requires'):
                    game.run(Path('never-read'),Path('never-created'),**{**valid,**changes})
                load.assert_not_called()

    def test_water_command_is_separate_explicit_boolean_and_never_legacy_material_enable(self):
        raw=b'{"operation":"water","sequence":3,"enabled":1}'
        self.assertEqual(game.native_command(raw,2,0,water_allowed=True)['enabled'],1)
        self.assertIsNone(game.native_command(raw,3,0,water_allowed=True))
        for value in (raw,raw.replace(b':1}',b':true}'),raw.replace(b':1}',b':2}'),raw.replace(b'}',b',"path":"other"}')):
            with self.subTest(value=value),self.assertRaises(ValueError):game.native_command(value,0,0,water_allowed=value!=raw)
        with self.assertRaises(ValueError):game.native_command(b'{"operation":"effects","sequence":4,"enabled":1}',0,6,water_allowed=True)

    def test_water_transport_requires_explicit_build_and_fixed_1076_body(self):
        payload=struct.pack('<5I',1076,1,3840,2160,1)+bytes(1056)
        dll=Path('fixture-native.dll')
        with patch.object(load_hook,'verify_build',return_value=(dll,{'abi':load_hook.BUILD_ABI_V2})),\
                patch.object(load_hook,'_invoke_pinned')as call:
            with self.assertRaisesRegex(ValueError,'WaterProgram'):load_hook.invoke(None,dll,'water',payload)
            call.assert_not_called()
        with patch.object(load_hook,'verify_build',return_value=(dll,{'abi':load_hook.BUILD_ABI_WATER})),\
                patch.object(load_hook,'file_lease',return_value=nullcontext()),\
                patch.object(load_hook,'_invoke_pinned',return_value=True)as call:
            self.assertTrue(load_hook.invoke(None,dll,'water',payload))
            call.assert_called_once_with(None,dll,payload,b'WePlayNativeGraphicsConfigureWater\0')
        with patch.object(load_hook,'verify_build')as verify:
            for broken in(payload[:-1],payload[:16]+struct.pack('<I',2)+payload[20:],struct.pack('<2I',1076,2)+payload[8:]):
                with self.assertRaises(ValueError):load_hook.invoke(None,dll,'water',broken)
            verify.assert_not_called()

    def test_material_v2_requires_exact_header_and_explicit_build_abi(self):
        payload=struct.pack('<2I',6180,2)+bytes(6172)
        dll=Path('fixture-native.dll')
        with patch.object(load_hook,'verify_build',return_value=(dll,{'abi':load_hook.BUILD_ABI})):
            with patch.object(load_hook,'_invoke_pinned') as call, self.assertRaisesRegex(ValueError,'does not support'):
                load_hook.invoke(None,dll,'effects',payload)
            call.assert_not_called()
        with patch.object(load_hook,'verify_build',return_value=(dll,{'abi':load_hook.BUILD_ABI_V2})), \
                patch.object(load_hook,'file_lease',return_value=nullcontext()), \
                patch.object(load_hook,'_invoke_pinned',return_value={'accepted':True}) as call:
            self.assertEqual(load_hook.invoke(None,dll,'effects',payload),{'accepted':True})
            call.assert_called_once_with(None,dll,payload,b'WePlayNativeGraphicsConfigureEffects\0')
        for size,version,count in ((4132,2,4132),(6180,1,6180),(6180,2,4132),(4132,1,6180),(6181,2,6181)):
            with self.subTest(size=size,version=version,count=count),patch.object(load_hook,'verify_build') as verify:
                with self.assertRaises(ValueError):load_hook.invoke(None,dll,'effects',struct.pack('<2I',size,version)+bytes(count-8))
                verify.assert_not_called()

    def test_worker_prefix_counts_utf16_and_alignment_for_v2_budget(self):
        renderer=Path('C:/private/renderer-\U0001f30a/d3d9.dll')
        dll=Path('C:/private/build/WePlaySupersamplingLab.dll')
        export=b'WePlayNativeGraphicsConfigureEffects\0'
        payload,renderer_at,dll_at,export_at,config_at=load_hook._payload_prefix(renderer,dll,export)
        self.assertEqual(renderer_at,8)
        self.assertEqual(payload[renderer_at:dll_at],str(renderer).encode('utf-16-le')+b'\0\0')
        self.assertEqual(payload[dll_at:export_at],str(dll).encode('utf-16-le')+b'\0\0')
        self.assertEqual(payload[export_at:export_at+len(export)],export)
        self.assertEqual(config_at,len(payload))
        self.assertEqual(config_at%4,0)
        self.assertLessEqual(config_at+6180,8192)

    def test_material_commands_only_toggle_prepared_subsets(self):
        self.assertEqual(game.native_command(b'{"operation":"effects","sequence":2,"enabled":0}',1,7)['enabled'],0)
        self.assertEqual(game.native_command(b'{"operation":"effects","sequence":2,"enabled":5}',1,7)['enabled'],5)
        self.assertIsNone(game.native_command(b'{"operation":"effects","sequence":1,"enabled":7}',1,7))
        for raw,flags in ((b'{"operation":"effects","sequence":2,"enabled":8}',7),
                          (b'{"operation":"effects","sequence":2,"enabled":true}',7),
                          (b'{"operation":"effects","sequence":2,"enabled":0}',0),
                          (b'{"operation":"effects","sequence":2,"enabled":7,"path":"foreign"}',7)):
            with self.assertRaises(ValueError):game.native_command(raw,0,flags)

    def test_comparison_is_explicit_diagnostic_not_general_memory_command(self):
        raw=b'{"operation":"comparison","sequence":3,"enabled":1}'
        self.assertEqual(game.native_command(raw,2,0,True)['enabled'],1)
        with self.assertRaises(ValueError):game.native_command(raw,2,7,False)
        with self.assertRaises(ValueError):game.native_command(raw.replace(b':1}',b':2}'),2,7,True)
        with self.assertRaises(ValueError):load_hook.invoke(None,Path('unloaded.dll'),'comparison',struct.pack('<3I',12,1,2))

    def test_crash_watcher_admission_has_no_pid_selector_and_is_opt_in(self):
        raw=b'{"operation":"crash-watch-admit","sequence":3}'
        self.assertEqual(game.native_command(raw,2,0,False,True)['sequence'],3)
        self.assertIsNone(game.native_command(raw,3,0,False,True))
        with self.assertRaises(ValueError):game.native_command(raw,0)
        with self.assertRaises(ValueError):game.native_command(raw.replace(b'}',b',"pid":123}'),0,0,False,True)
    def test_elapsed_measurement_budget_only_requests_regular_close_once(self):
        self.assertFalse(game.measurement_budget_expired(29, 30, False))
        self.assertTrue(game.measurement_budget_expired(30, 30, False))
        self.assertFalse(game.measurement_budget_expired(300, 30, True))
    def test_native_commands_are_capture_only_monotonic_and_bounded(self):
        self.assertEqual(game.capture_command(b'{"operation":"capture","sequence":3}', 2), 3)
        self.assertIsNone(game.capture_command(b'{"operation":"capture","sequence":2}', 2))
        for raw in (b'{"operation":"load","sequence":3}', b'{"operation":"capture","sequence":true}',
                    b'{"operation":"capture","sequence":1000001}', b' ' * 4097,
                    b'{"operation":"capture","sequence":3,"export":"arbitrary"}'):
            with self.subTest(raw=raw[:80]), self.assertRaises(ValueError):
                game.capture_command(raw, 0)
        with self.assertRaises(ValueError):
            load_hook.invoke(None, Path('never-loaded.dll'), 'arbitrary', b'')
        with self.assertRaises(ValueError):
            load_hook.invoke(None, Path('never-loaded.dll'), 'capture', struct.pack('<3I', 12, 1, 0))

    def test_v2_observer_and_one_to_one_control_have_exact_native_abi(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'work').mkdir()
            catalog = root / 'work/catalog.bin'
            catalog.write_bytes(b'private catalog fixture, no shader programs')
            report = root / 'work/hook.json'
            with patch.object(game, 'ROOT', root):
                control = load_hook.configuration((1280, 720), (1280, 720), report)
                self.assertEqual(len(control), 3132)
                self.assertEqual(struct.unpack_from('<2I', control), (3132, 2))
                self.assertEqual(control[1048:], bytes(2084))
                materials=load_hook.configuration((1920,1080),(1280,720),report,material_flags=7)
                self.assertEqual(struct.unpack_from('<I',materials,1048)[0],14)
                config = load_hook.configuration((1920, 1080), (1280, 720), report,
                    observer_catalog=catalog, observer_sha256=load_hook.digest(catalog))
                self.assertEqual(len(config), 3132)
                self.assertEqual(struct.unpack_from('<I', config, 1048)[0], 1)
                self.assertEqual(config[2076:2108], bytes.fromhex(load_hook.digest(catalog)))
                combined=load_hook.configuration((1920,1080),(1280,720),report,material_flags=7,
                    observer_catalog=catalog,observer_sha256=load_hook.digest(catalog))
                self.assertEqual(struct.unpack_from('<I',combined,1048)[0],15)
                ambient=load_hook.configuration((1920,1080),(1280,720),report,ambient_occlusion=True,
                    observer_catalog=catalog,observer_sha256=load_hook.digest(catalog))
                self.assertEqual(struct.unpack_from('<I',ambient,1048)[0],33)
                with self.assertRaises(ValueError):
                    load_hook.configuration((1920,1080),(1280,720),report,ambient_occlusion=True,shadow_filter=True,
                        observer_catalog=catalog,observer_sha256=load_hook.digest(catalog))
                self.assertTrue(config[2108:].decode('utf-16le').rstrip('\0').endswith('material-observer.private.json'))
                with self.assertRaises(ValueError):
                    load_hook.configuration((1920, 1080), (1280, 720), report,
                        observer_catalog=catalog, observer_sha256='0' * 64)

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
            effects_fixture=build/'supersampling_effects_gpu.exe'
            effects_fixture.write_bytes(b'authored fixture, never run')
            manifest = {'format': 's7-native-graphics-build-v2','source_set':'effects-v2','diagnostic_abi':{'comparison':12},
                        'compiled_capabilities':15,'abi':load_hook.BUILD_ABI,
                        'build_sha256': load_hook.digest(script), 'files': {
                            name: load_hook.digest(root / 'native' / name) for name in load_hook.SOURCES}}
            manifest['files'].update({dll.name: load_hook.digest(dll), fixture.name: load_hook.digest(fixture)})
            manifest['files'][effects_fixture.name]=load_hook.digest(effects_fixture)
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
