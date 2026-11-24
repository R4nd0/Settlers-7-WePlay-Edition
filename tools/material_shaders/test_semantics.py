import struct
import unittest

from tools.material_shaders import semantics


def fixture(*,name=b"g_Reflection",register_set=2,index=69,reserved=278):
    table=bytearray(struct.pack("<7I",28,0,0xffff0300,1,28,0,0))
    table.extend(struct.pack("<IHHHHII",64,register_set,index,1,reserved,48,0))
    table.extend(struct.pack("<6HI",1,3,1,4,1,0,0))
    table.extend(name+b"\0");table.extend(b"\0"*(-len(table)%4))
    comment=b"CTAB"+bytes(table)
    return struct.pack("<2I",0xffff0300,((len(comment)//4)<<16)|0xfffe)+comment+struct.pack("<2I",0,0xffff)


class ConstantReflectionTests(unittest.TestCase):
    def test_real_layout_shape_and_nonzero_opaque_reserved_field(self):
        rows=semantics.reflect(fixture())
        self.assertEqual(len(rows),1)
        row=rows[0]
        self.assertEqual((row["name"],row["register_set"],row["register_index"],row["register_count"]),
                         ("g_Reflection","float4",69,1))
        self.assertEqual((row["rows"],row["columns"]),(1,4))
        self.assertFalse(row["runtime_values_observed"])
        self.assertNotIn("default_value",row)

    def test_sampler_metadata(self):
        row=semantics.reflect(fixture(name=b"g_ReflectionMap",register_set=3,index=3))[0]
        self.assertEqual((row["register_set"],row["register_index"]),("sampler",3))

    def test_optimized_empty_table_and_absent_table(self):
        table=struct.pack("<7I",28,0,0xffff0300,0,0,0,0)
        comment=b"CTAB"+table
        program=struct.pack("<2I",0xffff0300,((len(comment)//4)<<16)|0xfffe)+comment+struct.pack("<2I",0,0xffff)
        self.assertEqual(semantics.reflect(program),[])
        self.assertEqual(semantics.reflect(struct.pack("<3I",0xffff0300,0,0xffff)),[])

    def test_unknown_set_and_unsafe_name_refused(self):
        for program in (fixture(register_set=9),fixture(name=b"../bad")):
            with self.assertRaises(ValueError):semantics.reflect(program)

    def test_header_version_and_entry_type_name_ranges(self):
        base=fixture()
        # CTAB starts at program byte12. Each malformed offset remains inside
        # an otherwise valid shader comment envelope, exercising table checks.
        for offset,value in ((12,24),(20,0xfffe0300),(28,0xffffffff),(40,0xffffffff),(52,0xffffffff)):
            broken=bytearray(base);struct.pack_into("<I",broken,offset,value)
            with self.subTest(offset=offset),self.assertRaises(ValueError):semantics.reflect(bytes(broken))


if __name__=="__main__":unittest.main()
