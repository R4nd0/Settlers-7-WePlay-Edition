from contextlib import contextmanager
from pathlib import Path
import hashlib,struct,tempfile,unittest
from unittest.mock import patch
from tools.material_shaders import recipes,water,water_palette as palette,water_palette_staging as stage,staging
from tools.material_shaders.test_staging_v2 import archive_fixture
from tools.material_shaders.test_water_staging import WaterStagingTests

@contextmanager
def authored(root):
    resources={f'Graphics/Shaders/Water_{v}_High.ShaderLib':v.encode()for v in('ATI','ATIx','NV')}
    sources={n:hashlib.sha256(b).hexdigest()for n,b in resources.items()};programs=[bytes([n+1])*3840 for n in range(3)]
    pins=tuple(hashlib.sha256(b).hexdigest()for b in programs)
    def generate(name,data,index):
        if hashlib.sha256(data).hexdigest()!=sources[name]:raise ValueError('changed original')
        return recipes.Generated(programs[index],dict(recipe=palette.POLICIES[index]['recipe'],source_resource=name,source_resource_sha256=sources[name],
            original_vertex_sha256=water.ORIGINAL_VERTEX,original_pixel_sha256=water.ORIGINAL_PIXEL,generated_pixel_sha256=pins[index],active=False,public_binary_distribution_permitted=False))
    with patch.object(water,'RESOURCES',sources),patch.object(palette,'PINS',pins),patch.object(palette,'generate',side_effect=generate)as generated,patch.object(stage.inventory,'ROOT',root):
        yield resources,programs,pins,generated

class PaletteStagingTests(unittest.TestCase):
    def setup(self,folder,edition='history'):
        return WaterStagingTests.setup_case(self,folder,edition)
    def prepare(self,recipient,case,platform,**changes):
        kwargs=dict(native_report=case/'native.private.json',verified_platform=platform,available_features=('improved_water',),available_config_versions=(2,),worker_overhead_bytes=392,private_case=True)
        kwargs.update(changes);return stage.prepare(recipient,(1920,1080),**kwargs)
    def test_all9_aliases_before_three_immutable_programs_exact3096_and_selected_only_changes(self):
        with tempfile.TemporaryDirectory()as d:
            root,recipient,case,platform,archive=self.setup(d)
            with authored(root)as(resources,programs,pins,generated):
                archive.write_bytes(archive_fixture(resources));source=archive.read_bytes();a=self.prepare(recipient,case,platform)
                self.assertEqual(generated.call_count,9);self.assertEqual(len(a['config']),3096)
                self.assertEqual(struct.unpack_from('<6I',a['config']),(3096,2,1920,1080,1,0));self.assertEqual(a['worker_payload_bytes'],3488)
                snapshots={p:p.read_bytes()for p in(case/'LocalData').rglob('*')if p.is_file()}
                b=self.prepare(recipient,case,platform,selected_variant=2)
                self.assertEqual(a['config'][:20],b['config'][:20]);self.assertEqual(a['config'][24:],b['config'][24:]);self.assertEqual(archive.read_bytes(),source)
                self.assertEqual(snapshots,{p:p.read_bytes()for p in snapshots})
                for i,p in enumerate(a['palette_variants']):
                    self.assertEqual(p['sha256'],pins[i]);self.assertEqual(Path(p['program_path']).read_bytes(),programs[i])
                    self.assertEqual(Path(a['config'][24+i*1024:1048+i*1024].decode('utf-16-le').rstrip('\0')),Path(p['program_path']))
                off=stage.configuration((1920,1080),[p['program_path']for p in a['palette_variants']],enabled=False,selected_variant=1)
                self.assertEqual(struct.unpack_from('<2I',off,16),(0,1));self.assertEqual(off[24:],a['config'][24:])
                self.assertFalse(a['active']);self.assertFalse(a['visible_quality_qualified'])
    def test_invalid_capability_budget_index_refuse_before_filesystem(self):
        with patch.object(staging,'plain',side_effect=AssertionError('filesystem')):
            for changes in({'available_config_versions':(1,)},{'available_config_versions':(True,2)},{'available_features':()}, {'worker_overhead_bytes':5097},{'worker_overhead_bytes':True},{'selected_variant':True},{'selected_variant':3}):
                with self.subTest(changes=changes),self.assertRaises(ValueError):self.prepare(Path('x'),Path('y'),{},**changes)
    def test_late_bad_alias_and_bad_existing_receipt_prevent_partial_cache_writes(self):
        with tempfile.TemporaryDirectory()as d:
            root,recipient,case,platform,archive=self.setup(d)
            with authored(root)as(resources,_,_,_):
                archive.write_bytes(archive_fixture({**resources,list(resources)[-1]:b'changed'}))
                with self.assertRaises(ValueError):self.prepare(recipient,case,platform)
                self.assertFalse((case/'LocalData').exists())
        with tempfile.TemporaryDirectory()as d:
            root,recipient,case,platform,archive=self.setup(d)
            with authored(root)as(resources,_,_,_):
                archive.write_bytes(archive_fixture(resources));a=self.prepare(recipient,case,platform);program=Path(a['palette_variants'][0]['program_path']);program.unlink()
                next((case/'LocalData').rglob('*-palette-v2.json')).write_bytes(b'corrupt')
                with self.assertRaises(ValueError):self.prepare(recipient,case,platform)
                self.assertFalse(program.exists())
    def test_gold_layout_old_accepted_B_receipt_preserved_and_closedcase_refused(self):
        with tempfile.TemporaryDirectory()as d:
            root,recipient,case,platform,archive=self.setup(d,'gold')
            with authored(root)as(resources,programs,pins,generated):
                archive.write_bytes(archive_fixture(resources));recipe=case/'LocalData/MaterialShaders'/palette.POLICIES[0]['recipe'];recipe.mkdir(parents=True)
                (recipe/(pins[0]+'.ps.bin')).write_bytes(programs[0]);old=recipe/(pins[0]+'-old-v1.json');old.write_bytes(b'original historical receipt')
                a=self.prepare(recipient,case,platform);self.assertEqual(a['edition'],'gold');self.assertEqual(old.read_bytes(),b'original historical receipt')
                (case/'checkpoint.private.json').write_text('{}');generated.reset_mock()
                with self.assertRaises(ValueError):self.prepare(recipient,case,platform)
                generated.assert_not_called()
    def test_reordered_paths_and_nonboolean_enable_refused(self):
        for enabled in(0,1,None,'true'):
            with self.assertRaises(ValueError):stage.configuration((1920,1080),[],enabled=enabled)
        with tempfile.TemporaryDirectory()as d:
            root,recipient,case,platform,archive=self.setup(d)
            with authored(root)as(resources,_,_,_):
                archive.write_bytes(archive_fixture(resources));a=self.prepare(recipient,case,platform);paths=[p['program_path']for p in a['palette_variants']]
                with self.assertRaises(ValueError):stage.configuration((1920,1080),paths[::-1])

if __name__=='__main__':unittest.main()
