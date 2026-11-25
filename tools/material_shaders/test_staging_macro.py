from contextlib import contextmanager,ExitStack
from pathlib import Path
import struct,tempfile,unittest
from unittest.mock import patch
from tools.material_shaders import recipes,shaderlib,staging_v2,staging_macro,terrain_macro,terrain_distance
from tools.material_shaders.test_staging_v2 import authored as previous_authored,library
from tools.material_shaders import test_staging_v2 as previous_tests
from tools.material_shaders.test_staging import settings,DISPLAY,HISTORY
from tools.material_shaders.test_terrain_macro import fixture
from tools.material_shaders.test_terrain import fixture as pixel

@contextmanager
def authored():
    with previous_authored()as resources:
        ground={}
        for vendor in ("ATI","ATIx","NV"):
            name=f"Graphics/Shaders/Terrain_{vendor}_High.ShaderLib"
            resources[name]=library([(r,fixture(r),pixel())for r in terrain_macro.DISTANCES],vendor+" macro")
            ground[name]=shaderlib.digest(resources[name])
        pairs={r:(shaderlib.digest(fixture(r)),shaderlib.digest(pixel()))for r in terrain_macro.DISTANCES}
        with ExitStack()as stack:
            stack.enter_context(patch.multiple(recipes,TERRAIN_RESOURCES=ground,TERRAIN_PAIRS=pairs))
            old=dict(staging_v2.PROGRAMS);new=dict(staging_v2.PROGRAMS)
            for role,prefix in (("BumpLayerShader","bump"),("ParallaxLayerShader","parallax")):
                code=terrain_distance._transform(fixture(role),role)[0]
                old[prefix+"_vertex"]=(staging_v2.PROGRAMS[prefix+"_vertex"][0],"vertex",len(code),shaderlib.digest(code))
                code=terrain_macro.transform(fixture(role),role)[0]
                new[prefix+"_vertex"]=(staging_macro.PROGRAMS[prefix+"_vertex"][0],"vertex",len(code),shaderlib.digest(code))
            stack.enter_context(patch.object(staging_v2,"PROGRAMS",old))
            stack.enter_context(patch.object(staging_macro,"PROGRAMS",new))
            yield resources

class StagingMacroTests(unittest.TestCase):
    def prepare(self,root,**changes):
        selected=changes.pop("selection",settings(render_scale=150,improved_terrain=True))
        kw=dict(config_version=2,available_config_versions=(2,),worker_overhead_bytes=392,
            available_features=("supersampling","improved_terrain","improved_vegetation"),available_scales=(100,150),
            native_report=root/"LocalData/native.json",verified_platform=HISTORY)
        kw.update(changes);return staging_macro.prepare(root,selected,DISPLAY,**kw)

    def test_exact_wire2_shape_new_paths_and_no_old_cache_repin(self):
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            root,archive=previous_tests.StagingV2Tests().recipient(folder,resources);source=archive.read_bytes()
            old=previous_tests.StagingV2Tests().prepare(root,selection=settings(render_scale=150,improved_terrain=True))
            previous={p:p.read_bytes()for p in (root/"LocalData").rglob("*")if p.is_file()}
            new=self.prepare(root)
            self.assertEqual(len(new["config"]),6180)
            self.assertEqual(struct.unpack_from("<3I",new["config"]),(6180,2,2))
            self.assertEqual(struct.unpack_from("<2f",new["config"],28),(1,1))
            self.assertEqual(new["terrain_recipe_variant"],3)
            self.assertEqual(new["worker_payload_bytes"],6572)
            for role in ("bump_vertex","parallax_vertex"):
                self.assertIn("macro-relief",new["artifacts"][role]["program_relative_path"])
                self.assertNotEqual(old["artifacts"][role]["program_relative_path"],new["artifacts"][role]["program_relative_path"])
            for path,data in previous.items():self.assertEqual(path.read_bytes(),data)
            again=previous_tests.StagingV2Tests().prepare(root,selection=settings(render_scale=150,improved_terrain=True))
            self.assertEqual(again["config"],old["config"]);self.assertEqual(archive.read_bytes(),source)
            self.assertFalse(new["runtime_qualified"])

    def test_wrong_selection_and_version_refuse_before_filesystem_work(self):
        with patch.object(staging_v2.staging,"plain",side_effect=AssertionError("filesystem")):
            for change in ({"selection":settings(improved_vegetation=True)},
                           {"selection":settings(improved_terrain=True,improved_water=True)},
                           {"config_version":1},{"available_config_versions":(1,)}):
                with self.subTest(change=change),self.assertRaises(ValueError):self.prepare(Path("absent"),**change)

    def test_last_bad_alias_refuses_all_cache_writes(self):
        with tempfile.TemporaryDirectory()as folder,authored()as resources:
            bad=dict(resources);bad["Graphics/Shaders/Terrain_NV_High.ShaderLib"]+=b"unknown"
            root,_=previous_tests.StagingV2Tests().recipient(folder,bad)
            with self.assertRaises(ValueError):self.prepare(root)
            self.assertFalse((root/"LocalData").exists())

if __name__=="__main__":unittest.main()
