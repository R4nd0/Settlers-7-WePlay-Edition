import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from tools.material_shaders import recipes,shaderlib,staging
from tools.material_shaders.test_inventory import archive_fixture
from tools.material_shaders.test_recipes import NAME,resource
from tools.material_shaders.test_foliage import vertex_fixture
from tools.material_shaders.test_shaderlib import shader


def settings(**changes):
    result=dict(render_scale=100,improved_water=False,improved_terrain=False,improved_vegetation=False,stable_shadows=False)
    result.update(changes);return result


DISPLAY=dict(width=1280,height=720,mode="windowed")
HISTORY=dict(format="weplay-platform-independent-v1",edition="history",files={})
GOLD=dict(format="weplay-platform-independent-v1",edition="gold",files={})


class RecipientStagingTests(unittest.TestCase):
    def owned(self,root,data=None,name=NAME,gold=False):
        recipient=root/"recipient";archive=recipient/("Data/Base/bba/gfx.bba"if gold else"Base/BBA/gfx.bba");archive.parent.mkdir(parents=True)
        archive.write_bytes(archive_fixture(name=name,data=resource()if data is None else data))
        return recipient,archive

    def compatible(self):
        return patch.multiple(recipes,RESOURCES={NAME:shaderlib.digest(resource())},
                              ORIGINAL_VERTEX=shaderlib.digest(vertex_fixture()),ORIGINAL_PIXEL=shaderlib.digest(shader()))

    def test_defaults_require_no_capability_or_filesystem_work(self):
        with patch.object(staging,"plain",side_effect=AssertionError("unexpected filesystem work")):
            self.assertIsNone(staging.prepare(Path("absent"),settings(),None,available_features=(),available_scales=(),native_report=Path("absent")))

    def test_unavailable_material_and_unqualified_2x_refused_before_source_reads(self):
        with patch.object(staging,"plain",side_effect=AssertionError("unexpected filesystem work")):
            with self.assertRaisesRegex(ValueError,"unavailable"):
                staging.prepare(Path("absent"),settings(improved_vegetation=True),DISPLAY,
                                available_features=(),available_scales=(100,150),native_report=Path("absent"))
            with self.assertRaisesRegex(ValueError,"unavailable"):
                staging.prepare(Path("absent"),settings(render_scale=200,improved_vegetation=True),DISPLAY,
                                available_features=("supersampling","improved_vegetation"),available_scales=(100,150),native_report=Path("absent"))

    def test_recipient_owned_source_to_localdata_matches_host_transport_contract(self):
        with tempfile.TemporaryDirectory() as folder,self.compatible():
            root,archive=self.owned(Path(folder));original=archive.read_bytes()
            report=root/"LocalData/native.json"
            result=staging.prepare(root,settings(render_scale=150,improved_vegetation=True),DISPLAY,
                available_features=("supersampling","improved_vegetation"),available_scales=(100,150),native_report=report,verified_platform=HISTORY)
            config=result["config"];self.assertEqual(len(config),4132)
            self.assertEqual(struct.unpack_from("<5I",config),(4132,1,4,1920,1080))
            leaf=Path(config[36:1060].decode("utf-16-le").rstrip("\0"))
            self.assertTrue(leaf.is_file()and leaf.is_relative_to(report.parent))
            self.assertEqual(config[1060:3108],bytes(2048))
            self.assertEqual(Path(config[3108:4132].decode("utf-16-le").rstrip("\0")),report.parent/"material-effects.private.json")
            self.assertEqual(archive.read_bytes(),original)
            self.assertFalse(result["active"]);self.assertFalse(result["ocean_enabled"])

    def test_unknown_source_or_incomplete_recipe_writes_no_cache_or_config(self):
        with tempfile.TemporaryDirectory() as folder,self.compatible():
            root,archive=self.owned(Path(folder),data=resource()+b"unknown")
            with self.assertRaises(ValueError):
                staging.prepare(root,settings(improved_vegetation=True),DISPLAY,available_features=("improved_vegetation",),
                    available_scales=(100,),native_report=root/"LocalData/native.json",verified_platform=HISTORY)
            self.assertFalse((root/"LocalData").exists())
        with tempfile.TemporaryDirectory() as folder:
            root,archive=self.owned(Path(folder),name="Graphics/Shaders/Terrain_ATI_High.ShaderLib",data=b"authored fixture")
            with patch.object(recipes,"generate_terrain",side_effect=[recipes.Generated(b"fixture",{}),ValueError("second recipe refused")]):
                with self.assertRaises(ValueError):
                    staging.prepare(root,settings(improved_terrain=True),DISPLAY,available_features=("improved_terrain",),
                        available_scales=(100,),native_report=root/"LocalData/native.json",verified_platform=HISTORY)
            self.assertFalse((root/"LocalData").exists())

    def test_private_output_cannot_enter_source_base_tree(self):
        with tempfile.TemporaryDirectory() as folder,self.compatible():
            fake_repo=Path(folder);work=fake_repo/"work";work.mkdir();root,_=self.owned(work)
            with patch.object(staging.inventory,"ROOT",fake_repo),self.assertRaises(ValueError):
                staging.prepare(root,settings(improved_vegetation=True),DISPLAY,available_features=("improved_vegetation",),
                    available_scales=(100,),native_report=root/"Base/BBA/native.json",verified_platform=HISTORY,private_case=True)

    def test_gold_layout_is_selected_only_by_the_trusted_platform_manifest(self):
        with tempfile.TemporaryDirectory() as folder,self.compatible():
            root,archive=self.owned(Path(folder),gold=True)
            report=root/"LocalData/native.json"
            with self.assertRaises(ValueError):
                staging.prepare(root,settings(improved_vegetation=True),DISPLAY,available_features=("improved_vegetation",),
                    available_scales=(100,),native_report=report,verified_platform=HISTORY)
            self.assertFalse((root/"LocalData").exists())
            result=staging.prepare(root,settings(improved_vegetation=True),DISPLAY,available_features=("improved_vegetation",),
                    available_scales=(100,),native_report=report,verified_platform=GOLD)
            self.assertEqual(result["platform_edition"],"gold")
            self.assertEqual(result["source_archive_relative_path"],"Data/Base/bba/gfx.bba")
            self.assertTrue(archive.is_file())

    def test_active_materials_require_an_explicit_verified_layout(self):
        with patch.object(staging,"plain",side_effect=AssertionError("unexpected path probing")):
            with self.assertRaisesRegex(ValueError,"platform layout"):
                staging.prepare(Path("absent"),settings(improved_water=True),DISPLAY,available_features=("improved_water",),
                    available_scales=(100,),native_report=Path("absent"))

    def test_public_module_whitelist_loads_from_launcher_layout_without_repository_paths(self):
        repo=Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as folder:
            launcher=Path(folder)/"Launcher";package=launcher/"material_shaders";package.mkdir(parents=True)
            for name in staging.PUBLIC_MODULES:
                self.assertTrue(name.endswith(".py")and not name.startswith("test_"))
                shutil.copyfile(Path(staging.__file__).parent/name,package/name)
            shutil.copyfile(repo/"tools/lan/bba.py",launcher/"bba.py")
            shutil.copyfile(repo/"tools/installer/native_graphics_settings.py",launcher/"native_graphics_settings.py")
            script="import sys;sys.path.insert(0,sys.argv[1]);import material_shaders.staging as s;import material_shaders.recipes;print(s.CONFIG_BYTES)"
            run=subprocess.run([sys.executable,"-I","-B","-c",script,str(launcher)],capture_output=True,text=True,timeout=20)
            self.assertEqual(run.returncode,0,run.stderr);self.assertEqual(run.stdout.strip(),"4132")


if __name__=="__main__":unittest.main()
