import ast
from pathlib import Path
import unittest
from unittest.mock import patch
from tools.graphics_lab import shader_profiles as profiles
from tools.material_shaders import leaf_palette,staging_leaf_palette,staging_macro

class ShaderProfileTests(unittest.TestCase):
    def test_fixed_enum_and_real_choice_counts(self):
        self.assertEqual({key:value['id']for key,value in profiles.PROFILES.items()},
                         dict(water=1,vegetation=2,terrain=3,shadow=4,ambient=5,all=6))
        self.assertEqual([len(item['choices'])for item in profiles.profiles()],[4,4,2,2,3,0])
        for value in(None,True,False,0,7,'leaf','ao','vegetation,terrain',[],{}):
            with self.subTest(value=value),self.assertRaises(ValueError):profiles.profile(value)
        for row in(-1,2,3,True,'1'):
            with self.subTest(row=row),self.assertRaises(ValueError):profiles.choice('terrain',row)

    def test_leaf_three_actual_pins_curves_and_original_vs_not_hemi(self):
        p=profiles.profile('vegetation')
        self.assertEqual(p['config_version'],4);self.assertEqual(p['wire_bytes'],4120)
        self.assertEqual(tuple(p['fixed_curves']),leaf_palette.CURVES)
        self.assertTrue(p['original_vertex_preserved']);self.assertFalse(p['hemi_stack'])
        for i,entry in enumerate(p['programs']):
            actual=staging_leaf_palette.PROGRAMS['leaf_'+entry['name']]
            self.assertEqual((entry['recipe'],entry['bytes'],entry['sha256']),(actual[0],actual[2],actual[3]))
            self.assertEqual(p['choices'][i+1]['request'],dict(flags=4,selected_variant=i))
        self.assertEqual(len({item['sha256']for item in p['programs']}),3)

    def test_macro_has_only_original_on_and_unit_factor_stager(self):
        p=profiles.profile('terrain');self.assertEqual(p['config_version'],2);self.assertEqual(p['wire_bytes'],6180)
        self.assertEqual([r['request']for r in p['choices']],[dict(flags=0),dict(flags=2)])
        self.assertEqual(p['terrain_detail_scale'],1);self.assertEqual(p['terrain_relief_scale'],1)
        self.assertFalse(p['independent_detail_relief_choices']);self.assertEqual(staging_macro.VARIANT,'terrain-macro-relief-v3')
        plan=profiles.setup('terrain');self.assertEqual(plan['stager_kwargs'],dict(config_version=2))
        self.assertTrue(plan['staging_selection']['improved_terrain']);self.assertFalse(plan['staging_selection']['improved_vegetation'])

    def test_run_kwargs_use_existing_game_signature_and_one_family(self):
        tree=ast.parse(Path(__file__).with_name('game.py').read_text(encoding='utf-8'))
        run=next(n for n in tree.body if isinstance(n,ast.FunctionDef)and n.name=='run')
        arguments={arg.arg for arg in run.args.args+run.args.kwonlyargs}
        for p in profiles.profiles():
            kwargs=p['run_kwargs'];self.assertTrue(set(kwargs)<=arguments)
            self.assertIs(kwargs['high_quality'],True);self.assertIs(kwargs['shader_overlay'],True)
            self.assertEqual(kwargs['shader_profile'],p['key'])
            families=[kwargs.get(k,False)for k in('water_palette','leaf_palette','terrain_macro','shadow_filter','ambient_occlusion')]
            self.assertEqual(sum(families),5 if p['id']==6 else 1)
            self.assertEqual(p['required_diagnostic_abi']['overlay_v3'if p['id']==6 else 'overlay_v2'],16)
        self.assertFalse(profiles.PROFILES['water']['run_kwargs']['effects_initially_off'])
        self.assertEqual(profiles.PROFILES['water']['run_kwargs']['water_variant'],'B')
        for key in('vegetation','terrain','shadow','ambient'):
            self.assertTrue(profiles.PROFILES[key]['run_kwargs']['effects_initially_off'])

    def test_ao_actual_profiles_keep_distinct_guard_caps(self):
        p=profiles.profile('ambient')
        self.assertEqual([c['request']['profile_version']for c in p['choices']],[2,1,2])
        self.assertEqual([r['ceiling']for r in p['policies']],[1,1.25])
        self.assertEqual([r['original_max']for r in p['policies']],[1,1.25])
        self.assertEqual(p['wire_bytes'],12)

    def test_setup_is_pure_deepcopied_and_has_no_claimed_capability_or_visual_pass(self):
        with patch('builtins.open',side_effect=AssertionError('IO')):
            plans=[profiles.setup(i)for i in range(1,6)]
        self.assertEqual(plans[0]['initial_choice'],1);self.assertEqual(plans[1]['initial_choice'],0)
        for p in plans:
            self.assertTrue(p['one_family_per_run']);self.assertFalse(p['active']);self.assertFalse(p['capability_verified'])
            self.assertFalse(p['runtime_verified']);self.assertFalse(p['visual_quality_qualified'])
        a=profiles.profile('vegetation');a['choices'][1]['request']['flags']=7;a['run_kwargs']['water_program']=True
        self.assertEqual(profiles.profile('vegetation')['choices'][1]['request']['flags'],4)
        self.assertNotIn('water_program',profiles.profile('vegetation')['run_kwargs'])
        self.assertEqual(profiles.setup('vegetation',initially_off=False)['initial_request'],dict(flags=4,selected_variant=0))
        for scale in(0,125,True,'200'):
            with self.assertRaises(ValueError):profiles.setup('vegetation',render_scale=scale)
        for off in(0,1,'true'):
            with self.assertRaises(ValueError):profiles.setup('vegetation',initially_off=off)

    def test_all_is_explicit_new_scope_not_silent_single_profile_upgrade(self):
        all_run=profiles.PROFILES['all']['run_kwargs'];self.assertEqual(all_run['material_config_version'],5)
        self.assertEqual(all_run['material_effects'],('improved_water','improved_terrain','improved_vegetation'))
        self.assertIs(all_run['effects_initially_off'],True);self.assertEqual(all_run['ambient_profile'],'B')
        plan=profiles.setup('all');self.assertFalse(plan['one_family_per_run']);self.assertIsNone(plan['initial_choice'])
        self.assertEqual(plan['initial_request'],dict(flags=0,selected_variant=0))
        self.assertEqual(plan['initial_group_requests']['water'],dict(enabled=1,selected_variant=0))
        self.assertEqual(plan['initial_group_requests']['terrain']['flags'],0)
        self.assertEqual(plan['profile']['required_abi'],dict(effects_v5=24,initialize_v3=3136))
        with self.assertRaises(ValueError):profiles.choice('all',0)

if __name__=='__main__':unittest.main()
