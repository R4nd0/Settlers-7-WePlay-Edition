"""Family overlay scope, source manifest and typed command boundaries."""
from contextlib import nullcontext
from copy import deepcopy
from pathlib import Path
import struct, unittest
from unittest.mock import patch
from tools.graphics_lab import game,load_hook,qualification
from tools.supersampling import hook_probe

class FamilyOverlayTests(unittest.TestCase):
    def test_explicit_family_must_match_admitted_resource_intent_before_io(self):
        for kwargs in ({'shader_profile':'unknown'}, {'shader_profile':True},
                       {'shader_profile':'vegetation','shader_overlay':True},
                       {'shader_profile':'terrain','shader_overlay':True,'leaf_palette':True},
                       {'shader_profile':'shadow','shader_overlay':True,'shadow_filter':True,'material_effects':('improved_terrain',)},
                       {'shader_profile':'ambient','shader_overlay':True,'shadow_filter':True}):
            with self.subTest(kwargs=kwargs),patch.object(game.platform,'load')as load:
                with self.assertRaisesRegex(ValueError,'Shader profile'):
                    game.run(Path('not-read'),Path('not-created'),**kwargs)
                load.assert_not_called()

    def test_each_single_family_passes_intent_validation(self):
        common=dict(shader_overlay=True,hook=Path('not-read.dll'),observer_catalog=Path('not-read.bin'),high_quality=True)
        modes={
            'water':dict(water_palette=True,water_program=True,material_effects=('improved_water',)),
            'vegetation':dict(leaf_palette=True,material_config_version=4,material_effects=('improved_vegetation',)),
            'terrain':dict(terrain_macro=True,material_config_version=2,material_effects=('improved_terrain',)),
            'shadow':dict(shadow_filter=True),
            'ambient':dict(ambient_occlusion=True),
        }
        for name,options in modes.items():
            with self.subTest(name=name),patch.object(game,'lab_path',side_effect=lambda p:p),\
                 patch.object(game.platform,'load',side_effect=LookupError('intent passed'))as load:
                with self.assertRaisesRegex(LookupError,'intent passed'):
                    game.run(Path('not-read'),Path('not-created'),shader_profile=name,**common,**options)
                load.assert_called_once()

    def test_family_rejects_other_render_and_debug_paths_before_io(self):
        valid=dict(shader_profile='terrain',shader_overlay=True,hook=Path('not-read.dll'),
                   observer_catalog=Path('not-read.bin'),high_quality=True,terrain_macro=True,
                   material_config_version=2,material_effects=('improved_terrain',))
        for extra in ({'comparison_uv':True},{'detailed_shadows':True},{'crash_watch':True},
                      {'baseline_crash_watch':True},{'baseline_metric_profile':True},
                      {'hook':None},{'observer_catalog':None},{'high_quality':False}):
            with self.subTest(extra=extra),patch.object(game.platform,'load')as load:
                with self.assertRaisesRegex(ValueError,'Shader profile requires isolated'):
                    game.run(Path('not-read'),Path('not-created'),**{**valid,**extra})
                load.assert_not_called()

    def test_overlay_v2_separate_fixed_header_profile_and_build_capability(self):
        dll=Path('not-read.dll')
        for profile in range(1,6):
            payload=struct.pack('<4I',16,2,1,profile)
            with patch.object(load_hook,'verify_build',return_value=(dll,{'diagnostic_abi':{'overlay_v2':16}})),\
                 patch.object(load_hook,'file_lease',return_value=nullcontext()),\
                 patch.object(load_hook,'_invoke_pinned',return_value=True)as invoke:
                self.assertTrue(load_hook.invoke(None,dll,'overlay_v2',payload))
                invoke.assert_called_once_with(None,dll,payload,b'WePlayNativeGraphicsConfigureOverlay\0')
        for raw in (struct.pack('<4I',16,2,1,0),struct.pack('<4I',16,2,1,6),struct.pack('<4I',16,2,2,2),struct.pack('<4I',16,1,1,2)):
            with self.subTest(raw=raw),patch.object(load_hook,'verify_build')as verify:
                with self.assertRaises(ValueError):load_hook.invoke(None,dll,'overlay_v2',raw)
                verify.assert_not_called()
        with patch.object(load_hook,'verify_build',return_value=(dll,{'diagnostic_abi':{'overlay':12}})):
            with self.assertRaisesRegex(ValueError,'family shader overlay'):
                load_hook.invoke(None,dll,'overlay_v2',struct.pack('<4I',16,2,1,2))

    def test_new_manifest_keeps_old_abi_and_requires_both_overlay_contracts(self):
        build=dict(format='s7-native-graphics-build-v2',source_set='effects-v13',compiled_capabilities=15,
                   abi=deepcopy(hook_probe.BUILD_ABI_WATER_PALETTE),
                   diagnostic_abi={'comparison':12,'shadow':12,'ambient':12,'ambient_v2':12,'overlay':12,'overlay_v2':16},
                   build_sha256='a'*64,files={})
        sources,_,valid=qualification._build_contract(build)
        self.assertTrue(valid);self.assertEqual(sources,set(hook_probe.OVERLAY_SOURCES));self.assertEqual(len(sources),45)
        old=deepcopy(build);old['source_set']='effects-v12'
        self.assertFalse(qualification._build_contract(old)[2]);del old['diagnostic_abi']['overlay_v2']
        self.assertTrue(qualification._build_contract(old)[2])

if __name__=='__main__':unittest.main()
