"""Scalar metadata fixtures; no original program bytes or archive reads."""
from copy import deepcopy
import hashlib
import struct
import unittest

from tools.shadow_detail import catalog_probe as p


def metadata():
    entries = []
    for resource, record, stage, digest, size in (
        (p.RIVER, 'RiverColor', 'vertex', p.RIVER_VS, 2592),
        (p.RIVER, 'RiverColor', 'pixel', p.RIVER_PS, 3740),
        (p.SHADOW, 'MaskShader', 'vertex', p.SHADOW_VS, 856),
        (p.SHADOW, 'MaskShader', 'pixel', p.SHADOW_PS, 2216)):
        entries.append({'id': len(entries)+1, 'resource_name': resource, 'shader_record': record,
                        'stage': stage, 'compiled_sha256': digest, 'compiled_bytes': size})
    def constant(name,index,count=1,kind='float4'):
        return {'name':name,'register_index':index,'register_count':count,'register_set':kind}
    bindings = [
        {'compiled_sha256':p.RIVER_VS,'stage':'vertex','bindings':[constant('g_VSMainViewProjectionMatrix',12,4),constant('g_VSEye',16)]},
        {'compiled_sha256':p.RIVER_PS,'stage':'pixel','bindings':[constant('g_PSSunDirection',5),constant('g_PSSunSpecularColor',7),constant('g_PSCamera',0)]},
        {'compiled_sha256':p.SHADOW_PS,'stage':'pixel','bindings':[constant('g_InverseShadowMapSize',57),constant('g_KernelRadius',33),constant('g_ShadowMap',1,kind='sampler')]},
    ]
    return {'entries':entries,'catalog_sha256':'c'*64},bindings


class MixedProbeTests(unittest.TestCase):
    def test_mixed_abi_camera_rawsun_shadow_constants_and_sampler(self):
        catalog,bindings=metadata();raw,proof=p.mixed_config(catalog,bindings)
        self.assertEqual(len(raw),952)
        self.assertEqual(struct.unpack_from('<4IQ',raw),(952,1,2,1,0))
        self.assertEqual(struct.unpack_from('<5I',raw,24),(1,2,3,8,0))
        self.assertEqual(struct.unpack_from('<5I',raw,140),(3,4,3,2,2))
        self.assertEqual(struct.unpack_from('<3I',raw,160),(2,57,3))
        self.assertEqual(struct.unpack_from('<3I',raw,172),(2,33,15))
        self.assertEqual(proof['config_sha256'],hashlib.sha256(raw).hexdigest())
        self.assertFalse(proof['shadow_effect_enabled'])
        self.assertFalse(proof['runtime_binding_observed'])
        self.assertIsNone(proof['sun_elevation'])

    def test_changed_program_binding_and_sampler_fail_closed(self):
        for variant in ('program','river','register','sampler'):
            catalog,bindings=metadata()
            if variant=='program':catalog['entries'][3]['compiled_sha256']='d'*64
            elif variant=='river':catalog['entries'][0]['compiled_sha256']='d'*64
            elif variant=='register':bindings[2]['bindings'][0]['register_index']=58
            else:bindings[2]['bindings'][2]['register_index']=2
            with self.subTest(variant=variant),self.assertRaises(ValueError):p.mixed_config(catalog,bindings)

    def test_catalog_ids_are_derived_not_hardcoded_old_material_ids(self):
        catalog,bindings=metadata()
        for row in catalog['entries']:row['id']+=10
        raw,_=p.mixed_config(catalog,bindings)
        self.assertEqual(struct.unpack_from('<2I',raw,24),(11,12))
        self.assertEqual(struct.unpack_from('<2I',raw,140),(13,14))


if __name__=='__main__':unittest.main()
