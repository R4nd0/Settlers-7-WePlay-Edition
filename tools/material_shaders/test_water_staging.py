from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from tools.material_shaders import recipes, water, water_staging as stage


@contextmanager
def authored(root):
    resources={f'Graphics/Shaders/Water_{vendor}_High.ShaderLib':vendor.encode()
               for vendor in ('ATI','ATIx','NV')}
    pins={name:hashlib.sha256(data).hexdigest() for name,data in resources.items()}
    program=bytes.fromhex('0003ffff')+b'\0'*(3840-8)+bytes.fromhex('ffff0000')
    digest=hashlib.sha256(program).hexdigest()
    def generate(name,data):
        if name not in pins or hashlib.sha256(data).hexdigest()!=pins[name]:
            raise ValueError('Changed owned source')
        return recipes.Generated(program,dict(recipe=water.RECIPE_ID,source_resource=name,
            source_resource_sha256=pins[name],generated_pixel_sha256=digest,
            active=False,public_binary_distribution_permitted=False))
    with patch.object(water,'RESOURCES',pins),patch.object(water,'generate',side_effect=generate) as generated, \
            patch.object(stage,'PROGRAM_SHA256',digest),patch.object(stage.inventory,'ROOT',root), \
            patch.object(stage.inventory.bba,'Archive'), \
            patch.object(stage.inventory,'read_payload',side_effect=lambda archive,name:resources[name]):
        yield program,digest,generated,resources


class WaterStagingTests(unittest.TestCase):
    def setup_case(self, directory, edition='history'):
        root=Path(directory);work=root/'work';work.mkdir()
        recipient=work/'recipient';recipient.mkdir();(recipient/'LocalData').mkdir()
        relative=stage.staging.LAYOUTS[edition]
        archive=recipient/relative;archive.parent.mkdir(parents=True);archive.write_bytes(b'authored archive fixture')
        case=work/'next-case';case.mkdir()
        platform=dict(format='weplay-platform-independent-v1',edition=edition,files={})
        return root,recipient,case,platform,archive

    def prepare(self, recipient, case, platform, **changes):
        options=dict(native_report=case/'native.private.json',verified_platform=platform,
                     available_features=('improved_water',),private_case=True)
        options.update(changes)
        return stage.prepare(recipient,(1920,1080),**options)

    def test_all_aliases_before_immutable_private_program_and_exact1076_transport(self):
        with tempfile.TemporaryDirectory() as d:
            root,recipient,case,platform,archive=self.setup_case(d)
            original=archive.read_bytes()
            with authored(root) as (program,digest,generated,_):
                value=self.prepare(recipient,case,platform)
                self.assertEqual(generated.call_count,3)
                path=Path(value['program_path']);self.assertEqual(path.read_bytes(),program)
                self.assertTrue(path.is_relative_to(case/'LocalData'))
                self.assertFalse((recipient/'LocalData/MaterialShaders').exists())
                self.assertEqual(len(value['config']),1076)
                self.assertEqual(struct.unpack_from('<5I',value['config']),(1076,1,1920,1080,1))
                self.assertEqual(value['config'][-32:],bytes.fromhex(digest))
                name=value['config'][20:1044].decode('utf-16le').rstrip('\0')
                self.assertEqual(Path(name),path)
                self.assertEqual(len(list(path.parent.glob('*.json'))),3)
                self.assertFalse(value['active']);self.assertFalse(value['visible_quality_qualified'])
                second=self.prepare(recipient,case,platform)
                self.assertEqual(value['config'],second['config'])
            self.assertEqual(original,archive.read_bytes())

    def test_bad_last_alias_creates_no_cache_after_two_good_sources(self):
        with tempfile.TemporaryDirectory() as d:
            root,recipient,case,platform,_=self.setup_case(d)
            with authored(root) as (_,_,_,resources):
                resources[list(resources)[-1]]=b'changed owned resource'
                with self.assertRaises(ValueError):self.prepare(recipient,case,platform)
            self.assertFalse((case/'LocalData').exists())

    def test_missing_capability_refuses_before_source_generation(self):
        with tempfile.TemporaryDirectory() as d:
            root,recipient,case,platform,_=self.setup_case(d)
            with authored(root) as (_,_,generated,_):
                with self.assertRaisesRegex(ValueError,'unavailable'):
                    self.prepare(recipient,case,platform,available_features=())
                generated.assert_not_called()

    def test_existing_corrupt_receipt_prevents_recreation_of_missing_program(self):
        with tempfile.TemporaryDirectory() as d:
            root,recipient,case,platform,_=self.setup_case(d)
            with authored(root):
                value=self.prepare(recipient,case,platform);program=Path(value['program_path']);program.unlink()
                next(program.parent.glob('*.json')).write_text('corrupt')
                with self.assertRaisesRegex(ValueError,'corrupt'):self.prepare(recipient,case,platform)
                self.assertFalse(program.exists())

    def test_changed_existing_program_never_overwritten(self):
        with tempfile.TemporaryDirectory() as d:
            root,recipient,case,platform,_=self.setup_case(d)
            with authored(root):
                value=self.prepare(recipient,case,platform);program=Path(value['program_path']);program.write_bytes(b'changed')
                with self.assertRaises(ValueError):self.prepare(recipient,case,platform)
                self.assertEqual(program.read_bytes(),b'changed')

    def test_active_or_completed_case_checkpoint_never_mutated(self):
        for name in ('native.private.json','checkpoint.private.json','result.private.json'):
            with self.subTest(name=name),tempfile.TemporaryDirectory() as d:
                root,recipient,case,platform,_=self.setup_case(d);(case/name).write_text('{}')
                with authored(root) as (_,_,generated,_):
                    with self.assertRaises(ValueError):self.prepare(recipient,case,platform)
                    generated.assert_not_called()
                self.assertEqual((case/name).read_text(),'{}')

    def test_gold_layout_and_recipient_localdata_confined(self):
        with tempfile.TemporaryDirectory() as d:
            root,recipient,case,platform,_=self.setup_case(d,edition='gold')
            with authored(root):
                value=self.prepare(recipient,case,platform,private_case=False,
                    native_report=recipient/'LocalData/native.private.json')
                self.assertEqual(value['edition'],'gold')
                self.assertTrue(Path(value['program_path']).is_relative_to(recipient/'LocalData'))
                with self.assertRaises(ValueError):self.prepare(recipient,case,platform,private_case=False)

    def test_unknown_layout_and_source_recipe_fail_before_writes(self):
        with tempfile.TemporaryDirectory() as d:
            root,recipient,case,platform,_=self.setup_case(d)
            with authored(root):
                with self.assertRaises(ValueError):self.prepare(recipient,case,{**platform,'edition':'unknown'})
                with patch.object(water,'generate',side_effect=ValueError('unverified program')):
                    with self.assertRaises(ValueError):self.prepare(recipient,case,platform)
            self.assertFalse((case/'LocalData').exists())

    def test_disabled_transport_is_explicit_and_contains_no_path_or_hash(self):
        raw=stage.configuration((1920,1080))
        self.assertEqual(struct.unpack_from('<5I',raw),(1076,1,1920,1080,0))
        self.assertEqual(raw[20:],bytes(1056))
        for size in ((0,1080),(3841,2160),(1920,2161),(True,1080)):
            with self.assertRaises(ValueError):stage.configuration(size)
        with self.assertRaises(ValueError):stage.configuration((1920,1080),enabled=1)
        with self.assertRaises(ValueError):stage.configuration((1920,1080),enabled=True)

    def test_transport_rejects_wrong_recipe_hash_and_path_overrun(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            for name,pin in (('wrong.ps.bin',stage.PROGRAM_SHA256),(stage.PROGRAM_SHA256+'.ps.bin','0'*64)):
                with self.assertRaises(ValueError):stage.configuration((1920,1080),enabled=True,
                    pixel_path=root/'MaterialShaders'/water.RECIPE_ID/name,pixel_sha256=pin)
            path=root/('x'*480)/'MaterialShaders'/water.RECIPE_ID/(stage.PROGRAM_SHA256+'.ps.bin')
            with self.assertRaises(ValueError):stage.configuration((1920,1080),enabled=True,pixel_path=path,pixel_sha256=stage.PROGRAM_SHA256)


if __name__=='__main__':unittest.main()
