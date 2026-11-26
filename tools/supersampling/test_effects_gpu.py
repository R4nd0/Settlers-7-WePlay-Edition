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
        for version in (True, 0, 2, 4, '3'):
            with self.subTest(version=version), self.assertRaises(ValueError):
                effects_gpu.validate_mode(valid, version)

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


if __name__ == '__main__':
    unittest.main()
