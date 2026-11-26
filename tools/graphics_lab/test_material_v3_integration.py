"""Explicit Leaf-PS transport boundaries; no game, process or GPU is opened."""
from contextlib import nullcontext
from copy import deepcopy
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

from tools.graphics_lab import game, load_hook, qualification as q
from tools.supersampling import hook_probe


class MaterialV3IntegrationTests(unittest.TestCase):
    def payload(self):
        raw=bytearray(7204)
        struct.pack_into('<5I4f',raw,0,7204,3,4,3840,2160,1.15,.94,1.20,1.10)
        for offset,name in ((3108,'C:/private/effects.private.json'),(6180,'C:/private/fixed.ps.bin')):
            value=name.encode('utf-16le');raw[offset:offset+len(value)]=value
        return bytes(raw)

    def test_leaf_ps_cannot_use_v2_or_ambiguous_headers(self):
        payload=self.payload();dll=Path('not-opened.dll')
        with patch.object(load_hook,'verify_build',return_value=(dll,{'abi':hook_probe.BUILD_ABI_WATER})),\
                patch.object(load_hook,'_invoke_pinned')as invoke:
            with self.assertRaisesRegex(ValueError,'material V3'):
                load_hook.invoke(None,dll,'effects',payload)
            invoke.assert_not_called()
        for size,version in ((6180,3),(7204,2),(7204,4)):
            broken=struct.pack('<2I',size,version)+payload[8:]
            with self.subTest(size=size,version=version),patch.object(load_hook,'verify_build')as verify:
                with self.assertRaises(ValueError):load_hook.invoke(None,dll,'effects',broken)
                verify.assert_not_called()
        with patch.object(load_hook,'verify_build',return_value=(dll,{'abi':hook_probe.BUILD_ABI_V3})),\
                patch.object(load_hook,'file_lease',return_value=nullcontext()),\
                patch.object(load_hook,'_invoke_pinned',return_value={'accepted':True})as invoke:
            self.assertEqual(load_hook.invoke(None,dll,'effects',payload),{'accepted':True})
            invoke.assert_called_once_with(None,dll,payload,b'WePlayNativeGraphicsConfigureEffects\0')

    def test_v3_is_explicit_and_excludes_stacked_trials_before_io(self):
        valid=dict(hook=Path('not-opened.dll'),high_quality=True,material_config_version=3,
                   material_effects=('improved_vegetation',),leaf_wrapped=True)
        for changes in ({'leaf_wrapped':1},{'leaf_wrapped':False},{'hook':None},{'high_quality':False},
                        {'material_config_version':2},{'material_effects':()},
                        {'material_effects':('improved_vegetation','improved_terrain')},
                        {'leaf_hemisphere':True},{'terrain_macro':True},{'water_program':True},
                        {'shadow_filter':True},{'ambient_occlusion':True},{'comparison_uv':True},
                        {'baseline_crash_watch':True}):
            with self.subTest(changes=changes),patch.object(game.platform,'load')as load:
                with self.assertRaisesRegex(ValueError,'Leaf wrapped diffuse requires explicit'):
                    game.run(Path('not-read'),Path('not-created'),**{**valid,**changes})
                load.assert_not_called()

    def test_v3_record_refuses_reused_old_shader_paths_or_parameters(self):
        raw=self.payload();self.assertEqual(q._material_configuration(raw)['version'],3)
        for offset,value in ((8,struct.pack('<I',6)),(20,struct.pack('<f',2)),
                             (36,b'A\0'),(1060,b'A\0'),(2084,b'A\0'),
                             (4132,b'A\0'),(5156,b'A\0'),(6180,bytes(1024))):
            broken=bytearray(raw);broken[offset:offset+len(value)]=value
            with self.subTest(offset=offset),self.assertRaises(ValueError):
                q._material_configuration(bytes(broken))

    def test_historical_schemas_do_not_gain_v3_implicitly(self):
        build=dict(format='s7-native-graphics-build-v2',source_set='effects-v9',
                   compiled_capabilities=15,abi=deepcopy(hook_probe.BUILD_ABI_V3),
                   diagnostic_abi={'comparison':12,'shadow':12,'ambient':12,'ambient_v2':12},
                   build_sha256='a'*64,files={})
        sources,_,valid=q._build_contract(build)
        self.assertTrue(valid);self.assertEqual(len(sources),38)
        self.assertEqual(sources,set(hook_probe.LEAF_PIXEL_SOURCES))
        for schema in ('effects-v7','effects-v8'):
            old=deepcopy(build);old['source_set']=schema
            self.assertFalse(q._build_contract(old)[2])
        old=deepcopy(build);old['source_set']='effects-v8';old['abi']=hook_probe.BUILD_ABI_WATER
        self.assertTrue(q._build_contract(old)[2])
        old['source_set']='effects-v9'
        self.assertFalse(q._build_contract(old)[2])

    def test_actual_worker_prefix_budget_is_not_assumed(self):
        export=b'WePlayNativeGraphicsConfigureEffects\0'
        for directory,expected in (('C:/private',True),('C:/'+('long-'*100),False)):
            prefix,*_=load_hook._payload_prefix(Path(directory)/'d3d9.dll',Path(directory)/load_hook.DLL_NAME,export)
            self.assertEqual(len(prefix)+7204<=8192,expected)


if __name__=='__main__':unittest.main()
