from contextlib import nullcontext
from copy import deepcopy
from pathlib import Path
import hashlib,json,struct,tempfile,unittest
from unittest.mock import patch
from tools.graphics_lab import game,load_hook,qualification
from tools.supersampling import hook_probe

class CombinedShaderTests(unittest.TestCase):
    def test_combined_choices_cannot_drift_through_external_file_commands(self):
        for operation in ('variant','effects','water','shadow','ambient','comparison'):
            raw=json.dumps(dict(operation=operation,sequence=1,enabled=1)).encode()
            with self.subTest(operation=operation),self.assertRaisesRegex(ValueError,'exclusively'):
                game.native_command(raw,0,combined_ui_only=True)
        self.assertIsNotNone(game.native_command(b'{"operation":"capture","sequence":1}',0,combined_ui_only=True))

    def test_initializer3_is_explicit_and_keeps_old_mutual_exclusion(self):
        with tempfile.TemporaryDirectory()as td:
            base=Path(td);catalog=base/'catalog.bin';catalog.write_bytes(b'catalog fixture')
            args=dict(observer_catalog=catalog,observer_sha256=hashlib.sha256(catalog.read_bytes()).hexdigest(),
                      material_flags=7,shadow_filter=True,ambient_occlusion=True)
            with patch.object(game,'lab_path',side_effect=lambda p:Path(p)):
                raw=load_hook.configuration((3840,2160),(1920,1080),base/'hook.json',combined_scope=True,**args)
                self.assertEqual(len(raw),3136);self.assertEqual(struct.unpack_from('<2I',raw),(3136,3))
                self.assertEqual(struct.unpack_from('<I',raw,1048),(63,))
                self.assertEqual(struct.unpack_from('<I',raw,3132),(1,))
                with self.assertRaisesRegex(ValueError,'exclusive'):load_hook.configuration((3840,2160),(1920,1080),base/'hook.json',**args)
                with self.assertRaisesRegex(ValueError,'Combined scope'):
                    load_hook.configuration((3840,2160),(1920,1080),base/'hook.json',combined_scope=True,**{**args,'material_flags':4})

    def test_new_commands_are_bounded_and_source_declared(self):
        dll=Path('not-read.dll');build=dict(abi={'effects_v5':24},diagnostic_abi={'overlay_v3':16})
        material=struct.pack('<6I',24,5,6,3840,2160,2);overlay=struct.pack('<4I',16,3,1,6)
        with patch.object(load_hook,'verify_build',return_value=(dll,build)),\
             patch.object(load_hook,'file_lease',return_value=nullcontext()),\
             patch.object(load_hook,'_invoke_pinned',return_value=True):
            self.assertTrue(load_hook.invoke(None,dll,'effects',material))
            self.assertTrue(load_hook.invoke(None,dll,'overlay_v3',overlay))
        for op,raw in (('effects',struct.pack('<6I',24,5,7,3840,2160,0)),
                       ('effects',struct.pack('<6I',24,5,6,3840,2160,3)),
                       ('overlay_v3',struct.pack('<4I',16,3,1,5)),('overlay_v2',overlay)):
            with self.subTest(op=op),patch.object(load_hook,'verify_build')as verify:
                with self.assertRaises(ValueError):load_hook.invoke(None,dll,op,raw)
                verify.assert_not_called()
        with patch.object(load_hook,'verify_build',return_value=(dll,{'abi':{},'diagnostic_abi':{}})):
            with self.assertRaisesRegex(ValueError,'combined material'):load_hook.invoke(None,dll,'effects',material)

    def test_all_family_intent_passes_before_io_but_partial_v5_does_not(self):
        valid=dict(shader_profile='all',shader_overlay=True,high_quality=True,
                   hook=Path('not-read.dll'),observer_catalog=Path('not-read.bin'),material_config_version=5,
                   material_effects=('improved_water','improved_terrain','improved_vegetation'),
                   water_program=True,water_palette=True,leaf_palette=True,terrain_macro=True,
                   shadow_filter=True,ambient_occlusion=True,ambient_profile='B')
        with patch.object(game,'lab_path',side_effect=lambda p:p),\
             patch.object(game.platform,'load',side_effect=LookupError('intent passed')):
            with self.assertRaisesRegex(LookupError,'intent passed'):
                game.run(Path('not-read'),Path('not-created'),**valid)
        for extra in ({'water_palette':False},{'terrain_macro':False},{'shadow_filter':False},
                      {'ambient_occlusion':False},{'leaf_palette':False},{'material_config_version':4},
                      {'comparison_uv':True},{'detailed_shadows':True},{'shader_profile':'vegetation'}):
            with self.subTest(extra=extra),patch.object(game.platform,'load')as load:
                with self.assertRaises(ValueError):game.run(Path('not-read'),Path('not-created'),**{**valid,**extra})
                load.assert_not_called()

    def test_schema14_adds_47_sources_without_changing_frozen_schema13(self):
        build=dict(format='s7-native-graphics-build-v2',source_set='effects-v14',compiled_capabilities=15,
                   abi=deepcopy(hook_probe.BUILD_ABI_COMBINED),
                   diagnostic_abi={'comparison':12,'shadow':12,'ambient':12,'ambient_v2':12,'overlay':12,'overlay_v2':16,'overlay_v3':16},
                   build_sha256='a'*64,files={})
        sources,_,valid=qualification._build_contract(build)
        self.assertTrue(valid);self.assertEqual(len(sources),47);self.assertEqual(sources,set(hook_probe.COMBINED_SOURCES))
        old=deepcopy(build);old['source_set']='effects-v13';self.assertFalse(qualification._build_contract(old)[2])
        old['abi']=hook_probe.BUILD_ABI_WATER_PALETTE;del old['diagnostic_abi']['overlay_v3']
        self.assertTrue(qualification._build_contract(old)[2])

if __name__=='__main__':unittest.main()
