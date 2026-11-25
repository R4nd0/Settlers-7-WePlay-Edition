import hashlib
import json
from pathlib import Path
import struct
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/installer'))
import native_graphics_runtime as runtime
import native_graphics_loader as loader
import native_graphics_settings as settings


class NativeRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.dll = self.root / runtime.DLL
        self.dll.write_bytes(b'authored fixture, no game code')
        self.pin = hashlib.sha256(self.dll.read_bytes()).hexdigest()
        self.exe = '1' * 64
        self.manifest = self.root / runtime.MANIFEST
        self.manifest.write_text(json.dumps(dict(format=runtime.FORMAT,
            dll=runtime.DLL, sha256=self.pin, abi=2,
            compiled_features=['supersampling','improved_water'])), encoding='utf-8')
        self.admitted = {self.pin: dict(manifest_sha256=loader.digest(self.manifest),
            editions={self.exe: dict(features=['supersampling'],scales=[100,150])})}
        self.selection = dict(native_graphics={**settings.DEFAULTS,'render_scale':150},
                              display=dict(width=1280,height=720,mode='windowed'))

    def test_compiled_support_cannot_qualify_features_editions_or_scales(self):
        with patch.dict(runtime.QUALIFIED_BUILDS,self.admitted,clear=True):
            cap=runtime.payload(self.root,self.exe)
            self.assertEqual(cap.features,('supersampling',))
            self.assertEqual(cap.scales,(100,150))
            with self.assertRaises(ValueError):runtime.payload(self.root,'2'*64)
            with patch.object(runtime,'installed',return_value=cap):
                runtime.require(self.root,self.selection)
                self.selection['native_graphics']['render_scale']=200
                with self.assertRaises(ValueError):runtime.require(self.root,self.selection)
                self.selection['native_graphics']['render_scale']=150
                self.selection['native_graphics']['improved_water']=True
                with self.assertRaises(ValueError):runtime.require(self.root,self.selection)

    def test_changed_manifest_or_dll_cannot_activate(self):
        with patch.dict(runtime.QUALIFIED_BUILDS,self.admitted,clear=True):
            self.manifest.write_text(self.manifest.read_text()+' ',encoding='utf-8')
            with self.assertRaises(ValueError):runtime.payload(self.root,self.exe)
            self.dll.write_bytes(b'changed')
            with self.assertRaises(ValueError):runtime.payload(self.root,self.exe)

    def test_inactive_legacy_never_inspects_payload_or_changes_callback(self):
        callback=object()
        with patch.object(runtime,'installed',side_effect=AssertionError('must not inspect')):
            self.assertIsNone(runtime.require(self.root,{}))
            with patch('graphics_choice._read_choice',return_value={'choice':None}):
                self.assertEqual(runtime.prepare_launch(self.root,('-vSynch',),callback),
                                 (('-vSynch',),callback))

    def test_renderer_and_reshade_conflict_precede_loading(self):
        with patch.object(runtime,'installed',side_effect=AssertionError('must not load')):
            with self.assertRaisesRegex(ValueError,'renderer_required'):
                runtime.require(self.root,self.selection,renderer='native')
            with self.assertRaisesRegex(ValueError,'effect_conflict'):
                runtime.require(self.root,{**self.selection,'enhanced_aa':True})

    def test_material_quality_uses_proposed_settings_then_checks_saved_at_launch(self):
        import quality_settings
        cap=runtime.Capability(self.dll,self.pin,('supersampling','improved_water'),(100,150))
        selected={**self.selection,'native_graphics':{**self.selection['native_graphics'],'improved_water':True}}
        high={**quality_settings.DEFAULTS,'ShaderQuality':2,'WaterQuality':2}
        with patch.object(runtime,'installed',return_value=cap), \
             patch.object(quality_settings,'load',return_value=dict(quality_settings.DEFAULTS)):
            runtime.require(self.root,selected,quality=high)
            with self.assertRaisesRegex(ValueError,'high_quality_required'):
                runtime.require(self.root,selected,check_quality=True)

    def test_package_module_whitelist_and_all_edition_gate(self):
        import install
        from tools.material_shaders.staging import PUBLIC_MODULES
        self.assertEqual(tuple(install.NATIVE_MATERIAL_MODULES),tuple(PUBLIC_MODULES))
        with patch.dict(runtime.QUALIFIED_BUILDS,self.admitted,clear=True):
            with self.assertRaises(ValueError):runtime.distribution(self.root)

    def test_physical_placement_survives_render_override_and_abi(self):
        dimensions=settings.render_size(self.selection['native_graphics'],self.selection['display'])
        original=('-vSynch','-width','1280','-height','720','-x','350','-y','100')
        self.assertEqual(runtime.render_arguments(original,dimensions),
            ('-vSynch','-width','1920','-height','1080','-x','350','-y','100'))
        with self.assertRaises(ValueError):runtime.render_arguments(('-width','1280','-width','720'),dimensions)
        data=runtime.configuration(dimensions,self.root/'report.private.json')
        self.assertEqual(len(data),3132)
        self.assertEqual(struct.unpack_from('<6I',data),(3132,2,1920,1080,1280,720))
        self.assertEqual(data[1048:],bytes(2084))
        self.assertEqual(loader.validate_command('initialize',data),b'WePlaySupersamplingInitialize\0')
        material=runtime.configuration(dimensions,self.root/'material.private.json',material_flags=7)
        self.assertEqual(struct.unpack_from('<I',material,1048)[0],14)
        for flags in (True,-1,8):
            with self.assertRaises(ValueError):runtime.configuration(dimensions,self.root/'report.json',material_flags=flags)

    def test_fixed_worker_and_export_allowlist(self):
        code=loader.worker(0x20000000,0x21000000,0x401000,0x401004,8,200,256,1024)
        self.assertLess(len(code),512)
        self.assertEqual(code[-7:],b'\x61\x9d\x31\xc0\xc2\x04\x00')
        self.assertIn(b'\xff\x15'+struct.pack('<I',0x401000),code)
        for operation,payload in [('arbitrary',bytes(12)),('effects',bytes(4132)),
                                  ('initialize',struct.pack('<2I',3132,1)+bytes(3124))]:
            with self.assertRaises(ValueError):loader.validate_command(operation,payload)
        payload=struct.pack('<2I',4132,1)+bytes(4124)
        self.assertEqual(loader.validate_command('effects',payload),b'WePlayNativeGraphicsConfigureEffects\0')

    def test_startup_callback_is_composed_before_fixed_initialization(self):
        cap=runtime.Capability(self.dll,self.pin,('supersampling',),(100,150))
        dimensions=settings.render_size(self.selection['native_graphics'],self.selection['display'])
        renderer=self.root/'d3d9.dll';renderer.write_bytes(b'renderer fixture')
        events=[]
        with patch('graphics_choice._read_choice',return_value=self.selection), \
             patch.object(runtime,'require',return_value=(cap,dimensions)), \
             patch('graphics_profile.ensure_closed'), \
             patch('platform_standalone.load',return_value=({},self.root/'Settlers7R.exe',None)), \
             patch.object(loader,'DXVK_SHA256',loader.digest(renderer)), \
             patch.object(loader,'invoke',side_effect=lambda *args: events.append(args[3])):
            arguments,callback=runtime.prepare_launch(self.root,(),lambda owned:events.append('owner'))
            callback(object())
        self.assertEqual(arguments,('-width','1920','-height','1080'))
        self.assertEqual(events,['owner','initialize'])

    def test_water_command_accepts_only_fixed_balanced_recipe_and_bounded_abi(self):
        from tools.material_shaders import water_variants, water_balanced
        self.assertEqual(loader.WATER_B_SHA256, water_balanced.PROGRAM_SHA256)
        self.assertEqual(loader.WATER_B_RECIPE, water_balanced.RECIPE_ID)
        program = self.root/'LocalData/MaterialShaders'/water_balanced.RECIPE_ID/(water_balanced.PROGRAM_SHA256+'.ps.bin')
        payload = water_variants.configuration((1920,1080),variant='B',enabled=True,pixel_path=program)
        self.assertEqual(loader.validate_command('water',payload),b'WePlayNativeGraphicsConfigureWater\0')
        bad = []
        for offset, value in ((0,1075),(4,2),(8,3841),(12,599),(16,2)):
            changed=bytearray(payload);struct.pack_into('<I',changed,offset,value);bad.append(bytes(changed))
        bad.extend((payload[:-1],payload+b'\0',payload[:-32]+bytes(32),
                    payload[:20]+bytes(1024)+payload[1044:]))
        changed=bytearray(payload);changed[1042:1044]=b'X\0';bad.append(bytes(changed))
        changed=bytearray(payload);changed[20:22]=b'\0\xd8';bad.append(bytes(changed))
        for value in bad:
            with self.subTest(payload_sha256=hashlib.sha256(value).hexdigest()), self.assertRaises(ValueError):
                loader.validate_command('water',value)
        a=water_variants.POLICIES['A']
        path=self.root/'LocalData/MaterialShaders'/a['recipe']/(a['sha256']+'.ps.bin')
        with self.assertRaises(ValueError):
            loader.validate_command('water',water_variants.configuration((1920,1080),variant='A',enabled=True,pixel_path=path))
        for operation in ('comparison','capture','configure','arbitrary'):
            with self.assertRaises(ValueError):loader.validate_command(operation,payload)

    def test_water_only_prepares_B_and_queues_zero_legacy_effects_before_water(self):
        from tools.material_shaders import water_variants, staging, water_balanced
        selected={**self.selection,'native_graphics':{**self.selection['native_graphics'],'improved_water':True}}
        dimensions=settings.render_size(selected['native_graphics'],selected['display'])
        cap=runtime.Capability(self.dll,self.pin,('supersampling','improved_water'),(100,150))
        program=self.root/'LocalData/MaterialShaders'/water_balanced.RECIPE_ID/(water_balanced.PROGRAM_SHA256+'.ps.bin')
        config=water_variants.configuration((1920,1080),variant='B',enabled=True,pixel_path=program)
        renderer=self.root/'d3d9.dll';renderer.write_bytes(b'renderer fixture')
        platform={'format':'weplay-platform-independent-v1','edition':'history','files':{}}
        events=[]
        with patch('graphics_choice._read_choice',return_value=selected), \
             patch.object(runtime,'require',return_value=(cap,dimensions)), \
             patch('graphics_profile.ensure_closed'), \
             patch('platform_standalone.load',return_value=(platform,self.root/'Settlers7R.exe',None)), \
             patch.object(loader,'DXVK_SHA256',loader.digest(renderer)), \
             patch.object(water_variants,'prepare',return_value={'config':config}) as prepare, \
             patch.object(staging,'prepare',side_effect=AssertionError('No legacy River preparation')), \
             patch.object(loader,'invoke',side_effect=lambda *args:events.append((args[3],args[4]))):
            _,callback=runtime.prepare_launch(self.root,(),lambda owned:events.append(('owner',None)))
            self.assertEqual(events,[])
            callback(object())
        self.assertEqual([operation for operation,_ in events],['owner','initialize','effects','water'])
        self.assertEqual(struct.unpack_from('<I',events[1][1],1048)[0],2)  # Native Water intent, observer off.
        effects=events[2][1]
        self.assertEqual(struct.unpack_from('<5I',effects),(4132,1,0,1920,1080))
        self.assertEqual(effects[36:3108],bytes(3072))
        self.assertEqual(events[3][1],config)
        self.assertEqual(prepare.call_args.kwargs['variant'],'B')
        self.assertEqual(prepare.call_args.kwargs['available_features'],cap.features)
        self.assertEqual(selected['native_graphics']['improved_water'],True)

    def test_water_and_terrain_stage_independently_without_legacy_River_flag(self):
        from tools.material_shaders import water_variants, staging, water_balanced
        selected={**self.selection,'native_graphics':{**self.selection['native_graphics'],
                  'improved_water':True,'improved_terrain':True}}
        dimensions=settings.render_size(selected['native_graphics'],selected['display'])
        cap=runtime.Capability(self.dll,self.pin,('supersampling','improved_water','improved_terrain'),(150,))
        program=self.root/'LocalData/MaterialShaders'/water_balanced.RECIPE_ID/(water_balanced.PROGRAM_SHA256+'.ps.bin')
        water=water_variants.configuration((1920,1080),variant='B',enabled=True,pixel_path=program)
        effects=struct.pack('<5I',4132,1,2,1920,1080)+bytes(4112)
        renderer=self.root/'d3d9.dll';renderer.write_bytes(b'renderer fixture')
        calls=[]
        with patch('graphics_choice._read_choice',return_value=selected), \
             patch.object(runtime,'require',return_value=(cap,dimensions)), \
             patch('graphics_profile.ensure_closed'), \
             patch('platform_standalone.load',return_value=({},self.root/'Settlers7R.exe',None)), \
             patch.object(loader,'DXVK_SHA256',loader.digest(renderer)), \
             patch.object(staging,'prepare',return_value={'config':effects}) as legacy, \
             patch.object(water_variants,'prepare',return_value={'config':water}), \
             patch.object(loader,'invoke',side_effect=lambda *args:calls.append((args[3],args[4]))):
            _,callback=runtime.prepare_launch(self.root,(),None)
            callback(object())
        self.assertFalse(legacy.call_args.args[1]['improved_water'])
        self.assertTrue(legacy.call_args.args[1]['improved_terrain'])
        self.assertTrue(selected['native_graphics']['improved_water'])
        self.assertEqual([operation for operation,_ in calls],['initialize','effects','water'])
        self.assertEqual(calls[1][1],effects)
        self.assertEqual(struct.unpack_from('<I',calls[0][1],1048)[0],6)

    def test_unqualified_water_cannot_prepare_files_or_return_an_initializer(self):
        from tools.material_shaders import water_variants
        selected={**self.selection,'native_graphics':{**self.selection['native_graphics'],'improved_water':True}}
        with patch('graphics_choice._read_choice',return_value=selected), \
             patch.dict(runtime.QUALIFIED_BUILDS,{},clear=True), \
             patch.object(runtime,'installed',side_effect=ValueError('native_graphics_unavailable')), \
             patch.object(water_variants,'prepare',side_effect=AssertionError('must not stage')):
            with self.assertRaisesRegex(ValueError,'native_graphics_unavailable'):
                runtime.prepare_launch(self.root,(),None)
        self.assertFalse((self.root/'LocalData').exists())

    def test_failed_water_preparation_never_loads_a_partial_native_configuration(self):
        from tools.material_shaders import water_variants
        selected={**self.selection,'native_graphics':{**self.selection['native_graphics'],'improved_water':True}}
        dimensions=settings.render_size(selected['native_graphics'],selected['display'])
        cap=runtime.Capability(self.dll,self.pin,('supersampling','improved_water'),(150,))
        renderer=self.root/'d3d9.dll';renderer.write_bytes(b'renderer fixture')
        with patch('graphics_choice._read_choice',return_value=selected), \
             patch.object(runtime,'require',return_value=(cap,dimensions)), \
             patch('graphics_profile.ensure_closed'), \
             patch('platform_standalone.load',return_value=({},self.root/'Settlers7R.exe',None)), \
             patch.object(loader,'DXVK_SHA256',loader.digest(renderer)), \
             patch.object(water_variants,'prepare',side_effect=ValueError('Unsupported recipient resource')), \
             patch.object(loader,'invoke',side_effect=AssertionError('must not load')):
            with self.assertRaisesRegex(ValueError,'Unsupported recipient resource'):
                runtime.prepare_launch(self.root,(),None)

    def test_packaged_water_dependency_whitelist_imports_without_repository_tools(self):
        import install
        repository=Path(__file__).resolve().parents[1]
        launcher=self.root/'Launcher';modules=launcher/'material_shaders';modules.mkdir(parents=True)
        for name in install.NATIVE_MATERIAL_MODULES:
            shutil.copyfile(repository/'tools/material_shaders'/name,modules/name)
        shutil.copyfile(repository/'tools/lan/bba.py',launcher/'bba.py')
        result=subprocess.run([sys.executable,'-I','-S','-c',
            'import sys;sys.path.insert(0,sys.argv[1]);from material_shaders import water_variants;'
            'assert water_variants.POLICIES["B"]["sha256"]==sys.argv[2]',str(launcher),loader.WATER_B_SHA256],
            capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stderr)


if __name__=='__main__':unittest.main()
