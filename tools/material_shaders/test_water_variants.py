from contextlib import contextmanager
import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from tools.material_shaders import recipes,water,water_variants as variants
from tools.material_shaders import test_water_staging as base_tests


@contextmanager
def authored(root):
    resources={f'Graphics/Shaders/Water_{name}_High.ShaderLib':name.encode() for name in ('ATI','ATIx','NV')}
    pins={name:hashlib.sha256(data).hexdigest() for name,data in resources.items()}
    programs={'A':b'A'*3840,'B':b'B'*3840};policies={}
    for number,label in enumerate(('A','B'),1):
        program=programs[label];pin=hashlib.sha256(program).hexdigest();recipe='authored-water-'+label
        def generate(name,data,program=program,pin=pin,recipe=recipe):
            if hashlib.sha256(data).hexdigest()!=pins[name]:raise ValueError('changed source')
            return recipes.Generated(program,dict(recipe=recipe,source_resource=name,source_resource_sha256=pins[name],
                generated_pixel_sha256=pin,active=False,public_binary_distribution_permitted=False))
        policies[label]=dict(id=number,recipe=recipe,sha256=pin,generate=generate)
    with patch.object(variants,'POLICIES',policies),patch.object(water,'RESOURCES',pins), \
            patch.object(variants.inventory,'ROOT',root),patch.object(variants.inventory.bba,'Archive'), \
            patch.object(variants.inventory,'read_payload',side_effect=lambda archive,name:resources[name]):
        yield programs,policies,resources


class FixedWaterVariantsTests(unittest.TestCase):
    def setup_case(self,d):return base_tests.WaterStagingTests.setup_case(self,d)
    def stage(self,source,case,platform,label):
        return variants.prepare(source,(1920,1080),variant=label,native_report=case/'native.private.json',
            verified_platform=platform,available_features=('improved_water',),private_case=True)

    def test_explicit_A_B_use_distinct_pins_recipes_same1076_and_preserve_A(self):
        with tempfile.TemporaryDirectory() as d:
            root,source,case,platform,_=self.setup_case(d)
            with authored(root) as (programs,policies,_):
                a=self.stage(source,case,platform,'A');a_path=Path(a['program_path']);a_bytes=a_path.read_bytes()
                b=self.stage(source,case,platform,'B');b_path=Path(b['program_path'])
                self.assertNotEqual(a_path,b_path);self.assertEqual(a_path.read_bytes(),a_bytes)
                for label,item in (('A',a),('B',b)):
                    self.assertEqual(item['variant_id'],policies[label]['id'])
                    self.assertEqual(Path(item['program_path']).read_bytes(),programs[label])
                    self.assertEqual(len(item['config']),1076)
                    self.assertEqual(struct.unpack_from('<5I',item['config']),(1076,1,1920,1080,1))
                    self.assertEqual(item['config'][-32:],bytes.fromhex(policies[label]['sha256']))
                    self.assertFalse(item['active']);self.assertFalse(item['visible_quality_qualified'])

    def test_wrong_recipe_hash_pair_unknown_variant_and_implicit_alias_refused(self):
        for value in (None,1,2,True,'balanced','C','a'):
            with self.assertRaises(ValueError):variants.configuration((1920,1080),variant=value)
        with tempfile.TemporaryDirectory() as d:
            root,source,case,platform,_=self.setup_case(d)
            with authored(root):
                a=self.stage(source,case,platform,'A')
                with self.assertRaises(ValueError):variants.configuration((1920,1080),variant='B',enabled=True,pixel_path=a['program_path'])

    def test_last_bad_source_or_corrupt_existing_artifact_never_half_writes(self):
        with tempfile.TemporaryDirectory() as d:
            root,source,case,platform,_=self.setup_case(d)
            with authored(root) as (_,_,resources):
                resources[list(resources)[-1]]=b'changed'
                with self.assertRaises(ValueError):self.stage(source,case,platform,'B')
            self.assertFalse((case/'LocalData').exists())
        with tempfile.TemporaryDirectory() as d:
            root,source,case,platform,_=self.setup_case(d)
            with authored(root):
                b=self.stage(source,case,platform,'B');program=Path(b['program_path']);program.unlink()
                next(program.parent.glob('*.json')).write_bytes(b'corrupt')
                with self.assertRaises(ValueError):self.stage(source,case,platform,'B')
                self.assertFalse(program.exists())

    def test_existing_case_checkpoint_and_missing_capability_fail_before_cache(self):
        with tempfile.TemporaryDirectory() as d:
            root,source,case,platform,_=self.setup_case(d)
            with authored(root):
                with self.assertRaises(ValueError):variants.prepare(source,(1920,1080),variant='B',native_report=case/'native.private.json',
                    verified_platform=platform,available_features=(),private_case=True)
                (case/'checkpoint.private.json').write_text('{}')
                with self.assertRaises(ValueError):self.stage(source,case,platform,'B')
            self.assertFalse((case/'LocalData').exists())


if __name__=='__main__':unittest.main()
