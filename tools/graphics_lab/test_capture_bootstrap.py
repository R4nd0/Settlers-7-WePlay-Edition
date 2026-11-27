"""Authored ABI/entry fixtures only; never invokes a real remote worker."""
from pathlib import Path
import struct
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch
from . import capture_bootstrap as b


class Tests(unittest.TestCase):
    def test_original_image_machine_stamp_size_and_entry_guards(self):
        header=bytearray(4096);header[:2]=b'MZ';struct.pack_into('<I',header,0x3c,0x80)
        header[0x80:0x86]=b'PE\0\0\x4c\x01';struct.pack_into('<I',header,0x88,123)
        struct.pack_into('<I',header,0xd0,0x5000)
        profile=(1,123,0x5000,0x200,None,'0102030405',(0x100,0x104))
        self.assertEqual(b.image_slots(bytes(header),0x400000,b'\1\2\3\4\5',profile),[0x400100,0x400104])
        for offset in (0,0x84,0x88,0xd0):
            changed=bytearray(header);changed[offset]^=1
            with self.subTest(offset=offset),self.assertRaises(RuntimeError):b.image_slots(bytes(changed),0x400000,b'\1\2\3\4\5',profile)
        with self.assertRaises(RuntimeError):b.image_slots(bytes(header),0x400000,b'xxxxx',profile)
        with self.assertRaises(RuntimeError):b.image_slots(b'',0x400000,b'',profile)

    def test_fixed_observer_or_explicit_cap_protocol(self):
        with patch.object(b,'_invoke_fixed')as invoke:
            b.invoke_water('owned',Path('dll'),b'config')
            invoke.assert_called_once_with('owned',Path('dll'),b'config','water_observe')
            invoke.reset_mock();b.invoke_water('owned',Path('dll'),b'config',pow_cap=True)
            invoke.assert_called_once_with('owned',Path('dll'),b'config','water_pow_cap')
            with self.assertRaises(ValueError):b.invoke_water('owned',Path('dll'),b'config',pow_cap=1)
        owned=SimpleNamespace(owned=Mock(),expected=b.UBI,native_initialized=True,kernel_isolation=7)
        for kind,flag in (('water_observe',1),('water_pow_cap',0),('unknown',0)):
            with self.subTest(kind=kind),self.assertRaises(ValueError):
                b._invoke_fixed(owned,Path('WePlayWaterOn12Probe.dll'),struct.pack('<3I',1036,1,flag)+b'\0'*1024,kind)
        with self.assertRaises(ValueError):
            b._invoke_fixed(owned,Path('WePlayOn12SpatialLab.dll'),struct.pack('<3I',1052,1,2)+b'\0'*1040,'on12_upscale')


if __name__=='__main__':unittest.main()
