import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from tools.material_shaders import catalog
from tools.material_shaders.test_shaderlib import fixture


class CatalogTests(unittest.TestCase):
    def test_scalar_binary_layout_and_program_identity_aliases(self):
        binary,report=catalog.build([("water","Shaders/Water.ShaderLib",fixture()),
                                     ("terrain","Shaders/Terrain.ShaderLib",fixture())])
        magic,version,rows=catalog.HEADER.unpack_from(binary)
        self.assertEqual((magic,version,rows),(b"S7MSCAT1",1,6))
        self.assertEqual(len(binary),16+rows*48)
        self.assertEqual(report["catalog_sha256"],hashlib.sha256(binary).hexdigest())
        self.assertEqual(len(report["alias_groups"]),3)
        for index,row in enumerate(report["entries"]):
            identifier,family,stage,size,digest=catalog.ENTRY.unpack_from(binary,16+index*48)
            self.assertEqual(identifier,index+1)
            self.assertEqual((family,stage,size),(row["family"],1 if row["stage"]=="vertex" else 2,row["compiled_bytes"]))
            self.assertEqual(digest.hex(),row["compiled_sha256"])
        self.assertFalse(report["contains_program_bytes"])
        self.assertFalse(report["replacement_enabled"])

    def test_unknown_family_duplicate_resource_and_bounds_refused(self):
        for resources in ([],[("other","Shaders/a.ShaderLib",fixture())],
                          [("water","Shaders/a.ShaderLib",fixture())]*2):
            with self.assertRaises(ValueError):catalog.build(resources)
        with patch.object(catalog,"MAX_ENTRIES",2),self.assertRaises(ValueError):
            catalog.build([("water","Shaders/a.ShaderLib",fixture())])

    def test_catalog_outputs_private_new_directory_only(self):
        binary,report=catalog.build([("water","Shaders/a.ShaderLib",fixture())])
        with tempfile.TemporaryDirectory() as folder,patch.object(catalog.inventory,"ROOT",Path(folder)):
            root=Path(folder)
            with self.assertRaises(ValueError):catalog.write(binary,report,root/"public")
            output=root/"work/catalog";catalog.write(binary,report,output)
            self.assertEqual((output/"catalog.bin").read_bytes(),binary)
            with self.assertRaises(FileExistsError):catalog.write(binary,report,output)


if __name__=="__main__":unittest.main()
