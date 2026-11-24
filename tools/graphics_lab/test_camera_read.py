"""Authored synthetic address spaces; no original code or live process reads."""
from copy import deepcopy
import struct
import unittest

from tools.graphics_lab import camera_read as c


class Memory:
    def __init__(self):self.regions = {};self.mutate = None
    def put(self, address, raw):self.regions[address] = bytes(raw)
    def word(self, address, value):self.put(address, struct.pack('<I', value))
    def read(self, address, size):
        if self.mutate:self.mutate(address, size)
        for start, raw in self.regions.items():
            if start <= address and address + size <= start + len(raw):
                return raw[address-start:address-start+size]
        raise OSError('synthetic inaccessible address')


def fixture(base=0x50000000, steam=False):
    memory = Memory()
    profile = {'edition': 'synthetic', 'image_size': 0x10000, 'root_rva': 0x5000, 'methods': []}
    methods = []
    data = bytearray(1024)
    for i, name in enumerate(('GetCamTranslation', 'GetCamLookAt')):
        callback, string, slot = 0x1100 + i*0x100, 0x2000 + i*0x40, 0x420 + i*0x40
        raw = bytearray([0x90] * (108 if steam else 16))
        operand = 7 if steam else 1
        struct.pack_into('<I', raw, operand, base + 0x5000)
        raw[20 if steam else 9] = 0x28 + i*12
        if steam:
            for at in (46, 68, 90):struct.pack_into('<i', raw, at, (base+0x1700)-(base+callback+at+4))
        memory.put(base+callback, raw)
        memory.put(base+string, name.encode()+b'\0')
        struct.pack_into('<2I', data, slot-0x400, base+string, base+callback)
        method = {'name': name, 'name_rva': string, 'field': 0x28 + i*12}
        if steam:
            masked = bytearray(raw)
            for at in (7, 46, 68, 90):masked[at:at+4] = bytes(4)
            method['template_fnv'] = c.fnv(masked)
        else:
            method.update(slot_rva=slot, callback_rva=callback,
                guards=((callback,len(raw),c.fnv(c.normalized_code(raw,base,(operand,))),(operand,)),))
        methods.append(method)
    profile['methods'] = tuple(methods)
    memory.put(base+0x400, data)
    if steam:
        profile.update(data_rva=0x400,data_bytes=len(data));del profile['root_rva']
    memory.word(base+0x5000, 0x70000100)
    memory.word(0x70000108, 0x70000200)
    memory.word(0x700002d8, 0x70000300)
    memory.put(0x70000328, struct.pack('<6f', 10,20,30,4,5,6))
    return memory, profile


class CameraReadTests(unittest.TestCase):
    def test_exact_code_and_registrations_produce_only_unsynchronized_pose(self):
        memory, profile = fixture()
        result = c.read_pose(memory.read, 0x50000000, profile)
        self.assertEqual(result['translation'], [10,20,30])
        self.assertEqual(result['look_at'], [4,5,6])
        self.assertFalse(result['same_frame_gpu_proof'])
        self.assertFalse(result['visible_shader_correlation_validated'])
        self.assertIsNone(result['fov_degrees'])
        self.assertEqual(len(result['code_guards']), 2)
        self.assertEqual(len(result['registration_guards']), 2)

    def test_aslr_normalization_keeps_original_instruction_identity(self):
        a, first = fixture(0x50000000)
        b, second = fixture(0x60000000)
        self.assertEqual(first['methods'][0]['guards'][0][2], second['methods'][0]['guards'][0][2])
        self.assertEqual(c.read_pose(a.read,0x50000000,first)['translation'], c.read_pose(b.read,0x60000000,second)['translation'])

    def test_changed_code_registration_name_and_inaccessible_pointer_are_rejected(self):
        for kind in ('code','registration','name','pointer'):
            memory, profile = fixture()
            if kind == 'code':memory.put(0x50001100, bytes(16))
            elif kind == 'registration':
                data=bytearray(memory.regions[0x50000400]);struct.pack_into('<I',data,0x24,0x50001300);memory.put(0x50000400,data)
            elif kind == 'name':memory.put(0x50002000,b'WrongCameraName!!\0')
            else:memory.word(0x50005000,0)
            with self.subTest(kind=kind), self.assertRaises((ValueError,OSError)):
                c.read_pose(memory.read,0x50000000,profile)

    def test_nonfinite_or_degenerate_camera_is_not_reported(self):
        for values in ((float('nan'),20,30,4,5,6),(1,2,3,1,2,3)):
            memory, profile=fixture();memory.put(0x70000328,struct.pack('<6f',*values))
            with self.assertRaises(ValueError):c.read_pose(memory.read,0x50000000,profile)

    def test_camera_motion_during_external_read_is_refused(self):
        memory, profile=fixture();reads=0
        def moving(address,size):
            nonlocal reads
            if address==0x70000328:
                reads+=1
                if reads==2:memory.put(address,struct.pack('<6f',11,20,30,4,5,6))
        memory.mutate=moving
        with self.assertRaisesRegex(ValueError,'changed during'):c.read_pose(memory.read,0x50000000,profile)

    def test_packed_style_runtime_template_derives_root_without_guessed_address(self):
        memory, profile=fixture(steam=True)
        result=c.read_pose(memory.read,0x50000000,profile)
        self.assertEqual(result['translation'],[10,20,30])
        self.assertEqual(result['steam_mapping'],'runtime-template-derived')
        self.assertFalse(result['same_frame_gpu_proof'])

    def test_unknown_packed_template_and_ambiguous_registration_are_refused(self):
        memory, profile=fixture(steam=True);changed=deepcopy(profile)
        changed['methods'][0]['template_fnv']^=1
        with self.assertRaises(ValueError):c.read_pose(memory.read,0x50000000,changed)
        data=bytearray(memory.regions[0x50000400]);data[0x90:0x98]=data[0x20:0x28];memory.put(0x50000400,data)
        with self.assertRaisesRegex(ValueError,'ambiguous'):c.read_pose(memory.read,0x50000000,profile)


if __name__=='__main__':unittest.main()
