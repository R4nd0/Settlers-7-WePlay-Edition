from contextlib import ExitStack,contextmanager
import hashlib,struct,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
from tools.material_shaders import staging_combined as lab,recipes,leaf_palette,terrain_macro,staging
from tools.material_shaders.test_staging_v2 import archive_fixture
from tools.material_shaders.test_staging import settings,DISPLAY,HISTORY

@contextmanager
def authored():
    leaf={f'Graphics/Shaders/SpeedTree_{v}_High.ShaderLib':v.encode()for v in('ATI','ATIx','NV')}
    terrain={f'Graphics/Shaders/Terrain_{v}_High.ShaderLib':(v+'terrain').encode()for v in('ATI','ATIx','NV')}
    resources={**leaf,**terrain};pins={n:hashlib.sha256(b).hexdigest()for n,b in resources.items()}
    catalog={};programs={}
    for i,role in enumerate(lab.SLOT_ORDER):
        recipe,stage,size,_=lab.PROGRAMS[role];code=bytes([i+1])*size;programs[role]=code;catalog[role]=(recipe,stage,size,hashlib.sha256(code).hexdigest())
    calls=[]
    def generate(name,data,role):
        if hashlib.sha256(data).hexdigest()!=pins[name]:raise ValueError('changed owned source')
        calls.append((name,role));recipe,stage,_,digest=catalog[role]
        pair=(recipes.ORIGINAL_VERTEX,recipes.ORIGINAL_PIXEL)if role.startswith('leaf_')else recipes.TERRAIN_PAIRS['ParallaxLayerShader'if role.startswith('parallax')else'BumpLayerShader']
        return recipes.Generated(programs[role],dict(recipe=recipe,source_resource=name,source_resource_sha256=pins[name],
            original_vertex_sha256=pair[0],original_pixel_sha256=pair[1],active=False,**{'generated_'+stage+'_sha256':digest}))
    with ExitStack()as stack:
        stack.enter_context(patch.object(lab,'PROGRAMS',catalog));stack.enter_context(patch.object(recipes,'RESOURCES',{n:pins[n]for n in leaf}));stack.enter_context(patch.object(recipes,'TERRAIN_RESOURCES',{n:pins[n]for n in terrain}))
        stack.enter_context(patch.object(leaf_palette,'generate',side_effect=lambda n,d,i:generate(n,d,'leaf_'+'ABC'[i])))
        stack.enter_context(patch.object(terrain_macro,'generate',side_effect=lambda n,d,r:generate(n,d,('bump'if r=='BumpLayerShader'else'parallax')+'_vertex')))
        stack.enter_context(patch.object(recipes,'generate_terrain',side_effect=lambda n,d,r:generate(n,d,('bump'if r=='BumpLayerShader'else'parallax')+'_pixel')))
        yield resources,calls,programs

class CombinedStageTests(unittest.TestCase):
    def recipient(self,folder,resources,edition='history'):
        root=Path(folder)/'recipient';source=root/staging.LAYOUTS[edition];source.parent.mkdir(parents=True);source.write_bytes(archive_fixture(resources));return root,source
    def prepare(self,root,**changes):
        selected=changes.pop('selection',settings(improved_terrain=True,improved_vegetation=True,render_scale=150))
        opts=dict(config_version=5,available_config_versions=(5,),worker_overhead_bytes=392,available_features=('supersampling','improved_terrain','improved_vegetation'),available_scales=(100,150,200),native_report=root/'LocalData/native.json',verified_platform=HISTORY)
        opts.update(changes);return lab.prepare(root,selected,DISPLAY,**opts)
    def test_all21_preflight_seven_fixed_artifacts_compact_wire_and_immutable_reuse(self):
        with tempfile.TemporaryDirectory()as folder,authored()as(resources,calls,programs):
            root,source=self.recipient(folder,resources);original=source.read_bytes();a=self.prepare(root)
            self.assertEqual(len(calls),21);self.assertEqual(len(a['source_receipts']),21);self.assertEqual(len(a['artifacts']),7)
            self.assertEqual(struct.unpack('<6I',a['config']),(24,5,0,1920,1080,0));self.assertEqual(a['material_flags'],6);self.assertEqual(a['worker_payload_bytes'],416)
            self.assertEqual([p['role']for p in a['program_slots']],list(lab.SLOT_ORDER))
            for slot in a['program_slots']:self.assertEqual((root/slot['program_relative_path']).read_bytes(),programs[slot['role']])
            files={p:p.read_bytes()for p in(root/'LocalData').rglob('*')if p.is_file()};b=self.prepare(root,enabled_flags=6,selected_variant=2)
            self.assertEqual(a['config'][:8],b['config'][:8]);self.assertEqual(a['config'][12:20],b['config'][12:20]);self.assertEqual(files,{p:p.read_bytes()for p in files});self.assertEqual(source.read_bytes(),original)
    def test_late_terrain_failure_writes_no_earlier_leaf_programs(self):
        with tempfile.TemporaryDirectory()as folder,authored()as(resources,_,_):
            resources=dict(resources);resources['Graphics/Shaders/Terrain_NV_High.ShaderLib']=b'changed';root,_=self.recipient(folder,resources)
            with self.assertRaises(ValueError):self.prepare(root)
            self.assertFalse((root/'LocalData').exists())
    def test_old_capabilities_bad_flags_and_partial_families_refused_before_filesystem(self):
        with patch.object(staging,'plain',side_effect=AssertionError('IO')):
            for change in({'config_version':4},{'available_config_versions':(4,)},{'enabled_flags':1},{'enabled_flags':True},{'selected_variant':3},{'worker_overhead_bytes':8169},
                           {'selection':settings(improved_vegetation=True)},{'selection':settings(improved_terrain=True,improved_vegetation=True,improved_water=True)}):
                with self.subTest(change=change),self.assertRaises(ValueError):self.prepare(Path('absent'),**change)
    def test_corrupt_last_receipt_blocks_recreating_other_missing_program(self):
        with tempfile.TemporaryDirectory()as folder,authored()as(resources,_,_):
            root,_=self.recipient(folder,resources);a=self.prepare(root);missing=root/a['program_slots'][0]['program_relative_path'];missing.unlink()
            last=a['artifacts']['parallax_pixel']['source_receipt_paths'][-1];(root/last).write_bytes(b'corrupt')
            with self.assertRaises(ValueError):self.prepare(root)
            self.assertFalse(missing.exists())

if __name__=='__main__':unittest.main()
