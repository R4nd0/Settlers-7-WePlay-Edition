from contextlib import ExitStack,contextmanager
from pathlib import Path
import struct,tempfile,unittest
from unittest.mock import patch
from tools.material_shaders import leaf_wrapped,recipes,shaderlib,semantics,staging,staging_leaf_wrapped
from tools.material_shaders.test_leaf_wrapped import fixture
from tools.material_shaders.test_shaderlib import shader
from tools.material_shaders.test_staging_v2 import library,archive_fixture
from tools.material_shaders.test_staging import settings,DISPLAY,HISTORY

@contextmanager
def authored():
    resources={};pins={}
    for vendor in('ATI','ATIx','NV'):
        name=f'Graphics/Shaders/SpeedTree_{vendor}_High.ShaderLib';resources[name]=library([('LeafCardShader',shader('vertex'),fixture())],vendor+' direct');pins[name]=shaderlib.digest(resources[name])
    with ExitStack()as stack:
        stack.enter_context(patch.multiple(recipes,RESOURCES=pins,ORIGINAL_VERTEX=shaderlib.digest(shader('vertex')),ORIGINAL_PIXEL=shaderlib.digest(fixture())))
        stack.enter_context(patch.object(semantics,'reflect',return_value=[]))
        code,_=leaf_wrapped._transform(fixture());programs={'leaf':('leaf-wrapped-diffuse-v4','pixel',len(code),shaderlib.digest(code))}
        stack.enter_context(patch.object(staging_leaf_wrapped,'PROGRAMS',programs));yield resources

class WrappedStagingTests(unittest.TestCase):
    def prepare(self,root,**changes):
        kw=dict(config_version=3,available_config_versions=(3,),worker_overhead_bytes=392,
            available_features=('supersampling','improved_vegetation'),available_scales=(100,150),native_report=root/'LocalData/native.json',verified_platform=HISTORY)
        selected=changes.pop('selection',settings(render_scale=150,improved_vegetation=True));kw.update(changes)
        return staging_leaf_wrapped.prepare(root,selected,DISPLAY,**kw)
    def recipient(self,folder,resources):
        root=Path(folder)/'recipient';source=root/'Base/BBA/gfx.bba';source.parent.mkdir(parents=True);source.write_bytes(archive_fixture(resources));return root,source
    def test_exact_v3_wire_old_fields_zero_and_immutable_recipient_cache(self):
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            root,source=self.recipient(folder,resources);before=source.read_bytes();a=self.prepare(root)
            self.assertEqual(len(a['config']),7204);self.assertEqual(struct.unpack_from('<3I',a['config']),(7204,3,4))
            self.assertEqual(a['config'][36:3108],bytes(3072));self.assertEqual(a['config'][4132:6180],bytes(2048))
            self.assertTrue(a['config'][6180:].decode('utf-16-le').rstrip('\0').endswith('.ps.bin'))
            self.assertEqual(a['worker_payload_bytes'],7596);self.assertEqual(a['leaf_recipe_variant'],4)
            self.assertEqual(len(a['source_receipts']),3);self.assertTrue(a['original_vertex_shader_preserved'])
            b=self.prepare(root);self.assertEqual(a['config'],b['config']);self.assertEqual(source.read_bytes(),before)
    def test_caps_shape_and_complete_worker_budget_refuse_before_filesystem(self):
        with patch.object(staging,'plain',side_effect=AssertionError('filesystem')):
            for changes in({'config_version':2},{'available_config_versions':(2,)},{'worker_overhead_bytes':989},
                           {'worker_overhead_bytes':True},{'selection':settings(improved_terrain=True,improved_vegetation=True)},
                           {'selection':settings(improved_water=True,improved_vegetation=True)}):
                with self.subTest(changes=changes),self.assertRaises(ValueError):self.prepare(Path('absent'),**changes)
    def test_last_bad_alias_or_bad_report_never_writes_earlier_cache(self):
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            bad=dict(resources);bad['Graphics/Shaders/SpeedTree_NV_High.ShaderLib']+=b'bad';root,_=self.recipient(folder,bad)
            with self.assertRaises(ValueError):self.prepare(root)
            self.assertFalse((root/'LocalData').exists())
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            root,_=self.recipient(folder,resources);(root/'LocalData/material-effects.private.json').mkdir(parents=True)
            with self.assertRaises(ValueError):self.prepare(root)
            self.assertFalse((root/'LocalData/MaterialShaders').exists())
if __name__=='__main__':unittest.main()
