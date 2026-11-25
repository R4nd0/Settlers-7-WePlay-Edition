import copy
import hashlib
import struct
import unittest

from tools.material_shaders import compare


def bmp(pixels,*,top_down=False,bits=32):
    height=len(pixels);width=len(pixels[0]);stride=((width*bits+31)//32)*4
    image=bytearray()
    for row in pixels if top_down else reversed(pixels):
        data=b"".join(bytes((b,g,r,alpha))if bits==32 else bytes((b,g,r))for r,g,b,alpha in row)
        image.extend(data+bytes(stride-len(data)))
    return b"BM"+struct.pack("<IHHI",54+len(image),0,0,54)+struct.pack("<IiiHHIIIIII",40,width,-height if top_down else height,1,bits,0,len(image),0,0,0,0)+image


def pinned_receipt(image,frame,active,revision):
    scene=dict(source="original_river_shader_uniforms",available=1,context_valid=7,frame=frame,
               vertex_sha256=compare.RIVER_VS,pixel_sha256=compare.RIVER_PS,target=[1,1],viewport=[1,1],
               catalog_sha256="a"*64,vs_constants_12_15=[1.0]*16,vs16_eye=[1.,2.,3.],ps5_sun_direction=[0.,0.,1.],ps7_sun_specular=[1.,1.,1.])
    meta=dict(format="s7-native-frame-capture-v1",complete=True,status=0,fallback_used=False,frame=frame,source=[1,1],
              source_bmp_sha256=hashlib.sha256(image).hexdigest(),scene_uniforms=scene,
              effects_configured=1,effects_frame_has_geometry=1,effects_revision=revision,effects_drawn_revision=revision,
              effects_active=active,effects_drawn_active=active,
              comparison=dict(diagnostic_only=True,frame=frame,last_applied_frame=frame,last_restored_frame=frame,
                              requested=1,active=1,pinned=1,faulted=0,unsafe=0,epoch=2,pin_committed_frame=10,
                              pinned_ps80=[2000000.]*4,completed_frame_applications=12,completed_frame_restorations=12))
    for key in ("effects_unsafe","effects_faulted","effects_disabled_reason","effects_module_faulted",
                "effects_module_shader_failures","effects_module_set_failures","effects_module_restore_failures","effects_module_exceptions"):
        meta[key]=0
    return meta


class ComparisonTests(unittest.TestCase):
    def test_roi_rgb_metrics_ignore_alpha_and_preserve_original_bytes(self):
        original=bmp([[(10,20,30,0),(20,30,40,0)]])
        altered=bmp([[(14,18,32,255),(20,30,40,255)]])
        before=(hashlib.sha256(original).hexdigest(),hashlib.sha256(altered).hexdigest())
        report=compare.differences(original,altered,(0,0,1,1),threshold=3)
        self.assertEqual(report["changed_rgb_pixels"],1);self.assertEqual(report["max_abs_rgb_difference"],4)
        self.assertEqual(report["channel_mae_rgb"],[4,2,2]);self.assertAlmostEqual(report["mean_abs_rgb_difference"],8/3)
        self.assertEqual(before,(hashlib.sha256(original).hexdigest(),hashlib.sha256(altered).hexdigest()))
        self.assertFalse(report["visual_quality_assessed"]);self.assertFalse(report["input_images_modified"])

    def test_bmp_orientation_and_24bit_padding(self):
        pixels=[[(1,2,3,4)],[(5,6,7,8)]]
        first=bmp(pixels,bits=24);second=bmp(pixels,top_down=True)
        self.assertEqual(compare.differences(first,second)["changed_rgb_pixels"],0)

    def test_invalid_geometry_crop_compression_and_resampling_refused(self):
        original=bmp([[(1,2,3,4)]])
        with self.assertRaises(ValueError):compare.differences(original,bmp([[(1,2,3,4),(1,2,3,4)]]))
        for roi in ((-1,0,1,1),(0,0,2,1),(0,0,0,1)):
            with self.assertRaises(ValueError):compare.differences(original,original,roi)
        bad=bytearray(original);struct.pack_into("<I",bad,30,3)
        with self.assertRaises(ValueError):compare.bitmap(bytes(bad))

    def test_camera_and_light_receipts_require_exact_frame_and_actual_image_hash(self):
        image=bmp([[(1,2,3,4)]]);digest=hashlib.sha256(image).hexdigest()
        scene=dict(source="original_river_shader_uniforms",available=1,context_valid=7,frame=40,
                   vertex_sha256=compare.RIVER_VS,pixel_sha256=compare.RIVER_PS,target=[1,1],viewport=[1,1],
                   catalog_sha256="a"*64,vs_constants_12_15=[1.0]*16,vs16_eye=[1.,2.,3.],ps5_sun_direction=[0.,0.,1.],ps7_sun_specular=[1.,1.,1.])
        first=dict(format="s7-native-frame-capture-v1",complete=True,status=0,fallback_used=False,frame=40,source=[1,1],source_bmp_sha256=digest,scene_uniforms=scene)
        second=copy.deepcopy(first);second["frame"]=41;second["scene_uniforms"]["frame"]=41
        result=compare.uniform_comparison(first,second,digest,digest)
        self.assertTrue(result["same_frame_provenance_per_image"]);self.assertTrue(result["captured_camera_uniforms_match"])
        self.assertIsNone(result["exposure_matched"]);self.assertFalse(result["visible_camera_independently_validated"])
        second["scene_uniforms"]["frame"]=40
        self.assertFalse(compare.uniform_comparison(first,second,digest,digest)["same_frame_provenance_per_image"])
        second=copy.deepcopy(first);second["scene_uniforms"]=None
        self.assertFalse(compare.uniform_comparison(first,second,digest,digest)["same_frame_provenance_per_image"])

    def test_off_on_off_requires_positive_current_frame_pin_and_measures_residual(self):
        control=bmp([[(10,20,30,0)]]);candidate=bmp([[(14,25,36,0)]])
        first=pinned_receipt(control,40,0,1);second=pinned_receipt(candidate,41,1,2);third=pinned_receipt(control,42,0,3)
        result=compare.comparison_series(control,candidate,control,first,second,third)
        self.assertTrue(result["comparison_evidence_complete"])
        self.assertTrue(result["original_camera_light_float32_bit_identical"])
        self.assertEqual(result["control_control_residual"]["mean_abs_rgb_difference"],0)
        self.assertGreater(result["effect_difference"]["mean_abs_rgb_difference"],0)
        self.assertFalse(result["automatic_quality_pass"]);self.assertFalse(result["runtime_qualified"])
        self.assertFalse(result["source_checkpoint_and_dll_identity_checked"])

    def test_global_pin_is_insufficient_and_all_four_components_must_match(self):
        image=bmp([[(1,2,3,4)]]);digest=hashlib.sha256(image).hexdigest()
        first=pinned_receipt(image,40,0,1);second=pinned_receipt(image,41,1,2)
        cases=(lambda d:d["comparison"].update(completed_frame_applications=0,completed_frame_restorations=0),
               lambda d:d["comparison"].update(last_applied_frame=40),
               lambda d:d["comparison"].update(completed_frame_restorations=11),
               lambda d:d["comparison"].update(pinned_ps80=[2000000.,2000000.,2000000.,2000001.]),
               lambda d:d["comparison"].update(pinned_ps80=[2000000.]*3+[float("nan")]),
               lambda d:d["comparison"].update(epoch=3),
               lambda d:d["comparison"].update(faulted=1),
               lambda d:d.update(effects_module_restore_failures=1),
               lambda d:d.update(effects_drawn_revision=1),
               lambda d:d.update(effects_frame_has_geometry=0))
        for mutate in cases:
            broken=copy.deepcopy(second);mutate(broken)
            with self.subTest(broken=broken):
                self.assertFalse(compare.uv_pin_comparison(first,broken,digest,digest)["diagnostic_uv_pin_verified"])

    def test_unmatched_repeat_control_or_camera_rejects_triplet_evidence(self):
        image=bmp([[(1,2,3,4)]])
        first=pinned_receipt(image,40,0,1);second=pinned_receipt(image,41,1,2);third=pinned_receipt(image,42,0,3)
        for mutate in (lambda d:d.update(effects_active=1,effects_drawn_active=1),
                       lambda d:d["scene_uniforms"].update(vs16_eye=[2.,2.,3.]),
                       lambda d:d.update(source_bmp_sha256="0"*64),
                       lambda d:d["comparison"].update(last_restored_frame=41)):
            broken=copy.deepcopy(third);mutate(broken)
            with self.subTest(broken=broken):
                result=compare.comparison_series(image,image,image,first,second,broken)
                self.assertFalse(result["comparison_evidence_complete"])
                self.assertFalse(result["automatic_quality_pass"])


if __name__=="__main__":unittest.main()
