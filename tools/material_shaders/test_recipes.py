import copy
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.material_shaders import recipes,shaderlib
from tools.material_shaders.test_foliage import vertex_fixture
from tools.material_shaders.test_shaderlib import encoded,shader


NAME="Graphics/Shaders/SpeedTree_ATI_High.ShaderLib"


def resource():
    return (b'FILEVERSION = 2.3.0.0;\nPLATFORM = DX9;\nSOURCECODE=$TEST{\n// authored fixture\n}$TEST\n'
            b'SHADER LeafCardShader {\nvs_bytecode[vs_3_0]={'+encoded(vertex_fixture())+
            b'}\nps_bytecode[ps_3_0]={'+encoded(shader())+b'}\n}\n')


class RecipientRecipeTests(unittest.TestCase):
    def patched(self):
        return patch.multiple(recipes,RESOURCES={NAME:shaderlib.digest(resource())},
                              ORIGINAL_VERTEX=shaderlib.digest(vertex_fixture()),ORIGINAL_PIXEL=shaderlib.digest(shader()))

    def test_owned_source_pair_required_and_output_stays_disabled(self):
        with self.patched():
            generated=recipes.generate_leaf(NAME,resource())
            self.assertFalse(generated.manifest["active"])
            self.assertFalse(generated.manifest["public_binary_distribution_permitted"])
            self.assertTrue(generated.manifest["generated_from_recipient_owned_resource"])
            with self.assertRaises(ValueError):recipes.generate_leaf(NAME,resource()+b"changed")
            with self.assertRaises(ValueError):recipes.generate_leaf("wrong",resource())

    def test_recipient_cache_is_content_addressed_idempotent_and_never_overwrites(self):
        with self.patched(),tempfile.TemporaryDirectory() as folder:
            root=Path(folder);generated=recipes.generate_leaf(NAME,resource())
            first=recipes.write_local_cache(root,generated);second=recipes.write_local_cache(root,generated)
            self.assertEqual(first,second)
            path=root/first["program_relative_path"]
            self.assertTrue(path.is_relative_to(root/"LocalData"))
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),first["generated_vertex_sha256"])
            path.write_bytes(b"changed cache")
            with self.assertRaises(ValueError):recipes.write_local_cache(root,generated)

    def test_tampered_receipt_identity_and_path_fields_refused(self):
        with self.patched(),tempfile.TemporaryDirectory() as folder:
            generated=recipes.generate_leaf(NAME,resource())
            for key,value in (("generated_vertex_sha256","0"*64),("source_resource_sha256","../escape"),("active",True)):
                manifest=copy.deepcopy(generated.manifest);manifest[key]=value
                with self.subTest(key=key),self.assertRaises(ValueError):
                    recipes.write_local_cache(Path(folder),recipes.Generated(generated.program,manifest))


if __name__=="__main__":unittest.main()
