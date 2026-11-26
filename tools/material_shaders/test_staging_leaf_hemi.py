from contextlib import contextmanager,ExitStack
from pathlib import Path
import shutil,struct,subprocess,sys,tempfile,unittest
from unittest.mock import patch
from tools.material_shaders import recipes,shaderlib,staging,staging_v2,staging_leaf_hemi,leaf_hemi,leaf_lobe
from tools.material_shaders.test_staging_v2 import authored as previous_authored,library
from tools.material_shaders import test_staging_v2 as previous_tests
from tools.material_shaders.test_staging import settings,DISPLAY,HISTORY,GOLD
from tools.material_shaders.test_leaf_hemi import vertex,pixel

@contextmanager
def authored():
    with previous_authored()as resources:
        originals={}
        for vendor in('ATI','ATIx','NV'):
            name=f'Graphics/Shaders/SpeedTree_{vendor}_High.ShaderLib'
            resources[name]=library([('LeafCardShader',vertex(),pixel())],vendor+' hemi');originals[name]=shaderlib.digest(resources[name])
        with ExitStack()as stack:
            stack.enter_context(patch.multiple(recipes,RESOURCES=originals,ORIGINAL_VERTEX=shaderlib.digest(vertex()),ORIGINAL_PIXEL=shaderlib.digest(pixel())))
            old=dict(staging_v2.PROGRAMS);old_code=leaf_lobe._transform(vertex())[0];old['leaf']=(old['leaf'][0],'vertex',len(old_code),shaderlib.digest(old_code))
            code=leaf_hemi.transform(vertex(),pixel())[0];new={**old,'leaf':(staging_leaf_hemi.PROGRAMS['leaf'][0],'vertex',len(code),shaderlib.digest(code))}
            stack.enter_context(patch.object(staging_v2,'PROGRAMS',old));stack.enter_context(patch.object(staging_leaf_hemi,'PROGRAMS',new))
            yield resources

class LeafHemisphereStagingTests(unittest.TestCase):
    def prepare(self,root,**changes):
        chosen=changes.pop('selection',settings(render_scale=150,improved_vegetation=True))
        kw=dict(config_version=2,available_config_versions=(2,),worker_overhead_bytes=392,
            available_features=('supersampling','improved_vegetation'),available_scales=(100,150),native_report=root/'LocalData/native.json',verified_platform=HISTORY)
        kw.update(changes);return staging_leaf_hemi.prepare(root,chosen,DISPLAY,**kw)
    def test_same6180_wire2_explicit_leaf_path_and_old_cache_preserved(self):
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            root,source=previous_tests.StagingV2Tests().recipient(folder,resources);original=source.read_bytes()
            old=previous_tests.StagingV2Tests().prepare(root,selection=settings(render_scale=150,improved_vegetation=True))
            saved={p:p.read_bytes()for p in(root/'LocalData').rglob('*')if p.is_file()}
            new=self.prepare(root);self.assertEqual(struct.unpack_from('<3I',new['config']),(6180,2,4));self.assertEqual(len(new['config']),6180)
            self.assertEqual(new['leaf_recipe_variant'],3);self.assertEqual(set(new['artifacts']),{'leaf'})
            self.assertIn('leaf-hemisphere-contrast-v3',new['artifacts']['leaf']['program_relative_path'])
            self.assertNotEqual(old['artifacts']['leaf']['program_relative_path'],new['artifacts']['leaf']['program_relative_path'])
            self.assertEqual(new['config'][1060:3108],bytes(2048));self.assertEqual(new['config'][4132:],bytes(2048))
            for p,data in saved.items():self.assertEqual(p.read_bytes(),data)
            again=previous_tests.StagingV2Tests().prepare(root,selection=settings(render_scale=150,improved_vegetation=True));self.assertEqual(old['config'],again['config'])
            self.assertEqual(source.read_bytes(),original);self.assertEqual(len(new['source_receipts']),3);self.assertFalse(new['runtime_qualified'])
    def test_mixed_or_wrong_version_rejected_before_filesystem(self):
        with patch.object(staging,'plain',side_effect=AssertionError('filesystem')):
            for changes in({'selection':settings(improved_terrain=True,improved_vegetation=True)},
                            {'selection':settings(improved_water=True,improved_vegetation=True)},
                            {'selection':settings(improved_terrain=True)},{'config_version':1},{'available_config_versions':(1,)}):
                with self.subTest(changes=changes),self.assertRaises(ValueError):self.prepare(Path('absent'),**changes)
    def test_bad_last_alias_refuses_all_writes(self):
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            bad=dict(resources);bad['Graphics/Shaders/SpeedTree_NV_High.ShaderLib']+=b'unknown';root,_=previous_tests.StagingV2Tests().recipient(folder,bad)
            with self.assertRaises(ValueError):self.prepare(root)
            self.assertFalse((root/'LocalData').exists())
    def test_gold_layout_explicit_and_disabled_paths_zero(self):
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            root,_=previous_tests.StagingV2Tests().recipient(folder,resources,gold=True)
            result=self.prepare(root,verified_platform=GOLD);self.assertEqual(result['source_archive_relative_path'],'Data/Base/bba/gfx.bba')
            self.assertEqual(struct.unpack_from('<3I',result['config']),(6180,2,4))
    def test_public_module_set_imports_with_no_repository_on_path(self):
        repo=Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory()as folder:
            launcher=Path(folder)/'Launcher';package=launcher/'material_shaders';package.mkdir(parents=True)
            for name in staging_leaf_hemi.PUBLIC_MODULES:
                self.assertFalse(name.startswith('test_'));shutil.copyfile(Path(staging_leaf_hemi.__file__).parent/name,package/name)
            shutil.copyfile(repo/'tools/lan/bba.py',launcher/'bba.py')
            shutil.copyfile(repo/'tools/installer/native_graphics_settings.py',launcher/'native_graphics_settings.py')
            run=subprocess.run([sys.executable,'-I','-B','-c','import sys;sys.path.insert(0,sys.argv[1]);import material_shaders.staging_leaf_hemi as s;print(s.PROGRAMS["leaf"][0])',str(launcher)],capture_output=True,text=True,timeout=20)
            self.assertEqual(run.returncode,0,run.stderr);self.assertEqual(run.stdout.strip(),'leaf-hemisphere-contrast-v3')

if __name__=='__main__':unittest.main()
