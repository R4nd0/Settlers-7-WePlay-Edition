import copy
import hashlib
import struct
import unittest

from tools.material_shaders import capture


def metadata_fixture():
    entries=[];reflected=[]
    for family,record,requests in capture.PRESETS:
        resource=f"Shaders/Family{family}_ATI_High.ShaderLib"
        for stage in ("vertex","pixel"):
            digest=hashlib.sha256((str(family)+record+stage).encode()).hexdigest()
            entries.append({"id":len(entries)+1,"family":family,"shader_record":record,"stage":stage,
                            "resource_name":resource,"compiled_sha256":digest})
            bindings=[]
            for index,(request_stage,name,mask,interpretation) in enumerate(requests):
                if request_stage==stage:
                    bindings.append({"name":name,"register_set":"float4","register_index":64+index,
                                     "register_count":1})
            if stage=="pixel":bindings.append({"name":"fixture_map","register_set":"sampler","register_index":3,"register_count":1})
            reflected.append({"resource":resource,"shader_record":record,"stage":stage,
                              "compiled_sha256":digest,"bindings":bindings})
    return {"entries":entries,"catalog_sha256":"a"*64},{"entries":reflected}


class CaptureConfigurationTests(unittest.TestCase):
    def test_exact_native_layout_and_read_only_flags(self):
        catalog,bindings=metadata_fixture()
        binary,manifest=capture.build(catalog,bindings,first_frame=123,frame_stride=300,sample_limit=4)
        self.assertEqual(len(binary),952)
        self.assertEqual(capture.HEADER.unpack_from(binary),(952,1,8,300,123))
        self.assertEqual(capture.TARGET.unpack_from(binary,24),(1,2,4,3,8))
        self.assertEqual(capture.REQUEST.unpack_from(binary,44),(1,64,15))
        self.assertTrue(manifest["read_only"])
        self.assertFalse(manifest["replacement_enabled"])
        self.assertFalse(manifest["foliage_backlighting_uniform_identified"])
        self.assertEqual(manifest["config_sha256"],hashlib.sha256(binary).hexdigest())

    def test_reflection_identity_stage_and_register_bounds_refused(self):
        catalog,bindings=metadata_fixture()
        broken=copy.deepcopy(bindings);broken["entries"][0]["compiled_sha256"]="0"*64
        with self.assertRaisesRegex(ValueError,"CTAB"):capture.build(catalog,broken)
        broken=copy.deepcopy(bindings);broken["entries"][0]["bindings"][0]["register_index"]=256
        with self.assertRaisesRegex(ValueError,"range"):capture.build(catalog,broken)
        broken=copy.deepcopy(bindings);broken["entries"][0]["bindings"]=[]
        with self.assertRaisesRegex(ValueError,"absent"):capture.build(catalog,broken)

    def test_capture_bounds_and_missing_pairs_refused(self):
        catalog,bindings=metadata_fixture()
        for options in ({"frame_stride":0},{"first_frame":-1},{"sample_limit":9},{"sample_limit":True}):
            with self.subTest(options=options),self.assertRaises(ValueError):capture.build(catalog,bindings,**options)
        broken=copy.deepcopy(catalog);broken["entries"][0]["stage"]="pixel"
        with self.assertRaises(ValueError):capture.build(broken,bindings)


if __name__=="__main__":unittest.main()
