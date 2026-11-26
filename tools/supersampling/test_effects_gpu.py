"""CPU-only policy tests; no collector, D3D device, process or window."""
import copy
import unittest

from tools.supersampling import effects_gpu


class EffectsGpuPolicyTests(unittest.TestCase):
    def test_old_default_remains_supported(self):
        effects_gpu.validate_mode({'source_set': 'effects-v8'}, 1)
        effects_gpu.validate_mode({}, 1, 'catalog.bin', 'old-pinned-sha')

    def test_v3_requires_explicit_versioned_build(self):
        valid = {'source_set': 'effects-v9', 'abi': {'effects_v3': 7204}}
        effects_gpu.validate_mode(valid, 3)
        for wrong in ({}, {'source_set': 'effects-v8', 'abi': {'effects_v3': 7204}},
                      {'source_set': 'effects-v9', 'abi': {'effects_v3': 6180}}):
            with self.subTest(wrong=wrong), self.assertRaises(ValueError):
                effects_gpu.validate_mode(wrong, 3)
        for version in (True, 0, 2, 5, '3'):
            with self.subTest(version=version), self.assertRaises(ValueError):
                effects_gpu.validate_mode(valid, version)

    def test_palette_requires_independent_v4_abi(self):
        good = {'source_set': 'effects-v10', 'abi': {'effects_v3': 7204, 'effects_v4': 4120}}
        effects_gpu.validate_mode(good, 3)
        effects_gpu.validate_mode(good, 4)
        for wrong in ({'source_set': 'effects-v9', 'abi': {'effects_v4': 4120}},
                      {'source_set': 'effects-v10', 'abi': {'effects_v4': 7204}}):
            with self.assertRaises(ValueError):
                effects_gpu.validate_mode(wrong, 4)
        with self.assertRaises(ValueError):
            effects_gpu.validate_mode(good, 4, 'catalog', 'digest')

    def test_v3_cannot_smuggle_comparison_catalog(self):
        manifest = {'source_set': 'effects-v9', 'abi': {'effects_v3': 7204}}
        for catalog, digest in (('catalog.bin', None), (None, 'sha'), ('catalog.bin', 'sha')):
            with self.subTest(catalog=catalog), self.assertRaises(ValueError):
                effects_gpu.validate_mode(manifest, 3, catalog, digest)

    @staticmethod
    def proof():
        receipt = dict(format='s7-material-effects-v3-gpu-v1', game_tested=False, wire_bytes=7204,
                       authored_programs=True, public_off_on_off=True, pixel_replacement_verified=True,
                       same_frame_capture=True, failed_draw_restored=True, reset_recreated_program=True,
                       warm_program_reused=True, leaf_applied=12, restored_draws=12)
        epoch = dict(format='s7-native-frame-capture-v1', sequence=31, complete=True, status=0,
                     effects_faulted=0, effects_unsafe=0, effects_module_faulted=0,
                     effects_module_shader_failures=0, effects_module_set_failures=0,
                     effects_module_restore_failures=0, effects_config_version=3, effects_active=4,
                     effects_drawn_active=4, effects_frame_has_geometry=1, effects_revision=2,
                     effects_drawn_revision=2, frame=5, source=[1280, 720], output=[640, 360],
                     client=[640, 360], viewport=[1280, 720], selected_target=[1280, 720],
                     presentation_path='filtered_output', fallback_used=False, filtered_api_succeeded=True,
                     filtered_present_hresult=0, final_present_hresult=0)
        return receipt, epoch

    def test_positive_exact_v3_epoch(self):
        effects_gpu.validate_v3_receipt(*self.proof())

    def test_missing_evidence_refused(self):
        receipt, epoch = self.proof()
        for source in (0, 1):
            for field in (receipt, epoch)[source]:
                rows = copy.deepcopy([receipt, epoch])
                del rows[source][field]
                with self.subTest(source=source, field=field), self.assertRaises(RuntimeError):
                    effects_gpu.validate_v3_receipt(*rows)

    def test_false_epoch_and_fallback_refused(self):
        for field, value in dict(effects_drawn_revision=1, effects_active=0, effects_drawn_active=0,
                                 effects_config_version=2, effects_frame_has_geometry=0, effects_faulted=1,
                                 effects_module_restore_failures=1, sequence=1, complete=False,
                                 client=[1280, 720], viewport=[640, 360], presentation_path='fallback_original',
                                 fallback_used=True, filtered_api_succeeded=False,
                                 filtered_present_hresult=0x8876086C).items():
            receipt, epoch = self.proof()
            epoch[field] = value
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                effects_gpu.validate_v3_receipt(receipt, epoch)

    def test_unbalanced_restoration_and_false_pixel_claim_refused(self):
        for field, value in dict(restored_draws=11, leaf_applied=0, wire_bytes=6180,
                                 pixel_replacement_verified=False, reset_recreated_program=False).items():
            receipt, epoch = self.proof()
            receipt[field] = value
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                effects_gpu.validate_v3_receipt(receipt, epoch)

    def palette_proof(self):
        hashes = [digit * 64 for digit in 'abc']
        receipt = dict(format='s7-material-effects-v4-gpu-v1', game_tested=False, authored_programs=True, wire_bytes=4120,
                       three_program_colors=True, first_match_creates_three=True, safe_live_selection=True,
                       off_retains_warm_palette=True, same_frame_captures=True, stale_epoch_deferred=True,
                       failed_original_restored=True, reset_recreated_palette=True, program_sha256=hashes,
                       variant_applications=[4, 3, 2], variant_restorations=[4, 3, 2])
        captures = []
        for index in range(4):
            _, epoch = self.proof()
            off = index == 3
            selected = 2 if off else index
            epoch.update(sequence=60 if off else 50 + index, frame=10 + index, effects_config_version=4,
                         effects_selected_variant=selected, effects_drawn_variant=None if off else selected,
                         effects_drawn_frame=10 + index, effects_revision=3 + index, effects_drawn_revision=3 + index,
                         effects_active=0 if off else 4, effects_drawn_active=0 if off else 4,
                         effects_variant_frame_applications=0 if off else 3,
                         effects_variant_frame_restorations=0 if off else 3, effects_variant_frame_failed_draws=0)
            epoch['leaf_palette'] = dict(selected_variant=selected, drawn_variant=None if off else selected,
                drawn_frame=epoch['frame'], frame_applications=0 if off else 3, frame_restorations=0 if off else 3,
                frame_failed_draws=0, configured_program_sha256=hashes[selected],
                drawn_program_sha256=None if off else hashes[selected], module_epoch=4 + index,
                variant_applications=[4, 3, 2], variant_restorations=[4, 3, 2], variant_successful_draws=[4, 3, 2],
                variant_failed_draws=[0, 0, 0], program_creations=[1, 1, 1], faulted=0, shader_failures=0,
                set_failures=0, restore_failures=0, exceptions=0, active_transactions=0, active_original_refs=0)
            captures.append(epoch)
        return receipt, captures

    def test_actual_three_program_palette_and_original_off(self):
        effects_gpu.validate_v4_receipt(*self.palette_proof())

    def test_palette_label_alone_cannot_substitute_for_draw_program_frame(self):
        for key, value in dict(effects_drawn_variant=None, effects_selected_variant=2, effects_drawn_frame=1,
                               effects_variant_frame_applications=0, effects_variant_frame_failed_draws=1).items():
            receipt, captures = self.palette_proof()
            captures[1][key] = value
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                effects_gpu.validate_v4_receipt(receipt, captures)
        for key, value in dict(drawn_program_sha256='f' * 64, module_epoch=0,
                               program_creations=[1, 0, 1], variant_successful_draws=[4, 0, 2],
                               active_original_refs=1).items():
            receipt, captures = self.palette_proof()
            captures[1]['leaf_palette'][key] = value
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                effects_gpu.validate_v4_receipt(receipt, captures)

    def test_palette_off_never_claims_a_drawn_replacement(self):
        receipt, captures = self.palette_proof()
        captures[3]['leaf_palette']['drawn_program_sha256'] = 'c' * 64
        with self.assertRaises(RuntimeError):
            effects_gpu.validate_v4_receipt(receipt, captures)

    def water_proof(self):
        hashes = [digit * 64 for digit in 'abc']
        receipt = dict(format='s7-water-palette-gpu-v1', game_tested=False, authored_programs=True, wire_bytes=3096,
            public_typed_queue=True, immediate_capture_deferred_safely=True, three_program_colors=True, first_draw_creates_three=True,
            off_selection_cache_reused=True, failed_original_restored=True, actual_reset=True, legacy_VS68_69_unchanged=True,
            frame_origin=5, program_sha256=hashes)
        captures = []
        for position, seq in enumerate((70, 80, 81, 82, 100)):
            off = position in (0, 4)
            selected = 0 if position == 0 else 2 if position == 4 else position - 1
            _, epoch = self.proof()
            epoch.update(sequence=seq, frame=10+position, effects_active=0, effects_drawn_active=0, effects_config_version=1, effects_configured=1)
            epoch['water'] = dict(frame=epoch['frame'], epoch=position+1, configured=1, requested=0 if off else 1,
                                 enabled=0 if off else 1, completed_applications=0 if off else 2, completed_restorations=0 if off else 2)
            epoch['water_palette'] = dict(config_version=2, selected_variant=selected, drawn_variant=None if off else selected,
                configured_program_sha256=hashes[selected], drawn_program_sha256=None if off else hashes[selected],
                frame=epoch['frame'], epoch=position+1, completed_applications=0 if off else 2,
                completed_restorations=0 if off else 2, completed_successful_draws=0 if off else 2, completed_failed_draws=0,
                variant_applications=[0,0,0] if position==0 else [4,3,2], variant_restorations=[0,0,0] if position==0 else [4,3,2],
                variant_successful_draws=[0,0,0] if position==0 else [4,3,2], variant_failed_draws=[0,0,0],
                program_creations=[0,0,0] if position==0 else [1,1,1], faulted=0, unsafe=0, shader_failures=0,
                set_failures=0, restore_failures=0, exceptions=0, active_transactions=0, pending_original_refs=0, cached_refs=0)
            captures.append(epoch)
        return receipt, captures

    def test_water_palette_is_separate_from_leaf_and_old1076(self):
        manifest=dict(source_set='effects-v11',abi=dict(water=1076,water_palette=3096,effects_v4=4120))
        effects_gpu.validate_mode(manifest,1,water_palette=True)
        for version in (True,3,4):
            with self.assertRaises(ValueError):effects_gpu.validate_mode(manifest,version,water_palette=True)
        with self.assertRaises(ValueError):effects_gpu.validate_mode(dict(source_set='effects-v10'),1,water_palette=True)
        effects_gpu.validate_water_palette_receipt(*self.water_proof())

    def test_water_requires_actual_current_program_and_successful_restoration(self):
        for key,value in dict(drawn_variant=2,drawn_program_sha256='f'*64,frame=1,completed_successful_draws=0,
                              completed_failed_draws=1,pending_original_refs=1,program_creations=[1,0,1]).items():
            receipt,captures=self.water_proof();captures[2]['water_palette'][key]=value
            with self.subTest(key=key),self.assertRaises(RuntimeError):effects_gpu.validate_water_palette_receipt(receipt,captures)

    def test_water_original_off_does_not_claim_selected_shader_drew(self):
        receipt,captures=self.water_proof();captures[0]['water_palette']['drawn_program_sha256']='a'*64
        with self.assertRaises(RuntimeError):effects_gpu.validate_water_palette_receipt(receipt,captures)

    def overlay_proof(self):
        receipt,_=self.water_proof()
        receipt['shader_overlay']=dict(wire_bytes=12,physical_panel_pixels=70000,font_pixels=150,
            outside_control_unchanged=True,original_state_restored=True,actual_wndproc_choices=4,
            safe_boundary_only=True,physical_input_before_scaling=True,actual_selected_program_pixels=True,
            reset_closed_cleanly=True,unsafe=0,epochs=[5,6,7,8],overlay_draws=8,input_events=8)
        return receipt

    def test_overlay_requires_explicit_source45_and_water_palette(self):
        manifest=dict(source_set='effects-v12',abi=dict(water=1076,water_palette=3096),diagnostic_abi=dict(overlay=12))
        effects_gpu.validate_mode(manifest,1,water_palette=True,shader_overlay=True)
        effects_gpu.validate_overlay_receipt(self.overlay_proof())
        for changes in(dict(water_palette=False,shader_overlay=True),dict(water_palette=True,shader_overlay='true')):
            with self.assertRaises(ValueError):effects_gpu.validate_mode(manifest,1,**changes)
        for invalid in(dict(source_set='effects-v11',abi=manifest['abi']),dict(source_set='effects-v12',abi=dict(water=1076,water_palette=3096))):
            with self.assertRaises(ValueError):effects_gpu.validate_mode(invalid,1,water_palette=True,shader_overlay=True)

    def test_overlay_labels_without_actual_pixels_safe_clicks_and_epochs_refused(self):
        for key,value in dict(physical_panel_pixels=0,font_pixels=0,actual_wndproc_choices=3,unsafe=1,
                              overlay_draws=7,input_events=7,epochs=[5,5,6,7],actual_selected_program_pixels=False,
                              safe_boundary_only=False,physical_input_before_scaling=False,original_state_restored=False,
                              reset_closed_cleanly=False,outside_control_unchanged=False).items():
            proof=self.overlay_proof();proof['shader_overlay'][key]=value
            with self.subTest(key=key),self.assertRaises(RuntimeError):effects_gpu.validate_overlay_receipt(proof)

    def test_leaf_v4_overlay_uses_profile2_config16_without_changing_water_v1(self):
        manifest=dict(source_set='effects-v13',abi=dict(water=1076,water_palette=3096,effects_v4=4120),diagnostic_abi=dict(overlay=12,overlay_v2=16))
        effects_gpu.validate_mode(manifest,4,shader_overlay=True)
        effects_gpu.validate_mode(manifest,1,water_palette=True,shader_overlay=True)
        leaf=self.overlay_proof();leaf['shader_overlay'].update(profile_id=2,wire_bytes=16)
        effects_gpu.validate_overlay_receipt(leaf,profile=2)
        with self.assertRaises(RuntimeError):effects_gpu.validate_overlay_receipt(leaf)
        water=self.overlay_proof()
        with self.assertRaises(RuntimeError):effects_gpu.validate_overlay_receipt(water,profile=2)
        for bad in(dict(source_set='effects-v12',abi=manifest['abi'],diagnostic_abi=manifest['diagnostic_abi']),
                   dict(source_set='effects-v13',abi=manifest['abi'],diagnostic_abi=dict(overlay=12))):
            with self.assertRaises(ValueError):effects_gpu.validate_mode(bad,4,shader_overlay=True)

    def test_combined_scope_is_explicit_additive_and_requires_preserved_actual_groups(self):
        manifest=dict(source_set='effects-v14',abi=dict(initialize_v3=3136,effects_v5=24),diagnostic_abi=dict(overlay_v3=16))
        effects_gpu.validate_mode(manifest,5,'owned-catalog','a'*64,shader_overlay=True)
        for kwargs in(dict(shader_overlay=False),dict(shader_overlay=True,water_palette=True)):
            with self.assertRaises(ValueError):effects_gpu.validate_mode(manifest,5,'owned-catalog','a'*64,**kwargs)
        with self.assertRaises(ValueError):effects_gpu.validate_mode(manifest,5,shader_overlay=True)
        old=dict(manifest,source_set='effects-v13')
        with self.assertRaises(ValueError):effects_gpu.validate_mode(old,5,'owned-catalog','a'*64,shader_overlay=True)
        receipt=dict(format='s7-combined-shader-gpu-v1',game_tested=False,authored_programs=True,initialize_bytes=3136,scope=1,
            material_bytes=24,overlay_bytes=16,profile=6,seven_programs=True,rapid_groups_atomic=True,other_groups_preserved=True,
            tab_only_ui=True,actual_leaf_terrain_water_pixels=True,original_restored=True,actual_reset=True,faulted=0,unsafe=0,
            warm_material_creations=7,peak_programs=7,physical_panel_pixels=100000,font_pixels=120,overlay_draws=12)
        effects_gpu.validate_combined_receipt(receipt)
        for key,value in dict(rapid_groups_atomic=False,other_groups_preserved=False,tab_only_ui=False,actual_leaf_terrain_water_pixels=False,
                              original_restored=False,actual_reset=False,unsafe=1,warm_material_creations=8,peak_programs=8).items():
            bad=dict(receipt,**{key:value})
            with self.subTest(key=key),self.assertRaises(RuntimeError):effects_gpu.validate_combined_receipt(bad)


if __name__ == '__main__':
    unittest.main()
