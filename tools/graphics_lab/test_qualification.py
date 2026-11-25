"""Synthetic evidence records; no original game, image editing or GPU execution."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zlib

from tools.graphics_lab import qualification as q


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def png(width, height):
    def chunk(name, data):
        return struct.pack('>I', len(data)) + name + data + struct.pack('>I', zlib.crc32(name + data) & 0xffffffff)
    header = struct.pack('>2I5B', width, height, 8, 2, 0, 0, 0)
    pixels = b''.join(b'\0' + bytes(width * 3) for _ in range(height))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b'')


def bmp(width, height):
    pixels = bytes(width * height * 4)
    return (struct.pack('<2sIHHI', b'BM', 54 + len(pixels), 0, 0, 54)
            + struct.pack('<IiiHHIIiiII', 40, width, -height, 1, 32, 0, len(pixels), 0, 0, 0, 0) + pixels)


class QualificationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.work = self.root / 'work'
        self.work.mkdir()
        self.bundle = self.work / 'owned-game'
        self.bin = self.bundle / 'Base/Bin'
        self.bin.mkdir(parents=True)
        self.binaries = {}
        for name in ('Settlers7R.exe', 'uplay_r1_loader.dll', 'd3d9.dll', 'Vision90.dll', 'vBase90.dll'):
            path = self.bin / name
            path.write_bytes(('synthetic ' + name).encode())
            self.binaries['Base/Bin/' + name] = digest(path.read_bytes())
        self.profile = self.bundle / 'LocalData/ProfileRoot/THE SETTLERS 7 - History Edition'
        self.profile.mkdir(parents=True)
        self.save = self.profile / 's79.s7s'
        self.save.write_bytes(b'synthetic original save')
        for name, value in (('ROOT', self.root), ('EDITIONS', {'ubisoft-history': self.binaries['Base/Bin/Settlers7R.exe'],
                                                           'steam-history': 'b' * 64, 'gold': 'c' * 64}),
                            ('DXVK', self.binaries['Base/Bin/d3d9.dll'])):
            p = patch.object(q, name, value)
            p.start()
            self.addCleanup(p.stop)
        self.cases = []
        for case_id, role in (('control', 'control'), ('candidate', 'candidate')):
            self.cases.append(self.fixture(case_id, role))
        self.manifest = self.work / 'qualification.private.json'
        self.data = {'format': q.FORMAT, 'cases': self.cases, 'pairs': [{'control': 'control', 'candidate': 'candidate'}]}
        self.write_manifest()

    def write_manifest(self):
        self.manifest.write_text(json.dumps(self.data), encoding='utf-8')

    def fixture(self, case_id, role):
        case = self.work / case_id
        case.mkdir()
        def emit(relative, data):
            path = case / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(data), encoding='utf-8')
        memory = {'monitor_failed': False, 'recovery_required': False, 'max_pressure': 0, 'samples': 20}
        owned = {'sha256': self.binaries['Base/Bin/Settlers7R.exe'], 'path': str(self.bin / 'Settlers7R.exe'),
                 'pid': 1234 if role == 'control' else 1235, 'parent': 1000, 'native_initialized': True, 'kernel_isolation': 7}
        emit('result.private.json', {'game_started': True, 'shared_profiles_unchanged': True,
             'game_binaries_unchanged': True, 'private_options_restored': True, 'exit_code': 0,
             'owned_timeout_or_stop': False, 'error_type': None, 'cleanup_errors': [], 'memory': memory})
        emit('memory.private.json', memory)
        emit('owned.private.json', owned)
        source = case / 'source/tools/graphics_lab/game.py'
        source.parent.mkdir(parents=True)
        source.write_bytes(b'synthetic frozen controller source')
        emit('checkpoint.private.json', {'files': self.binaries, 'reshade_requested': False,
             'requested_render_size': [4, 4] if role == 'candidate' else [2, 2],
             'sources': {'tools/graphics_lab/game.py': digest(source.read_bytes())}})
        emit('recovery-journal.private.json', {'format': 's7-graphics-lab-recovery-v1', 'state': 'restored',
             'root': str(self.bundle), 'critical': self.binaries})
        emit('portable-before/snapshot.private.json', {'portable': {'files': {
             'THE SETTLERS 7 - History Edition/s79.s7s': {'sha256': digest(self.save.read_bytes())}}}})
        if role == 'candidate':
            emit('hook-state.private.json', {'format': 's7-supersampling-hook-v1', 'lab_only': True,
                 'source': [4, 4], 'selected_target': [4, 4], 'viewport': [4, 4], 'output': [2, 2], 'client': [2, 2],
                 'attached': 1, 'present_calls': 100, 'output_present_calls': 99, 'high_resolution_draws': 900,
                 'indexed_draws': 800, 'fallback_calls': 1, 'swapchain_creations': 1, 'last_failure': 0, 'resize_failure': 0})
            build_files = {}
            for name in q.HOOK_SOURCES | {'WePlaySupersamplingLab.dll', 'supersampling_hook_fixture.exe'}:
                path = case / name
                path.write_bytes(('synthetic compiled snapshot ' + name).encode())
                build_files[name] = digest(path.read_bytes())
            emit('hook-initialized.private.json', {'initialized': True, 'original_entry_guarded': True,
                 'worker_code_rx': True, 'dll_sha256': build_files['WePlaySupersamplingLab.dll']})
            emit('supersampling-hook-build.json', {'format': 's7-supersampling-hook-build-v1',
                 'build_sha256': 'b' * 64, 'files': build_files})
        reader = case / 'reader.py'
        reader.write_bytes(b'synthetic native observation reader checkpoint')
        observation = {'format': 's7-native-scene-observation-v1', 'origin': 'guarded-runtime-read',
             'reader_source': q._ref(reader), 'identity': {k: owned[k] for k in ('pid', 'parent', 'sha256')},
             'renderer_sha256': self.binaries['Base/Bin/d3d9.dll'],
             'render_size': [4, 4] if role == 'candidate' else [2, 2], 'output_size': [2, 2],
             'camera': {'translation': [1, 2, 3], 'look_at': [4, 5, 6], 'fov_degrees': 45},
             'save_sha256': digest(self.save.read_bytes()), 'native_lod_scale': 1.5, 'simulation_pace_ms': 100,
             'frames': [{'turn': 100 + i, 'elapsed_seconds': i * .1, 'sun_azimuth_degrees': 240 + i * .01,
                         'sun_elevation_degrees': 30, 'exposure': 2.6} for i in range(3)]}
        captures = []
        for stage in ('source', 'output'):
            path = case / (stage + '.png')
            size = 4 if role == 'candidate' and stage == 'source' else 2
            path.write_bytes(png(size, size))
            captures.append((stage, path))
            for frame in observation['frames']:
                frame[stage + '_image_sha256'] = digest(path.read_bytes())
        emit('scene.private.json', observation)
        return q.record_case(case, case_id=case_id, edition='ubisoft-history', role=role,
                             captures=captures, observation=case / 'scene.private.json')

    def change_artifact(self, case_index, role, mutate):
        reference = self.data['cases'][case_index]['artifacts'][role]
        path = Path(reference['path'])
        data = json.loads(path.read_text())
        mutate(data)
        path.write_text(json.dumps(data))
        self.data['cases'][case_index]['artifacts'][role] = q._ref(path)
        self.write_manifest()

    def test_matching_file_backed_records_never_automatically_qualify_visuals_or_release(self):
        result = q.aggregate(self.manifest)
        self.assertTrue(all(c['runtime_records_consistent'] for c in result['cases']))
        self.assertTrue(result['pairs'][0]['metadata_fields_comparable'])
        self.assertFalse(result['pairs'][0]['measurements_comparable'])
        self.assertFalse(result['pairs'][0]['visible_camera_validated'])
        self.assertFalse(result['visual_qualified'])
        self.assertFalse(result['release_qualified'])
        self.assertEqual(result['edition_coverage']['gold']['consistent_native_runs'], [])
        self.assertEqual(result['edition_coverage']['ubisoft-history']['consistent_ssaa_runs'], ['candidate'])
        self.assertTrue(result['edition_coverage']['ubisoft-history']['actual_lifecycle_and_fallback_review_required'])

    def test_synthetic_cases_never_count_toward_native_edition_coverage(self):
        self.data['cases'][1]['kind'] = 'synthetic'
        self.write_manifest()
        result = q.aggregate(self.manifest)
        self.assertFalse(result['cases'][1]['runtime_records_consistent'])
        self.assertFalse(result['pairs'][0]['measurements_comparable'])

    def test_low_resolution_scene_cannot_pass_just_because_output_is_correct(self):
        self.change_artifact(1, 'hook', lambda data: data.update(selected_target=[2, 2], viewport=[2, 2]))
        result = q.aggregate(self.manifest)
        self.assertFalse(result['cases'][1]['runtime_records_consistent'])
        self.assertIn('selected_target was not the requested high-resolution scene', result['cases'][1]['rejected'])

    def test_graceful_terminal_detach_preserves_historical_attachment_evidence(self):
        self.change_artifact(1, 'hook', lambda data: data.update(attached=0,hook_detached=1,terminal_cleanup=1,window_shutdowns=1))
        self.assertTrue(q.aggregate(self.manifest)['cases'][1]['native_ssaa_records_consistent'])
        self.change_artifact(1, 'hook', lambda data: data.update(hook_detached=0))
        result=q.aggregate(self.manifest)
        self.assertFalse(result['cases'][1]['native_ssaa_records_consistent'])
        self.assertIn('no active attachment or verified graceful terminal detach',result['cases'][1]['rejected'])

    def test_verified_gold_rotating_autosave_requires_before_image_and_never_waives_manual_changes(self):
        relative=q.GOLD_SAVE_ROLE_PROOF['rotating_autosave_relative']
        auto=self.bundle/'LocalData/ProfileRoot'/relative
        auto.parent.mkdir(parents=True,exist_ok=True)
        old=b'synthetic older owned autosave';auto.write_bytes(old)
        before={'THE SETTLERS 7 - History Edition/s79.s7s':{'sha256':digest(self.save.read_bytes())},
                relative:{'sha256':digest(old),'size':len(old)}}
        backup=self.work/'backup';target=backup/relative;target.parent.mkdir(parents=True);target.write_bytes(old)
        auto.write_bytes(b'synthetic newer owned autosave')
        result,_=q._save_classification(self.bundle,backup,before,True)
        self.assertFalse(result['all_original_private_saves_unchanged'])
        self.assertTrue(result['manual_original_private_saves_unchanged'])
        self.assertTrue(result['private_rotating_autosave_changes'][0]['before_image_identity_verified'])
        self.assertEqual(auto.read_bytes(),b'synthetic newer owned autosave')
        self.save.write_bytes(b'changed manual original')
        result,_=q._save_classification(self.bundle,backup,before,True)
        self.assertFalse(result['manual_original_private_saves_unchanged'])
        self.assertIn('THE SETTLERS 7 - History Edition/s79.s7s',result['unapproved_original_private_save_changes'])
        target.unlink()
        result,_=q._save_classification(self.bundle,backup,before,True)
        self.assertIn(relative,result['unapproved_original_private_save_changes'])
        result,_=q._save_classification(self.bundle,backup,before,False)
        self.assertEqual(result['private_rotating_autosave_changes'],[])

    def test_verified_history_slot_is_edition_scoped_and_requires_exact_retained_before_image(self):
        relative=q.UBI_SAVE_ROLE_PROOF['rotating_autosave_relative']
        auto=self.bundle/'LocalData/ProfileRoot'/relative;auto.parent.mkdir(parents=True,exist_ok=True)
        auto.write_bytes(b'newer owned progress')
        old=b'older diagnostic before image';before={relative:{'sha256':digest(old),'size':len(old)}}
        backup=self.work/'history-backup';target=backup/relative;target.parent.mkdir(parents=True);target.write_bytes(old)
        result,_=q._save_classification(self.bundle,backup,before,True,relative)
        self.assertFalse(result['all_original_private_saves_unchanged'])
        self.assertTrue(result['manual_original_private_saves_unchanged'])
        result,_=q._save_classification(self.bundle,backup,before,True)
        self.assertIn(relative,result['unapproved_original_private_save_changes'])
        target.write_bytes(b'unrelated backup')
        result,_=q._save_classification(self.bundle,backup,before,True,relative)
        self.assertIn(relative,result['unapproved_original_private_save_changes'])
        self.assertEqual(auto.read_bytes(),b'newer owned progress')

    def steam_observation(self):
        from tools.graphics_lab import save_role_read
        case=self.work/'steam-role-read';case.mkdir()
        reader=case/'save-role.private.reader.py';reader.write_bytes(Path(save_role_read.__file__).read_bytes())
        owned={'pid':123,'parent':456,'sha256':q.EDITIONS['steam-history']}
        value=deepcopy(q.STEAM_SLOT_OBSERVATION)
        value.update(format='s7-original-steam-save-role-read-v1',edition='steam-history',read_only=True,
            automatic_classification_accepted=False,restoration_performed=False,identity=dict(owned),
            runtime_mapping='original registration plus complete verified instruction template',
            observer_started_monotonic_ns=100,observer_finished_monotonic_ns=200,reader_source=q._ref(reader))
        return case,reader,owned,value

    def test_steam_slot_observation_requires_reviewed_reader_identity_and_exact_runtime_code(self):
        case,reader,owned,value=self.steam_observation()
        q._verify_steam_slot_read(value,owned,case)
        for key,invalid in [('first',31),('callback_rva',0x13d590),('template_fnv64','0'*16),
                            ('normalized_callback_sha256','0'*64),('automatic_classification_accepted',True)]:
            changed=deepcopy(value);changed[key]=invalid
            with self.assertRaises(ValueError):q._verify_steam_slot_read(changed,owned,case)
        changed=deepcopy(value);changed['registration']['name_slot_rva']+=4
        with self.assertRaises(ValueError):q._verify_steam_slot_read(changed,owned,case)
        changed=deepcopy(value);changed['identity']['pid']+=1
        with self.assertRaises(ValueError):q._verify_steam_slot_read(changed,owned,case)
        reader.write_bytes(b'changed retained reader')
        with self.assertRaises(ValueError):q._verify_steam_slot_read(value,owned,case)

    def test_steam_role_policy_requires_closed_actual_case_before_original_archive_access(self):
        case,reader,owned,value=self.steam_observation()
        path=case/'save-role-read.private.json';path.write_text(json.dumps(value))
        with self.assertRaises(FileNotFoundError):q.steam_save_role_proof(self.bundle,path,owned=owned)
        (case/'result.private.json').write_text(json.dumps({'game_started':True,'exit_code':None}))
        with self.assertRaisesRegex(ValueError,'completed actual game'):q.steam_save_role_proof(self.bundle,path,owned=owned)

    def test_hook_build_identity_must_match_the_initialized_binary(self):
        self.change_artifact(1, 'hook_build', lambda data: data['files'].update(WePlaySupersamplingLab_dll='different'))
        self.change_artifact(1, 'hook_init', lambda data: data.update(dll_sha256='c' * 64))
        result = q.aggregate(self.manifest)
        self.assertFalse(result['cases'][1]['native_ssaa_records_consistent'])
        self.assertIn('hook initialization/build manifest mismatch', result['cases'][1]['rejected'])

    def effects_build(self):
        reference = self.data['cases'][1]['artifacts']['hook_build']
        directory = Path(reference['path']).parent
        def update(data):
            data.update(format='s7-native-graphics-build-v2', source_set='effects-v1',
                        compiled_capabilities=15, abi=dict(q.EFFECTS_BUILD_ABI))
            for name in (q.EFFECTS_HOOK_SOURCES | q.EFFECTS_HOOK_BINARIES) - data['files'].keys():
                path = directory / name
                path.write_bytes(('synthetic effects compiled snapshot ' + name).encode())
                data['files'][name] = digest(path.read_bytes())
        self.change_artifact(1, 'hook_build', update)
        return directory

    def test_early_engine_cleanup_requires_complete_original_returns_and_clean_exit(self):
        self.effects_build()
        self.change_artifact(1, 'hook', lambda data: data.update(attached=0,hook_detached=1,
            terminal_cleanup=1,window_shutdowns=0,engine_deinit_calls=2,engine_deinit_returned=2,
            engine_deinit_rejected=0,engine_deinit_cleanup=1,shutdown_stage=3))
        self.assertTrue(q.aggregate(self.manifest)['cases'][1]['native_ssaa_records_consistent'])
        for key,value in [('engine_deinit_returned',1),('engine_deinit_rejected',1),
                          ('engine_deinit_cleanup',0),('shutdown_stage',2)]:
            original=2 if key=='engine_deinit_returned' else 0 if key=='engine_deinit_rejected' else 1 if key=='engine_deinit_cleanup' else 3
            self.change_artifact(1,'hook',lambda data,key=key,value=value:data.update({key:value}))
            self.assertFalse(q.aggregate(self.manifest)['cases'][1]['native_ssaa_records_consistent'])
            self.change_artifact(1,'hook',lambda data,key=key,original=original:data.update({key:original}))
        self.change_artifact(1,'result',lambda data:data.update(exit_code=1))
        self.assertFalse(q.aggregate(self.manifest)['cases'][1]['native_ssaa_records_consistent'])

    def test_effects_build_checks_19_frozen_sources_and_three_binaries(self):
        self.effects_build()
        self.assertEqual(len(q.EFFECTS_HOOK_SOURCES), 19)
        result = q.aggregate(self.manifest)
        self.assertTrue(result['cases'][1]['runtime_records_consistent'])
        self.assertFalse(result['visual_qualified'])
        self.assertFalse(result['release_qualified'])
        self.change_artifact(1, 'hook_build', lambda data: data['abi'].update(effects=4136))
        self.assertIn('hook initialization/build manifest mismatch',
                      q.aggregate(self.manifest)['cases'][1]['rejected'])

    def test_effects_build_rejects_changed_snapshot_and_missing_gpu_program(self):
        directory = self.effects_build()
        (directory / 'material_shader_effects.cpp').write_bytes(b'changed after compile')
        self.assertIn('compiled hook source/binary snapshot changed',
                      q.aggregate(self.manifest)['cases'][1]['rejected'])
        self.change_artifact(1, 'hook_build', lambda data: data['files'].pop('supersampling_effects_gpu.exe'))
        self.assertIn('incomplete native hook build source identities',
                      q.aggregate(self.manifest)['cases'][1]['rejected'])

    def test_effects_build_unknown_source_set_and_capabilities_rejected(self):
        self.effects_build()
        self.change_artifact(1, 'hook_build', lambda data: data.update(source_set='effects-v2'))
        self.assertFalse(q.aggregate(self.manifest)['cases'][1]['runtime_records_consistent'])
        self.change_artifact(1, 'hook_build', lambda data: data.update(source_set='effects-v1', compiled_capabilities=31))
        self.assertFalse(q.aggregate(self.manifest)['cases'][1]['runtime_records_consistent'])

    def comparison_build(self):
        directory=self.effects_build()
        def update(data):
            data.update(source_set='effects-v2',diagnostic_abi={'comparison':12})
            for name in q.COMPARISON_HOOK_SOURCES-data['files'].keys():
                path=directory/name;path.write_bytes(('synthetic comparison snapshot '+name).encode())
                data['files'][name]=digest(path.read_bytes())
        self.change_artifact(1,'hook_build',update)
        return directory

    def modern_build(self):
        directory=self.comparison_build()
        def update(data):
            data.update(source_set='effects-v3',abi=dict(q.MODERN_BUILD_ABI))
            for name in q.MODERN_HOOK_SOURCES-data['files'].keys():
                path=directory/name;path.write_bytes(('synthetic modern snapshot '+name).encode())
                data['files'][name]=digest(path.read_bytes())
        self.change_artifact(1,'hook_build',update)
        return directory

    def water_build(self):
        directory=self.modern_build()
        def update(data):
            data.update(source_set='effects-v4',abi=dict(q.WATER_BUILD_ABI))
            for name in q.WATER_HOOK_SOURCES-data['files'].keys():
                path=directory/name;path.write_bytes(('synthetic water snapshot '+name).encode())
                data['files'][name]=digest(path.read_bytes())
        self.change_artifact(1,'hook_build',update)
        return directory

    def resize_candidate(self,source_size,output_size):
        case=self.data['cases'][1]
        self.change_artifact(1,'checkpoint',lambda data:data.update(requested_render_size=[source_size]*2))
        self.change_artifact(1,'hook',lambda data:data.update(source=[source_size]*2,selected_target=[source_size]*2,
            viewport=[source_size]*2,output=[output_size]*2,client=[output_size]*2))
        for capture in case['captures']:
            path=Path(capture['file']['path']);side=source_size if capture['stage']=='source' else output_size
            path.write_bytes(png(side,side));capture['file']=q._ref(path)
        path=Path(case['observation']['path']);obs=json.loads(path.read_text())
        obs.update(render_size=[source_size]*2,output_size=[output_size]*2)
        for frame in obs['frames']:
            for capture in case['captures']:frame[capture['stage']+'_image_sha256']=capture['file']['sha256']
        path.write_text(json.dumps(obs));case['observation']=q._ref(path)

    def test_water_v4_28_immutable_sources_and_water_abi(self):
        self.water_build()
        self.assertEqual(len(q.WATER_HOOK_SOURCES),28)
        self.assertTrue(q.aggregate(self.manifest)['cases'][1]['runtime_records_consistent'])
        self.change_artifact(1,'hook_build',lambda data:data['abi'].update(water=1060))
        self.assertFalse(q.aggregate(self.manifest)['cases'][1]['runtime_records_consistent'])

    def terrain_macro_build(self):
        directory=self.water_build()
        def update(data):
            data.update(source_set='effects-v5')
            for name in q.TERRAIN_MACRO_HOOK_SOURCES-data['files'].keys():
                path=directory/name;path.write_bytes(('synthetic macro snapshot '+name).encode())
                data['files'][name]=digest(path.read_bytes())
        self.change_artifact(1,'hook_build',update)
        return directory

    def test_terrain_v5_requires_exact29_sources_and_unchanged_abi(self):
        self.terrain_macro_build()
        self.assertEqual(len(q.TERRAIN_MACRO_HOOK_SOURCES),29)
        result=q.aggregate(self.manifest)
        self.assertTrue(result['cases'][1]['runtime_records_consistent'])
        self.assertFalse(result['visual_qualified'])
        self.change_artifact(1,'hook_build',lambda data:data['abi'].update(effects_v2=6184))
        self.assertFalse(q.aggregate(self.manifest)['cases'][1]['runtime_records_consistent'])

    def test_terrain_v5_missing_changed_or_mislabeled_guard_rejected(self):
        directory=self.terrain_macro_build()
        path=directory/'terrain_macro_guard.h'
        original=path.read_bytes();path.write_bytes(original+b'changed')
        self.assertFalse(q.aggregate(self.manifest)['cases'][1]['runtime_records_consistent'])
        path.write_bytes(original)
        self.change_artifact(1,'hook_build',lambda data:data.update(source_set='effects-v4'))
        self.assertFalse(q.aggregate(self.manifest)['cases'][1]['runtime_records_consistent'])
        self.change_artifact(1,'hook_build',lambda data:(data.update(source_set='effects-v5'),data['files'].pop('terrain_macro_guard.h')))
        self.assertFalse(q.aggregate(self.manifest)['cases'][1]['runtime_records_consistent'])

    def test_terrain_v5_keeps_terminal_failure_rejection(self):
        self.terrain_macro_build()
        self.change_artifact(1,'result',lambda data:data.update(exit_code=0xc0000005))
        self.assertFalse(q.aggregate(self.manifest)['cases'][1]['runtime_records_consistent'])

    def shadow_build(self):
        directory=self.terrain_macro_build()
        def update(data):
            data.update(source_set='effects-v6',diagnostic_abi={'comparison':12,'shadow':12})
            for name in q.SHADOW_HOOK_SOURCES-data['files'].keys():
                path=directory/name;path.write_bytes(('synthetic shadow snapshot '+name).encode())
                data['files'][name]=digest(path.read_bytes())
        self.change_artifact(1,'hook_build',update)
        return directory

    def test_shadow_v6_requires32_sources_and_separate_fixed_diagnostic_abi(self):
        self.shadow_build()
        self.assertEqual(len(q.SHADOW_HOOK_SOURCES),32)
        self.assertTrue(q.aggregate(self.manifest)['cases'][1]['runtime_records_consistent'])
        self.assertFalse(q.aggregate(self.manifest)['visual_qualified'])
        for abi in ({'comparison':12},{'comparison':12,'shadow':16},{'comparison':12,'shadow':12,'arbitrary':12}):
            self.change_artifact(1,'hook_build',lambda data:data.update(diagnostic_abi=abi))
            self.assertFalse(q.aggregate(self.manifest)['cases'][1]['runtime_records_consistent'])

    def test_shadow_v6_changed_missing_or_mislabeled_sources_rejected(self):
        directory=self.shadow_build()
        path=directory/'shadow2048_filter_policy.h';original=path.read_bytes();path.write_bytes(original+b'changed')
        self.assertFalse(q.aggregate(self.manifest)['cases'][1]['runtime_records_consistent'])
        path.write_bytes(original)
        self.change_artifact(1,'hook_build',lambda data:data.update(source_set='effects-v5',diagnostic_abi={'comparison':12}))
        self.assertFalse(q.aggregate(self.manifest)['cases'][1]['runtime_records_consistent'])
        self.change_artifact(1,'hook_build',lambda data:(data.update(source_set='effects-v6',diagnostic_abi={'comparison':12,'shadow':12}),data['files'].pop('shadow_filter_effects.cpp')))
        self.assertFalse(q.aggregate(self.manifest)['cases'][1]['runtime_records_consistent'])

    def test_shadow_v6_diagnostic_intent_is_not_release_capability(self):
        self.shadow_build()
        self.change_artifact(1,'hook_build',lambda data:data.update(compiled_capabilities=31))
        self.assertFalse(q.aggregate(self.manifest)['cases'][1]['runtime_records_consistent'])

    @staticmethod
    def water_record(on=True):
        return {'frame':80,'requested':int(on),'enabled':int(on),'configured':int(on),'epoch':2 if on else 1,
            'faulted':0,'unsafe':0,'completed_applications':3 if on else 0,'completed_restorations':3 if on else 0,
            'last_applied_frame':80 if on else 0,'last_restored_frame':80 if on else 0,'shader_creations':1 if on else 0,
            'shader_failures':0,'set_failures':0,'restore_failures':0,'exceptions':0,'pending_original_refs':0}

    def test_water_state_requires_normalized_same_frame_and_restored_success(self):
        good=self.water_record();frame={'frame':80,'water':good}
        self.assertEqual(q._water_record(frame)[1:],(True,False))
        self.assertEqual(q._water_record({'frame':80,'water':self.water_record(False)})[1:],(False,True))
        for key,value in [('completed_restorations',2),('last_applied_frame',79),('faulted',1),
                          ('pending_original_refs',1),('set_failures',1),('shader_creations',0)]:
            changed=deepcopy(good);changed[key]=value
            self.assertFalse(q._water_record({'frame':80,'water':changed})[1])
        changed=deepcopy(good);changed['frame']=79
        with self.assertRaises(ValueError):q._water_record({'frame':80,'water':changed})
        changed=deepcopy(good);changed['variant']=3
        with self.assertRaises(ValueError):q._water_record({'frame':80,'water':changed})

    def test_water_variant_mapping_is_fixed_and_never_falls_back_from_unknown_pin(self):
        self.assertEqual(q._water_variant(q.WATER_PROGRAM_SHA256),(1,q.WATER_RECIPE))
        self.assertEqual(q._water_variant(q.WATER_BALANCED_PROGRAM_SHA256),(2,q.WATER_BALANCED_RECIPE))
        with self.assertRaises(ValueError):q._water_variant('f'*64)
        self.assertTrue(q._water_stage_format({'format':'s7-recipient-water-stage-v1'},1))
        self.assertFalse(q._water_stage_format({'format':'s7-recipient-water-stage-v1'},2))
        self.assertTrue(q._water_stage_format({'format':'s7-recipient-water-variant-stage-v1','variant':'B','variant_id':2},2))
        self.assertFalse(q._water_stage_format({'format':'s7-recipient-water-variant-stage-v1','variant':'A','variant_id':2},2))

    def test_separate_water_end_to_end_binding_bootstrap_and_no_legacy_overlap(self):
        from unittest.mock import patch
        directory=self.water_build();case=self.data['cases'][1];self.resize_candidate(1024,512)
        self.add_native_capture(source_size=1024,output_size=512)
        self.change_artifact(1,'checkpoint',lambda data:data.update(water_program_requested=True,material_effects_requested=['improved_water']))
        program=struct.pack('<I',0xffff0300)+bytes(3840-8)+struct.pack('<I',0xffff)  # Authored identity fixture, never submitted to a GPU.
        pin=digest(program)
        with patch.object(q,'WATER_PROGRAM_SHA256',pin):
            target=directory/f'LocalData/MaterialShaders/{q.WATER_RECIPE}/{pin}.ps.bin';target.parent.mkdir(parents=True);target.write_bytes(program)
            case['artifacts']['water_program']=q._ref(target)
            encoded=str(target).encode('utf-16-le');raw=struct.pack('<5I',1076,1,1024,1024,1)+encoded.ljust(1024,b'\0')+bytes.fromhex(pin)
            path=directory/'water-config.bin';path.write_bytes(raw);case['artifacts']['water_config']=q._ref(path)
            stage={'format':'s7-recipient-water-stage-v1','edition':'history','render':[1024,1024],
                'config_bytes':1076,'recipe':q.WATER_RECIPE,'program_path':str(target),'program_sha256':pin,
                'program_bytes':3840,'source_resources':dict(q.WATER_SOURCES),'private_case':True,'no_legacy_river_scaling_permitted':True}
            path=directory/'water-stage.private.json';path.write_text(json.dumps(stage));case['artifacts']['water_stage']=q._ref(path)
            raw=struct.pack('<5I4f',4132,1,0,1024,1024,1.15,.94,1.2,1.1)+bytes(4132-36)
            path=directory/'effects-config.bin';path.write_bytes(raw);case['artifacts']['effects_config']=q._ref(path)
            initialized=json.loads(Path(case['artifacts']['hook_init']['path']).read_text())
            for role,export in [('water_configured','WePlayNativeGraphicsConfigureWater'),('effects_configured','WePlayNativeGraphicsConfigureEffects')]:
                path=directory/q.ARTIFACTS[role];path.write_text(json.dumps({'succeeded':True,'worker_code_rx':True,
                    'original_entry_guarded':True,'dll_sha256':initialized['dll_sha256'],'export':export}));case['artifacts'][role]=q._ref(path)
            path=Path(case['native_captures'][0]['path']);frame=json.loads(path.read_text())
            epoch=dict.fromkeys(q.EFFECT_EPOCH_FIELDS,0);epoch.update(effects_requested=1,effects_configured=1,
                effects_revision=1,effects_drawn_revision=1,effects_frame_has_geometry=1)
            frame.update(epoch,effects_config_version=1,water=self.water_record());path.write_text(json.dumps(frame))
            case['native_captures'][0]=q._ref(path);self.write_manifest()
            result=q.aggregate(self.manifest);row=result['cases'][1]
            self.assertTrue(row['runtime_records_consistent'],row['rejected'])
            self.assertTrue(row['native_frame_pairs'][0]['water_shader_replacement_recorded'])
            self.assertTrue(row['native_frame_pairs'][0]['water_only_configuration_recorded'])
            self.assertFalse(result['visual_qualified'])
            frame['effects_active']=1;path.write_text(json.dumps(frame));case['native_captures'][0]=q._ref(path);self.write_manifest()
            row=q.aggregate(self.manifest)['cases'][1]
            self.assertFalse(row['native_frame_pairs'][0]['water_shader_replacement_recorded'])
            self.assertIn('legacy River VS mutation overlaps separate WaterProgram',row['rejected'])
            # Same immutable transport/source schema, explicitly different pinned
            # recipe. B must have the native measured variant2, not an A label.
            balanced=bytearray(program);balanced[100]=1;balanced=bytes(balanced);balanced_pin=digest(balanced)
            with patch.object(q,'WATER_BALANCED_PROGRAM_SHA256',balanced_pin):
                target_b=directory/f'LocalData/MaterialShaders/{q.WATER_BALANCED_RECIPE}/{balanced_pin}.ps.bin'
                target_b.parent.mkdir(parents=True);target_b.write_bytes(balanced);case['artifacts']['water_program']=q._ref(target_b)
                encoded=str(target_b).encode('utf-16-le');raw=struct.pack('<5I',1076,1,1024,1024,1)+encoded.ljust(1024,b'\0')+bytes.fromhex(balanced_pin)
                config_path=Path(case['artifacts']['water_config']['path']);config_path.write_bytes(raw);case['artifacts']['water_config']=q._ref(config_path)
                stage.update(format='s7-recipient-water-variant-stage-v1',variant='B',variant_id=2,
                    recipe=q.WATER_BALANCED_RECIPE,program_path=str(target_b),program_sha256=balanced_pin)
                stage_path=Path(case['artifacts']['water_stage']['path']);stage_path.write_text(json.dumps(stage));case['artifacts']['water_stage']=q._ref(stage_path)
                frame['effects_active']=0;frame['water']['variant']=2;path.write_text(json.dumps(frame));case['native_captures'][0]=q._ref(path);self.write_manifest()
                row=q.aggregate(self.manifest)['cases'][1]
                self.assertTrue(row['runtime_records_consistent'],row['rejected'])
                self.assertTrue(row['native_frame_pairs'][0]['water_shader_replacement_recorded'])
                self.assertEqual(row['water_configuration']['variant'],2)
                frame['water']['variant']=1;path.write_text(json.dumps(frame));case['native_captures'][0]=q._ref(path);self.write_manifest()
                row=q.aggregate(self.manifest)['cases'][1]
                self.assertFalse(row['native_frame_pairs'][0]['water_shader_replacement_recorded'])
                self.assertIn('native water variant/configuration mismatch',row['rejected'])

    def test_modern_v3_25_immutable_sources_and_effects_v2_abi(self):
        self.modern_build()
        self.assertEqual(len(q.MODERN_HOOK_SOURCES),25)
        self.assertTrue(q.aggregate(self.manifest)['cases'][1]['runtime_records_consistent'])
        self.change_artifact(1,'hook_build',lambda data:data['abi'].update(effects_v2=4132))
        self.assertFalse(q.aggregate(self.manifest)['cases'][1]['runtime_records_consistent'])

    def test_material_transport_versions_and_bounds(self):
        def config(size,version):
            return struct.pack('<5I4f',size,version,6,512,512,1.15,.94,1.2,1.1)+bytes(size-36)
        self.assertEqual(q._material_configuration(config(6180,2))['version'],2)
        self.assertEqual(q._material_configuration(config(4132,1))['version'],1)
        for raw in (config(6180,1),config(4132,2),config(6180,2)[:-1]):
            with self.assertRaises(ValueError):q._material_configuration(raw)
        bad=bytearray(config(6180,2));struct.pack_into('<f',bad,20,float('nan'))
        with self.assertRaises(ValueError):q._material_configuration(bytes(bad))
        bad=bytearray(config(6180,2));bad[40]=1
        with self.assertRaises(ValueError):q._material_configuration(bytes(bad))

    def test_native_v2_material_capture_requires_matching_config_receipt_and_version(self):
        directory=self.modern_build();case=self.data['cases'][1]
        self.change_artifact(1,'checkpoint',lambda data:data.update(requested_render_size=[512,512]))
        self.change_artifact(1,'hook',lambda data:data.update(source=[512,512],selected_target=[512,512],
            viewport=[512,512],output=[256,256],client=[256,256]))
        for capture in case['captures']:
            path=Path(capture['file']['path']);side=512 if capture['stage']=='source' else 256
            path.write_bytes(png(side,side));capture['file']=q._ref(path)
        obs_path=Path(case['observation']['path']);obs=json.loads(obs_path.read_text())
        obs.update(render_size=[512,512],output_size=[256,256])
        for frame in obs['frames']:
            for capture in case['captures']:frame[capture['stage']+'_image_sha256']=capture['file']['sha256']
        obs_path.write_text(json.dumps(obs));case['observation']=q._ref(obs_path)
        self.add_native_capture(source_size=512,output_size=256)
        raw=struct.pack('<5I4f',6180,2,6,512,512,1.15,.94,1.2,1.1)+bytes(6180-36)
        config=directory/'effects-config.bin';config.write_bytes(raw);case['artifacts']['effects_config']=q._ref(config)
        initialized=json.loads(Path(case['artifacts']['hook_init']['path']).read_text())
        receipt=directory/'effects-configured.private.json';receipt.write_text(json.dumps({
            'succeeded':True,'worker_code_rx':True,'original_entry_guarded':True,
            'export':'WePlayNativeGraphicsConfigureEffects','dll_sha256':initialized['dll_sha256']}))
        case['artifacts']['effects_configured']=q._ref(receipt)
        path=Path(case['native_captures'][0]['path']);frame=json.loads(path.read_text())
        epoch=dict.fromkeys(q.EFFECT_EPOCH_FIELDS,0);epoch.update(effects_requested=6,effects_configured=1,
            effects_active=2,effects_revision=2,effects_drawn_active=2,effects_drawn_revision=2,effects_frame_has_geometry=1)
        frame.update(epoch,effects_config_version=2);path.write_text(json.dumps(frame));case['native_captures'][0]=q._ref(path);self.write_manifest()
        result=q.aggregate(self.manifest);row=result['cases'][1]
        self.assertTrue(row['runtime_records_consistent'],row['rejected'])
        self.assertTrue(row['native_frame_pairs'][0]['material_configuration_bound'])
        self.assertTrue(row['native_frame_pairs'][0]['material_config_version_measured'])
        self.assertEqual(row['material_configuration']['version'],2)
        self.assertFalse(result['visual_qualified'])
        frame['effects_config_version']=1;path.write_text(json.dumps(frame));case['native_captures'][0]=q._ref(path);self.write_manifest()
        self.assertIn('native material version/configuration mismatch',q.aggregate(self.manifest)['cases'][1]['rejected'])
        frame['effects_config_version']=2;path.write_text(json.dumps(frame));case['native_captures'][0]=q._ref(path);self.write_manifest()
        self.change_artifact(1,'effects_configured',lambda data:data.update(dll_sha256='f'*64))
        self.assertIn('material configuration/source receipt binding missing',q.aggregate(self.manifest)['cases'][1]['rejected'])

    def test_comparison_build_requires_exact_22_sources_and_diagnostic_abi(self):
        self.comparison_build()
        self.assertEqual(len(q.COMPARISON_HOOK_SOURCES),22)
        self.assertTrue(q.aggregate(self.manifest)['cases'][1]['runtime_records_consistent'])
        self.change_artifact(1,'hook_build',lambda data:data.update(diagnostic_abi={'comparison':16}))
        self.assertFalse(q.aggregate(self.manifest)['cases'][1]['runtime_records_consistent'])

    @staticmethod
    def comparison_record():
        return {'diagnostic_only':True,'frame':80,'requested':1,'active':1,'pinned':1,
                'faulted':0,'unsafe':0,'epoch':2,'pinned_ps80':[.2,.2,.2,.2],
                'pin_committed_frame':10,'completed_frame_applications':5,
                'completed_frame_restorations':5,'last_applied_frame':80,'last_restored_frame':80}

    def test_comparison_pin_alone_stale_counts_fault_or_nonfinite_cannot_qualify(self):
        record=self.comparison_record()
        self.assertTrue(q._comparison_record({'frame':80,'comparison':record})[1])
        for key,value in [('completed_frame_applications',0),('completed_frame_restorations',4),
                          ('faulted',1),('unsafe',1),('last_applied_frame',79)]:
            changed=deepcopy(record);changed[key]=value
            self.assertFalse(q._comparison_record({'frame':80,'comparison':changed})[1])
        for key,value in [('frame',79),('epoch',0),('pinned_ps80',[0,0,0,float('nan')])]:
            changed=deepcopy(record);changed[key]=value
            with self.assertRaises(ValueError):q._comparison_record({'frame':80,'comparison':changed})

    def test_diagnostic_comparison_requires_source_request_observe_and_configured_dll_receipt(self):
        directory=self.comparison_build();self.add_native_capture()
        self.change_artifact(1,'checkpoint',lambda data:data.update(comparison_uv_requested=True))
        self.change_artifact(1,'hook_init',lambda data:data.update(observer_catalog_sha256='c'*64))
        initialized=json.loads(Path(self.data['cases'][1]['artifacts']['hook_init']['path']).read_text())
        configured={'succeeded':True,'worker_code_rx':True,'original_entry_guarded':True,
                    'dll_sha256':initialized['dll_sha256'],'export':'WePlayNativeGraphicsConfigureComparison'}
        path=directory/'comparison-configured.private.json';path.write_text(json.dumps(configured))
        self.data['cases'][1]['artifacts']['comparison_configured']=q._ref(path)
        capture=Path(self.data['cases'][1]['native_captures'][0]['path'])
        frame=json.loads(capture.read_text());frame['comparison']=self.comparison_record();capture.write_text(json.dumps(frame))
        self.data['cases'][1]['native_captures'][0]=q._ref(capture);self.write_manifest()
        result=q.aggregate(self.manifest);row=result['cases'][1]
        self.assertTrue(row['runtime_records_consistent'])
        self.assertTrue(row['native_frame_pairs'][0]['draw_local_ps80_isolation_recorded'])
        self.assertFalse(result['visual_qualified'])
        self.change_artifact(1,'checkpoint',lambda data:data.update(comparison_uv_requested=False))
        row=q.aggregate(self.manifest)['cases'][1]
        self.assertFalse(row['native_frame_pairs'][0]['draw_local_ps80_isolation_recorded'])
        self.assertIn('unsupported or unbound diagnostic comparison record',row['rejected'])
        self.change_artifact(1,'checkpoint',lambda data:data.update(comparison_uv_requested=True))
        self.change_artifact(1,'hook_init',lambda data:data.pop('observer_catalog_sha256'))
        self.assertIn('diagnostic comparison request/configuration binding missing',q.aggregate(self.manifest)['cases'][1]['rejected'])
        self.change_artifact(1,'hook_init',lambda data:data.update(observer_catalog_sha256='c'*64))
        self.change_artifact(1,'comparison_configured',lambda data:data.update(dll_sha256='b'*64))
        row=q.aggregate(self.manifest)['cases'][1]
        self.assertFalse(row['native_frame_pairs'][0]['draw_local_ps80_isolation_recorded'])
        self.assertIn('diagnostic comparison request/configuration binding missing',row['rejected'])

    def test_changed_artifact_hash_is_rejected_without_rebinding(self):
        path = Path(self.data['cases'][1]['artifacts']['hook']['path'])
        path.write_bytes(b'changed receipt')
        result = q.aggregate(self.manifest)
        self.assertFalse(result['cases'][1]['runtime_records_consistent'])
        self.assertIn('hook: ValueError', result['cases'][1]['rejected'])

    def test_missing_capture_and_operator_notes_do_not_establish_a_comparable_pair(self):
        self.data['cases'][1]['captures'] = []
        path = Path(self.data['cases'][1]['observation']['path'])
        data = json.loads(path.read_text())
        data['origin'] = 'operator-notes'
        path.write_text(json.dumps(data))
        self.data['cases'][1]['observation'] = q._ref(path)
        self.write_manifest()
        result = q.aggregate(self.manifest)
        self.assertFalse(result['pairs'][0]['measurements_comparable'])
        self.assertIn('native high-resolution source image', result['cases'][1]['missing'])

    def test_camera_save_lod_sun_and_cadence_must_match(self):
        for key, replacement in (('camera', {'translation': [9, 2, 3], 'look_at': [4, 5, 6], 'fov_degrees': 45}),
                                 ('save_sha256', 'b' * 64), ('native_lod_scale', 2.0),
                                 ('simulation_pace_ms', 5)):
            with self.subTest(key=key):
                data = deepcopy(self.data)
                path = Path(data['cases'][1]['observation']['path'])
                original = path.read_bytes()
                observation = json.loads(original)
                observation[key] = replacement
                path.write_text(json.dumps(observation))
                data['cases'][1]['observation'] = q._ref(path)
                self.manifest.write_text(json.dumps(data))
                self.assertFalse(q.aggregate(self.manifest)['pairs'][0]['measurements_comparable'])
                path.write_bytes(original)
        self.write_manifest()

    def test_memory_failure_force_stop_and_original_save_change_are_rejected(self):
        self.change_artifact(1, 'result', lambda data: data.update(owned_timeout_or_stop=True))
        self.save.write_bytes(b'changed original save')
        result = q.aggregate(self.manifest)
        self.assertIn('watchdog/forced stop', result['cases'][1]['rejected'])
        self.assertIn('original private save changed', result['cases'][1]['rejected'])
        self.assertFalse(result['cases'][1]['runtime_records_consistent'])

    def test_healthy_monitor_with_recorded_warning_still_fails_pressure_free_gate(self):
        self.change_artifact(1,'memory',lambda data:data.update(max_pressure=2))
        self.change_artifact(1,'result',lambda data:data['memory'].update(max_pressure=2))
        row=q.aggregate(self.manifest)['cases'][1]
        self.assertFalse(row['runtime_records_consistent'])
        self.assertIn('memory pressure warning/limit recorded',row['rejected'])
        self.assertNotIn('unhealthy memory monitoring',row['rejected'])

    def test_image_hash_size_and_complete_data_are_checked(self):
        self.assertEqual(q._image_size(png(4, 3)), [4, 3])
        for raw in (png(4, 3)[:33], png(4, 3)[:-1], b'not an image'):
            with self.subTest(raw=raw[:10]), self.assertRaises(ValueError):
                q._image_size(raw)
        image = Path(self.data['cases'][1]['captures'][1]['file']['path'])
        image.write_bytes(png(3, 3))
        self.data['cases'][1]['captures'][1]['file'] = q._ref(image)
        self.write_manifest()
        self.assertIn('capture dimensions do not match their native stage', q.aggregate(self.manifest)['cases'][1]['rejected'])

    def test_original_vision_matrix_camera_contract_and_memory_failure(self):
        self.assertTrue(q._camera({'world_matrix': [1 if i % 5 == 0 else 0 for i in range(16)],
                                   'fov_native': [60, 40], 'clip_native': [1, 1000]}))
        self.assertFalse(q._camera({'world_matrix': [0] * 15, 'fov_native': [60, 40], 'clip_native': [1, 1000]}))
        self.change_artifact(1, 'memory', lambda data: data.update(monitor_failed=True))
        self.change_artifact(1, 'result', lambda data: data['memory'].update(monitor_failed=True))
        result = q.aggregate(self.manifest)
        self.assertIn('unhealthy memory monitoring', result['cases'][1]['rejected'])
        self.assertFalse(result['cases'][1]['native_ssaa_records_consistent'])

    def add_native_capture(self, *, fallback=False, source_size=4, output_size=2):
        case = self.work / 'candidate'
        source, output = bmp(source_size, source_size), bmp(output_size, output_size)
        (case / 'capture-1-source.bmp').write_bytes(source)
        (case / 'capture-1-output.bmp').write_bytes(output)
        data = {'format': 's7-native-frame-capture-v1', 'sequence': 1, 'frame': 80, 'edition': 1,
                'complete': True, 'status': 0, 'source': [source_size, source_size], 'output': [output_size, output_size],
                'source_bmp_sha256': digest(source), 'output_bmp_sha256': digest(output),
                'capture_stall_ms': 42.0, 'desktop_capture': False, 'exclude_from_pacing': True,
                'present_hresult': 0, 'final_present_hresult': 0,
                'readback_hresult': 0, 'client': [output_size, output_size], 'viewport': [source_size, source_size], 'selected_target': [source_size, source_size],
                'high_resolution_draws': 900, 'indexed_draws': 800,
                'attempted_path_hresult': 0x88760868 if fallback else 0,
                'filtered_present_hresult': 0x88760868 if fallback else 0,
                'presentation_path': 'fallback_original' if fallback else 'filtered_output',
                'fallback_used': fallback, 'filtered_api_succeeded': not fallback,
                'observation_available': 7, 'observation_status': 0,
                'camera_world': [1 if i % 5 == 0 else 0 for i in range(16)],
                'fov_native': [60, 40], 'clip_native': [1, 1000], 'lod_scale': 1.5,
                'sun_azimuth': None, 'sun_elevation': None, 'exposure': None, 'simulation_turn': None}
        path = case / 'capture-1.json'
        path.write_text(json.dumps(data))
        self.data['cases'][1]['native_captures'] = [q._ref(path)]
        self.data['cases'][1]['captures'].extend([
            {'stage': 'source', 'file': q._ref(case / 'capture-1-source.bmp')},
            {'stage': 'output', 'file': q._ref(case / 'capture-1-output.bmp')}])
        self.write_manifest()

    def test_native_same_frame_buffer_pair_and_filtered_present_are_separate(self):
        self.add_native_capture()
        result = q.aggregate(self.manifest)
        frame = result['cases'][1]['native_frame_pairs'][0]
        self.assertTrue(frame['gpu_buffer_pair_bound'])
        self.assertTrue(frame['filtered_present_recorded'])
        self.assertEqual(frame['lod_scale'], 1.5)
        self.assertFalse(frame['visible_camera_validated'])
        self.assertIn('vision_main_context_camera', frame)
        self.assertIsNone(frame['exposure'])
        self.assertEqual(result['edition_coverage']['ubisoft-history']['same_frame_filtered_capture_runs'], ['candidate'])
        self.assertFalse(result['release_qualified'])

    def test_successful_original_fallback_never_qualifies_a_failed_filtered_present(self):
        self.add_native_capture(fallback=True)
        result = q.aggregate(self.manifest)
        frame = result['cases'][1]['native_frame_pairs'][0]
        self.assertTrue(frame['gpu_buffer_pair_bound'])
        self.assertFalse(frame['filtered_present_recorded'])
        self.assertEqual(result['edition_coverage']['ubisoft-history']['same_frame_filtered_capture_runs'], [])
        self.assertIn('per-frame successful filtered/original presentation without fallback', result['cases'][1]['missing'])

    def test_same_frame_river_uniforms_require_exact_hash_frame_and_scene(self):
        self.effects_build()
        self.add_native_capture()
        self.change_artifact(1, 'hook_init', lambda data: data.update(observer_catalog_sha256='d' * 64))
        uniform = {'source': 'original_river_shader_uniforms', 'available': 1, 'context_valid': 7,
                   'frame': 80, 'successful_draw': 71,
                   'vertex_id': 309, 'pixel_id': 310, 'vertex_aliases': 3, 'pixel_aliases': 1,
                   'catalog_sha256': 'd' * 64, 'vertex_sha256': q.RIVER_VERTEX_SHA256,
                   'pixel_sha256': q.RIVER_PIXEL_SHA256, 'target': [4, 4], 'viewport': [4, 4],
                   'target_format': 21, 'alpha_blend': 0, 'alpha_test': 0, 'srgb_write': 0,
                   'vs_constants_12_15': [1 if i % 5 == 0 else 0 for i in range(16)],
                   'vs16_eye': [10, 20, 30], 'ps5_sun_direction': [.1, .2, .3],
                   'ps7_sun_specular': [1, 2, 3]}
        def write(values):
            reference = self.data['cases'][1]['native_captures'][0]
            path = Path(reference['path'])
            frame = json.loads(path.read_text())
            frame['scene_uniforms'] = values
            path.write_text(json.dumps(frame))
            self.data['cases'][1]['native_captures'][0] = q._ref(path)
            self.write_manifest()
        write(uniform)
        frame = q.aggregate(self.manifest)['cases'][1]['native_frame_pairs'][0]
        self.assertTrue(frame['same_frame_river_uniforms_recorded'])
        self.assertFalse(frame['visible_camera_validated'])
        self.assertIsNone(frame['exposure'])
        for key, invalid in [('frame', 79), ('pixel_sha256', 'e' * 64), ('target', [2, 2]),
                             ('catalog_sha256', 'b' * 64), ('context_valid', 0),
                             ('vs16_eye', [0, 0, float('nan')])]:
            changed = deepcopy(uniform)
            changed[key] = invalid
            write(changed)
            result = q.aggregate(self.manifest)['cases'][1]
            self.assertFalse(result['native_frame_pairs'][0]['same_frame_river_uniforms_recorded'])
            self.assertIn('invalid or unsupported same-frame original River shader observation', result['rejected'])
        write(None)
        result = q.aggregate(self.manifest)['cases'][1]
        self.assertTrue(result['runtime_records_consistent'])
        self.assertFalse(result['native_frame_pairs'][0]['same_frame_river_uniforms_recorded'])

    def test_effect_epoch_requires_geometry_and_drawn_revision_without_proving_material_activation(self):
        epoch = dict.fromkeys(q.EFFECT_EPOCH_FIELDS, 0)
        epoch.update(effects_requested=7, effects_configured=1, effects_active=3, effects_revision=8,
                     effects_drawn_revision=8, effects_drawn_active=3, effects_frame_has_geometry=1)
        self.assertTrue(q._effect_epoch(epoch)['drawn_configuration_consistent'])
        self.assertFalse(q._effect_epoch(epoch)['individual_material_changes_proven'])
        for key, value in [('effects_drawn_revision', 7), ('effects_frame_has_geometry', 0),
                           ('effects_module_restore_failures', 1), ('effects_faulted', 1)]:
            changed = dict(epoch)
            changed[key] = value
            self.assertFalse(q._effect_epoch(changed)['drawn_configuration_consistent'])
        with self.assertRaises(ValueError):
            q._effect_epoch({'effects_requested': 7})
        self.assertIsNone(q._effect_epoch({}))

    def test_duplicate_cases_unknown_pairs_and_external_paths_refuse_manifest(self):
        self.data['cases'].append(deepcopy(self.data['cases'][0]))
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'unique'):
            q.aggregate(self.manifest)
        self.data['cases'].pop()
        self.data['pairs'][0]['candidate'] = 'missing'
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'known case'):
            q.aggregate(self.manifest)
        with self.assertRaisesRegex(ValueError, 'below repository'):
            q._plain(self.root / 'outside.json')


if __name__ == '__main__':
    unittest.main()
