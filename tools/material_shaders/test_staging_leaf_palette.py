from contextlib import ExitStack,contextmanager
from pathlib import Path
import struct,tempfile,unittest
from unittest.mock import patch
from tools.material_shaders import leaf_palette,recipes,shaderlib,semantics,staging,staging_leaf_palette
from tools.material_shaders.test_leaf_wrapped import fixture
from tools.material_shaders.test_shaderlib import shader
from tools.material_shaders.test_staging_v2 import library,archive_fixture
from tools.material_shaders.test_staging import settings,DISPLAY,HISTORY

@contextmanager
def authored():
    resources={};pins={}
    for vendor in('ATI','ATIx','NV'):
        name=f'Graphics/Shaders/SpeedTree_{vendor}_High.ShaderLib';resources[name]=library([('LeafCardShader',shader('vertex'),fixture())],vendor+' palette');pins[name]=shaderlib.digest(resources[name])
    with ExitStack()as stack:
        stack.enter_context(patch.multiple(recipes,RESOURCES=pins,ORIGINAL_VERTEX=shaderlib.digest(shader('vertex')),ORIGINAL_PIXEL=shaderlib.digest(fixture())))
        stack.enter_context(patch.object(semantics,'reflect',return_value=[]))
        programs={}
        for i,name in enumerate('ABC'):
            generated=leaf_palette.generate(next(iter(resources)),next(iter(resources.values())),i)
            programs['leaf_'+name]=(leaf_palette.RECIPES[i],'pixel',len(generated.program),shaderlib.digest(generated.program))
        stack.enter_context(patch.object(staging_leaf_palette,'PROGRAMS',programs));yield resources

class PaletteStagingTests(unittest.TestCase):
    def prepare(self,root,**changes):
        kw=dict(config_version=4,available_config_versions=(4,),worker_overhead_bytes=392,available_features=('supersampling','improved_vegetation'),available_scales=(100,150),native_report=root/'LocalData/native.json',verified_platform=HISTORY)
        selected=changes.pop('selection',settings(render_scale=150,improved_vegetation=True));kw.update(changes)
        return staging_leaf_palette.prepare(root,selected,DISPLAY,**kw)
    def recipient(self,folder,resources):
        root=Path(folder)/'recipient';source=root/'Base/BBA/gfx.bba';source.parent.mkdir(parents=True);source.write_bytes(archive_fixture(resources));return root,source
    def test_exact_wire_allthree_prepared_once_selection_does_not_rewrite_files(self):
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            root,source=self.recipient(folder,resources);original=source.read_bytes();a=self.prepare(root)
            self.assertEqual(len(a['config']),4120);self.assertEqual(struct.unpack_from('<6I',a['config']),(4120,4,4,1920,1080,0))
            self.assertEqual(a['worker_payload_bytes'],4512);self.assertEqual(len(a['source_receipts']),9)
            self.assertEqual([p['name']for p in a['palette_variants']],list('ABC'))
            files={p:p.read_bytes()for p in(root/'LocalData/MaterialShaders').rglob('*')if p.is_file()}
            b=self.prepare(root,selected_variant=2)
            self.assertEqual(a['config'][:20],b['config'][:20]);self.assertEqual(a['config'][24:],b['config'][24:])
            self.assertEqual(b['leaf_recipe_variant'],7);self.assertEqual(source.read_bytes(),original)
            self.assertEqual(files,{p:p.read_bytes()for p in files})
            for i in range(3):
                path=a['config'][24+1024*i:1048+1024*i].decode('utf-16-le').rstrip('\0');self.assertTrue(Path(path).is_file())
    def test_caps_budget_selection_refused_before_filesystem(self):
        with patch.object(staging,'plain',side_effect=AssertionError('filesystem')):
            for changes in({'config_version':3},{'available_config_versions':(3,)},{'worker_overhead_bytes':4073},{'worker_overhead_bytes':True},{'selected_variant':True},{'selected_variant':3},{'selection':settings(improved_terrain=True,improved_vegetation=True)}):
                with self.subTest(changes=changes),self.assertRaises(ValueError):self.prepare(Path('absent'),**changes)
    def test_late_bad_alias_report_and_existing_artifact_fail_before_any_cache_write(self):
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            bad=dict(resources);bad['Graphics/Shaders/SpeedTree_NV_High.ShaderLib']+=b'bad';root,_=self.recipient(folder,bad)
            with self.assertRaises(ValueError):self.prepare(root)
            self.assertFalse((root/'LocalData').exists())
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            root,_=self.recipient(folder,resources);(root/'LocalData/material-effects.private.json').mkdir(parents=True)
            with self.assertRaises(ValueError):self.prepare(root)
            self.assertFalse((root/'LocalData/MaterialShaders').exists())
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            root,_=self.recipient(folder,resources);a=self.prepare(root);path=next((root/'LocalData/MaterialShaders').rglob('*.ps.bin'));path.write_bytes(b'unknown')
            with self.assertRaises(ValueError):self.prepare(root,selected_variant=1)
            self.assertEqual(path.read_bytes(),b'unknown')

if __name__=='__main__':unittest.main()
