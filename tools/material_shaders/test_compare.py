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


if __name__=="__main__":unittest.main()
