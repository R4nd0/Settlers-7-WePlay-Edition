"""Fixed water-palette transport/intent boundaries; no process or GPU access."""
from contextlib import nullcontext
from copy import deepcopy
from pathlib import Path
import json,struct,unittest
from unittest.mock import patch
from tools.graphics_lab import game,load_hook,qualification as q
from tools.supersampling import hook_probe


class WaterPaletteIntegrationTests(unittest.TestCase):
    def payload(self,selected=0,enabled=1):
        return struct.pack('<6I',3096,2,3840,2160,enabled,selected)+bytes(3072)

    def test_queued_variant_is_profile_specific(self):
        request=dict(operation='variant',sequence=3,variant=2,enabled=1)
        raw=json.dumps(request).encode()
        self.assertEqual(game.native_command(raw,2,water_palette_allowed=True),request)
        self.assertIsNone(game.native_command(raw,3,water_palette_allowed=True))
        for kwargs in ({},{'palette_allowed':True},{'palette_allowed':True,'water_palette_allowed':True}):
            with self.subTest(kwargs=kwargs),self.assertRaises(ValueError):game.native_command(raw,0,**kwargs)
        for changes in ({'variant':True},{'variant':3},{'enabled':4},{'enabled':True},{'path':'unknown.ps.bin'}):
            with self.subTest(changes=changes),self.assertRaises(ValueError):
                game.native_command(json.dumps({**request,**changes}).encode(),0,water_palette_allowed=True)
        with self.assertRaises(ValueError):
            game.native_command(b'{"operation":"water","sequence":4,"enabled":1}',0,water_allowed=True,water_palette_allowed=True)

    def test_palette_startup_rejects_mixed_and_undeclared_modes_before_io(self):
        valid=dict(hook=Path('not-opened.dll'),observer_catalog=Path('not-read.bin'),high_quality=True,
                   material_effects=('improved_water',),water_program=True,water_palette=True)
        for update in ({'water_palette':1},{'water_program':False},{'hook':None},{'observer_catalog':None},
                       {'high_quality':False},{'material_config_version':2},{'leaf_palette':True},
                       {'leaf_wrapped':True},{'shadow_filter':True},{'ambient_occlusion':True},
                       {'comparison_uv':True},{'crash_watch':True},{'baseline_crash_watch':True},
                       {'material_effects':('improved_water','improved_terrain')}):
            with self.subTest(update=update),patch.object(game.platform,'load')as load:
                with self.assertRaisesRegex(ValueError,'Water palette requires explicit'):
                    game.run(Path('not-read'),Path('not-created'),**{**valid,**update})
                load.assert_not_called()

    def test_wire_is_separate_from_accepted_water_v1(self):
        dll=Path('not-opened.dll');raw=self.payload()
        with patch.object(load_hook,'verify_build',return_value=(dll,{'abi':hook_probe.BUILD_ABI_V4})):
            with self.assertRaisesRegex(ValueError,'WaterPalette'):load_hook.invoke(None,dll,'water_palette',raw)
        with patch.object(load_hook,'verify_build',return_value=(dll,{'abi':hook_probe.BUILD_ABI_WATER_PALETTE})),\
             patch.object(load_hook,'file_lease',return_value=nullcontext()),\
             patch.object(load_hook,'_invoke_pinned',return_value={'accepted':True})as invoke:
            self.assertEqual(load_hook.invoke(None,dll,'water_palette',raw),{'accepted':True})
            invoke.assert_called_once_with(None,dll,raw,b'WePlayNativeGraphicsConfigureWater\0')
        for operation,body in [('water',raw),('water_palette',struct.pack('<2I',3096,1)+raw[8:]),
                               ('water_palette',self.payload(3)),('water_palette',self.payload(enabled=4))]:
            with self.subTest(operation=operation),patch.object(load_hook,'verify_build')as verify:
                with self.assertRaises(ValueError):load_hook.invoke(None,dll,operation,body)
                verify.assert_not_called()

    def test_source43_and_3096_capability_are_both_required(self):
        build=dict(format='s7-native-graphics-build-v2',source_set='effects-v11',compiled_capabilities=15,
                   abi=deepcopy(hook_probe.BUILD_ABI_WATER_PALETTE),
                   diagnostic_abi={'comparison':12,'shadow':12,'ambient':12,'ambient_v2':12},
                   build_sha256='a'*64,files={})
        sources,_,valid=q._build_contract(build)
        self.assertTrue(valid);self.assertEqual(len(sources),43)
        self.assertEqual(sources,set(hook_probe.WATER_PALETTE_SOURCES))
        old=deepcopy(build);old['source_set']='effects-v10'
        self.assertFalse(q._build_contract(old)[2]);old['abi']=hook_probe.BUILD_ABI_V4
        self.assertTrue(q._build_contract(old)[2])


if __name__=='__main__':unittest.main()
