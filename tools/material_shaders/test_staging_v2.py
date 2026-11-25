"""Authored resources only; explicit V2 gates, preflight and immutable caches."""
from contextlib import ExitStack,contextmanager
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from tools.material_shaders import inventory,leaf_lobe,recipes,shaderlib,staging,staging_v2,terrain,terrain_distance
from tools.material_shaders.test_leaf_lobe import fixture as leaf_vertex
from tools.material_shaders.test_terrain import fixture as terrain_pixel
from tools.material_shaders.test_terrain_distance import fixture as terrain_vertex
from tools.material_shaders.test_shaderlib import encoded,shader
from tools.material_shaders.test_staging import settings,DISPLAY,HISTORY,GOLD


def library(records,label):
    result=b'FILEVERSION = 2.3.0.0;\nPLATFORM = DX9;\nSOURCECODE=$FIXTURE{\n// '+label.encode()+b'\n}$FIXTURE\n'
    for name,vertex,pixel in records:
        result+=b'SHADER '+name.encode()+b' {\nvs_bytecode[vs_3_0]={'+encoded(vertex)+b'}\nps_bytecode[ps_3_0]={'+encoded(pixel)+b'}\n}\n'
    return result


def archive_fixture(resources):
    payload=bytearray();entries=bytearray()
    for name,data in resources.items():
        spelling=name.replace('/','\\').encode('ascii')+b'\0'
        padded=((len(spelling)+3)&~3)+(4 if len(spelling)%4==0 else 0)
        values=[0]*16;values[2]=len(data);values[3]=inventory.bba.crc32(data)
        values[6]=32+len(payload);values[8]=len(data);values[12]=len(spelling)
        entries+=struct.pack('<16I',*values)+spelling+bytes(padded-len(spelling));payload+=data
    directory=bytearray(276);struct.pack_into('<3I',directory,0,0,272,276+len(entries))
    struct.pack_into('<I',directory,272,len(resources));directory+=entries+struct.pack('<I',0)
    encrypted=inventory.bba.crypt(directory,inventory.bba.FILE_KEYS,1532281822,8,True)
    header=inventory.bba.crypt(struct.pack('<4I',32+len(payload),0,len(encrypted),inventory.bba.crc32(encrypted)),inventory.bba.HEAD_KEYS,1231853475,7,True)
    return b'BAF\x06'+struct.pack('<3I',0,16,0)+header+payload+encrypted


@contextmanager
def authored():
    resources={};leaf={};ground={};water={}
    for vendor in ('ATI','ATIx','NV'):
        name=f'Graphics/Shaders/SpeedTree_{vendor}_High.ShaderLib'
        resources[name]=library([('LeafCardShader',leaf_vertex(),shader())],vendor+' leaf');leaf[name]=shaderlib.digest(resources[name])
        name=f'Graphics/Shaders/Terrain_{vendor}_High.ShaderLib'
        resources[name]=library([(role,terrain_vertex(role),terrain_pixel())for role in terrain_distance.LAYOUTS],vendor+' terrain');ground[name]=shaderlib.digest(resources[name])
        name=f'Graphics/Shaders/Water_{vendor}_High.ShaderLib'
        resources[name]=library([('RiverColor',shader('vertex'),shader())],vendor+' water');water[name]=shaderlib.digest(resources[name])
    pairs={role:(shaderlib.digest(terrain_vertex(role)),shaderlib.digest(terrain_pixel()))for role in terrain_distance.LAYOUTS}
    replacements={'leaf':leaf_lobe._transform(leaf_vertex())[0],
                  'bump_vertex':terrain_distance._transform(terrain_vertex('BumpLayerShader'),'BumpLayerShader')[0],
                  'parallax_vertex':terrain_distance._transform(terrain_vertex('ParallaxLayerShader'),'ParallaxLayerShader')[0],
                  'bump_pixel':terrain.extend_normal_detail(terrain_pixel())[0],
                  'parallax_pixel':terrain.extend_normal_detail(terrain_pixel())[0]}
    programs={role:(staging_v2.PROGRAMS[role][0],staging_v2.PROGRAMS[role][1],len(code),shaderlib.digest(code))for role,code in replacements.items()}
    with ExitStack()as stack:
        stack.enter_context(patch.multiple(recipes,RESOURCES=leaf,TERRAIN_RESOURCES=ground,TERRAIN_PAIRS=pairs,
                                          ORIGINAL_VERTEX=shaderlib.digest(leaf_vertex()),ORIGINAL_PIXEL=shaderlib.digest(shader())))
        stack.enter_context(patch.multiple(staging,RIVER_VERTEX=shaderlib.digest(shader('vertex')),RIVER_PIXEL=shaderlib.digest(shader())))
        stack.enter_context(patch.multiple(staging_v2,PROGRAMS=programs,WATER_RESOURCES=water))
        yield resources


class StagingV2Tests(unittest.TestCase):
    def recipient(self,folder,resources,gold=False):
        root=Path(folder)/'recipient';path=root/('Data/Base/bba/gfx.bba'if gold else'Base/BBA/gfx.bba')
        path.parent.mkdir(parents=True);path.write_bytes(archive_fixture(resources));return root,path

    def prepare(self,root,**changes):
        kwargs=dict(config_version=2,available_config_versions=(2,),worker_overhead_bytes=512,available_features=('supersampling','improved_water','improved_terrain','improved_vegetation'),
                    available_scales=(100,150),native_report=root/'LocalData/native.json',verified_platform=HISTORY)
        selected=changes.pop('selection',settings(render_scale=150,improved_water=True,improved_terrain=True,improved_vegetation=True))
        kwargs.update(changes);return staging_v2.prepare(root,selected,DISPLAY,**kwargs)

    def test_explicit_v2_and_scale_capabilities_refuse_before_source_io(self):
        with patch.object(staging,'plain',side_effect=AssertionError('unexpected filesystem work')):
            for changes in ({'config_version':1},{'available_config_versions':(1,)},{'available_config_versions':(True,)},
                            {'worker_overhead_bytes':2013},{'worker_overhead_bytes':True},{'worker_overhead_bytes':0},
                            {'available_features':()},{'selection':settings(render_scale=200,improved_terrain=True)}):
                with self.subTest(changes=changes),self.assertRaises(ValueError):self.prepare(Path('absent'),**changes)
            self.assertIsNone(self.prepare(Path('absent'),selection=settings(),available_features=(),available_scales=(),available_config_versions=()))

    def test_all_aliases_generate_five_immutable_programs_exact_6180_transport(self):
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            root,source=self.recipient(folder,resources);original=source.read_bytes();result=self.prepare(root)
            self.assertEqual(len(result['config']),6180);self.assertLessEqual(len(result['config']),8192)
            self.assertEqual(result['worker_payload_bytes'],6692)
            self.assertEqual(struct.unpack_from('<5I',result['config']),(6180,2,7,1920,1080))
            self.assertEqual(len(result['artifacts']),5)
            for index,role in enumerate(staging_v2.PATH_ORDER):
                value=result['config'][36+index*1024:36+(index+1)*1024].decode('utf-16-le').rstrip('\0')
                path=Path(value);self.assertTrue(path.is_relative_to(root/'LocalData'))
                if role!='report':self.assertTrue(path.is_file())
            for item in result['artifacts'].values():self.assertEqual(len(item['source_receipt_paths']),3)
            second=self.prepare(root);self.assertEqual(second['config'],result['config'])
            self.assertEqual(source.read_bytes(),original);self.assertFalse(result['active']);self.assertFalse(result['runtime_qualified'])
            self.assertTrue(result['all_vendor_aliases_checked']);self.assertEqual(result['required_shader_quality'],'high')

    def test_last_bad_vendor_source_leaves_no_cache_after_earlier_generation(self):
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            bad=dict(resources);bad['Graphics/Shaders/SpeedTree_NV_High.ShaderLib']+=b'unknown'
            root,_=self.recipient(folder,bad)
            with self.assertRaisesRegex(ValueError,'resource identity'):self.prepare(root)
            self.assertFalse((root/'LocalData').exists())

    def test_water_only_checks_all_aliases_and_creates_report_directory_without_programs(self):
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            root,_=self.recipient(folder,resources)
            result=self.prepare(root,selection=settings(improved_water=True))
            self.assertEqual(struct.unpack_from('<3I',result['config']),(6180,2,1))
            self.assertFalse(result['artifacts']);self.assertEqual(len(result['source_receipts']),3)
            self.assertTrue((root/'LocalData').is_dir());self.assertFalse((root/'LocalData/MaterialShaders').exists())
            self.assertEqual(result['config'][36:3108],bytes(3072));self.assertEqual(result['config'][4132:],bytes(2048))

    def test_private_case_uses_its_own_localdata_and_preserves_recipient_source(self):
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            repo=Path(folder);work=repo/'work';work.mkdir();case=work/'diagnostic';case.mkdir()
            root,source=self.recipient(work,resources);original=source.read_bytes()
            with patch.object(inventory,'ROOT',repo):
                result=self.prepare(root,native_report=case/'native.json',private_case=True)
            self.assertEqual(Path(result['artifact_root']),case)
            self.assertTrue(all((case/item['program_relative_path']).is_relative_to(case/'LocalData')for item in result['artifacts'].values()))
            self.assertFalse((root/'LocalData').exists());self.assertEqual(source.read_bytes(),original)

    def test_existing_corrupt_receipt_is_found_before_recreating_other_missing_file(self):
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            root,_=self.recipient(folder,resources);first=self.prepare(root)
            missing=root/first['artifacts']['bump_vertex']['program_relative_path'];missing.unlink()
            corrupt=root/first['artifacts']['leaf']['source_receipt_paths'][-1];corrupt.write_bytes(b'bad receipt')
            with self.assertRaisesRegex(ValueError,'immutable cache'):self.prepare(root)
            self.assertFalse(missing.exists());self.assertEqual(corrupt.read_bytes(),b'bad receipt')

    def test_generated_identity_mismatch_fails_before_any_output(self):
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            root,_=self.recipient(folder,resources)
            with patch.object(leaf_lobe,'generate',return_value=recipes.Generated(b'unknown',{'active':False})):
                with self.assertRaisesRegex(ValueError,'generated program identity'):self.prepare(root)
            self.assertFalse((root/'LocalData').exists())

    def test_gold_layout_no_fallback_and_disabled_paths_empty(self):
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            root,_=self.recipient(folder,resources,gold=True)
            with self.assertRaisesRegex(ValueError,'archive is absent'):self.prepare(root)
            result=self.prepare(root,verified_platform=GOLD,selection=settings(improved_vegetation=True))
            self.assertEqual(result['source_archive_relative_path'],'Data/Base/bba/gfx.bba')
            self.assertEqual(struct.unpack_from('<3I',result['config']),(6180,2,4))
            self.assertEqual(result['config'][1060:3108],bytes(2048));self.assertEqual(result['config'][4132:],bytes(2048))

    def test_unknown_platform_or_wrong_private_location_does_not_write(self):
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            root,_=self.recipient(folder,resources)
            for changes in ({'verified_platform':None},{'native_report':root/'Base/BBA/native.json','private_case':True},
                            {'native_report':Path('relative.json')},{'native_report':root/'LocalData/material-effects.private.json'}):
                with self.subTest(changes=changes),self.assertRaises(ValueError):self.prepare(root,**changes)
            self.assertFalse((root/'LocalData').exists())

    def test_existing_report_directory_is_refused_before_cache_writes(self):
        for name in ('native.json','material-effects.private.json'):
            with self.subTest(name=name),tempfile.TemporaryDirectory()as folder,authored()as resources:
                root,_=self.recipient(folder,resources);(root/'LocalData'/name).mkdir(parents=True)
                with self.assertRaisesRegex(ValueError,'report target'):self.prepare(root)
                self.assertFalse((root/'LocalData/MaterialShaders').exists())

    def test_path_worker_bound_and_role_suffix_are_preflighted(self):
        for value,suffix in (('relative/leaf.vs.bin','.vs.bin'),('C:/x/../leaf.vs.bin','.vs.bin'),
                             ('C:/x/leaf:stream.vs.bin','.vs.bin'),('C:/x/leaf.ps.bin','.vs.bin'),
                             ('C:/x/CON.vs.bin','.vs.bin'),('C:/NUL/leaf.vs.bin','.vs.bin'),
                             ('C:/x/COM1.vs.bin','.vs.bin'),('C:/x/LPT\u00b2.vs.bin','.vs.bin'),
                             ('C:/x/'+('a'*256)+'.vs.bin','.vs.bin'),
                             ('C:/'+('a'*100+'/')*3+('\U0001f600'*95)+'.private.json','.private.json'),
                             ('C:/'+('a'*505)+'.vs.bin','.vs.bin')):
            with self.subTest(value=value),self.assertRaises(ValueError):staging_v2._path(value,suffix)
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            root,_=self.recipient(folder,resources)
            with patch.object(staging_v2,'_path',side_effect=ValueError('bounded path preflight')):
                with self.assertRaises(ValueError):self.prepare(root)
            self.assertFalse((root/'LocalData').exists())

    def test_future_public_module_set_imports_without_repository_paths(self):
        repo=Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory()as folder:
            launcher=Path(folder)/'Launcher';package=launcher/'material_shaders';package.mkdir(parents=True)
            for name in staging_v2.PUBLIC_MODULES_V2:
                self.assertFalse(name.startswith('test_'));shutil.copyfile(Path(staging_v2.__file__).parent/name,package/name)
            shutil.copyfile(repo/'tools/lan/bba.py',launcher/'bba.py')
            shutil.copyfile(repo/'tools/installer/native_graphics_settings.py',launcher/'native_graphics_settings.py')
            run=subprocess.run([sys.executable,'-I','-B','-c','import sys;sys.path.insert(0,sys.argv[1]);import material_shaders.staging_v2 as s;print(s.CONFIG_BYTES)',str(launcher)],capture_output=True,text=True,timeout=20)
            self.assertEqual(run.returncode,0,run.stderr);self.assertEqual(run.stdout.strip(),'6180')


if __name__=='__main__':unittest.main()
