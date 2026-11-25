"""Synthetic byte regions only; no original callback body is redistributed."""
import contextlib
import hashlib
import struct
import unittest
from unittest import mock

from tools.graphics_lab import save_role_read as s


class SaveRoleReadTests(unittest.TestCase):
    def fixture(self, *, first=30, count=50, duplicate=False, bad_sink=False):
        self.base=0x650000;self.callback=self.base+0x123000;self.sink=self.base+0x45000
        self.slot=s.DATA_RVA+0x120;self.first=self.base+s.RDATA_RVA+0x100
        self.second=self.first+8
        code=bytearray(66)  # Intentionally not an actual original program.
        for offset,address in ((10,self.first),(34,self.second)):
            struct.pack_into('<I',code,offset,address)
        for offset in (26,50):
            struct.pack_into('<i',code,offset,self.sink-self.callback-offset-4)
        template=bytearray(code)
        for offset in (10,26,34,50):template[offset:offset+4]=bytes(4)
        self.template_hash=s.camera_read.fnv(template)
        sink=b'S'*56;self.sink_hash=hashlib.sha256(sink).hexdigest()
        data=bytearray(s.DATA_BYTES)
        struct.pack_into('<2I',data,0x120,self.base+s.NAME_RVA,self.callback)
        if duplicate:struct.pack_into('<2I',data,0x180,self.base+s.NAME_RVA,self.callback)
        self.regions=[(self.base+s.NAME_RVA,s.NAME),(self.callback,bytes(code)),
            (self.sink,b'X'*56 if bad_sink else sink),(self.first,struct.pack('<2d',first,count)),
            (self.base+s.DATA_RVA,bytes(data))]
        self.reads=[]
        def read(address,size):
            self.reads.append((address,size))
            if size>65536:raise AssertionError('unbounded reader')
            for start,body in self.regions:
                if start<=address and address+size<=start+len(body):
                    return body[address-start:address-start+size]
            raise ValueError('unmapped synthetic region')
        return read

    @contextlib.contextmanager
    def synthetic_pins(self):
        with mock.patch.object(s,'CALLBACK_TEMPLATE_FNV',self.template_hash),mock.patch.object(s,'LUA_SINK_SHA',self.sink_hash):
            yield

    def test_bounded_registered_callback_derives_actual_operands_and_normalizes_aslr(self):
        read=self.fixture()
        with self.synthetic_pins():result=s.read_policy(read,self.base)
        self.assertEqual(result['first'],30);self.assertEqual(result['count'],50)
        self.assertEqual(result['first_rva'],self.first-self.base)
        self.assertEqual(result['registration']['name_slot_rva'],self.slot)
        self.assertEqual(result['callback_rva'],self.callback-self.base)
        self.assertTrue(self.reads);self.assertLessEqual(max(n for _,n in self.reads),65536)

    def test_synthetic_body_never_passes_production_instruction_pins(self):
        read=self.fixture()
        with self.assertRaises(ValueError):s.read_policy(read,self.base)

    def test_changed_values_duplicate_registration_and_sink_reject(self):
        for settings in ({'first':31},{'count':51},{'duplicate':True},{'bad_sink':True}):
            read=self.fixture(**settings)
            with self.synthetic_pins(),self.subTest(settings=settings),self.assertRaises(ValueError):s.read_policy(read,self.base)

    def test_invalid_base_or_name_cannot_scan(self):
        read=self.fixture()
        with self.assertRaises(ValueError):s.read_policy(read,0)
        self.assertFalse(self.reads)
        self.regions[0]=(self.base+s.NAME_RVA,b'X'*len(s.NAME))
        with self.synthetic_pins(),self.assertRaises(ValueError):s.read_policy(read,self.base)
        self.assertEqual(len(self.reads),1)

    def test_nonsteam_case_refuses_before_any_process_open(self):
        with mock.patch.object(s.scope,'_case_files',return_value={'owned':{'sha256':'0'*64}}),mock.patch.object(s.scope.isolated_process,'Windows') as windows:
            with self.assertRaises(ValueError):s.sample('owned-case','owned-dll')
            windows.assert_not_called()


if __name__=='__main__':unittest.main()
