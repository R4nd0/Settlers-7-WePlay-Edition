import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from tools.material_shaders import water_gpu as gpu


class WaterGpuProvenanceTests(unittest.TestCase):
    def fixture(self, directory):
        root=Path(directory);(root/'work').mkdir();(root/'native').mkdir()
        build=root/'work/build';build.mkdir()
        sources={}
        for name in gpu.SOURCES:
            data=('authored source '+name).encode();(root/'native'/name).write_bytes(data);(build/name).write_bytes(data)
            sources[name]=hashlib.sha256(data).hexdigest()
        exe=bytearray(128);exe[:2]=b'MZ';struct.pack_into('<I',exe,0x3c,64);exe[64:70]=b'PE\0\0\x4c\x01'
        (build/'WaterGpuFixture.exe').write_bytes(exe)
        manifest=dict(format='s7-water-gpu-build-v1',sources=sources,exe_sha256=hashlib.sha256(exe).hexdigest(),executed=False)
        (build/'build.private.json').write_text(json.dumps(manifest))
        return root,build,manifest

    def test_matching_snapshot_source_and_named_executable_required(self):
        with tempfile.TemporaryDirectory() as d:
            root,build,manifest=self.fixture(d)
            with patch.object(gpu,'ROOT',root):
                exe,actual=gpu.verify_build(build)
            self.assertEqual(exe.name,'WaterGpuFixture.exe');self.assertEqual(actual,manifest)

    def test_stale_current_source_or_retained_snapshot_refused(self):
        for changed in ('native','snapshot'):
            with self.subTest(changed=changed),tempfile.TemporaryDirectory() as d:
                root,build,_=self.fixture(d)
                target=(root/'native' if changed=='native' else build)/'water_shader_effects.cpp'
                target.write_bytes(b'new checkpoint')
                with patch.object(gpu,'ROOT',root),self.assertRaisesRegex(ValueError,'checkpoint changed'):
                    gpu.verify_build(build)

    def test_changed_binary_or_renamed_unbound_executable_refused(self):
        for changed in ('bytes','name'):
            with self.subTest(changed=changed),tempfile.TemporaryDirectory() as d:
                root,build,_=self.fixture(d);exe=build/'WaterGpuFixture.exe'
                if changed=='bytes':exe.write_bytes(exe.read_bytes()+b'altered')
                else:exe.rename(build/'Other.exe')
                with patch.object(gpu,'ROOT',root),self.assertRaises((ValueError,FileNotFoundError)):
                    gpu.verify_build(build)

    def test_x64_executable_is_not_admitted_even_with_own_hash(self):
        with tempfile.TemporaryDirectory() as d:
            root,build,manifest=self.fixture(d);exe=build/'WaterGpuFixture.exe';raw=bytearray(exe.read_bytes());raw[68:70]=b'\x64\x86';exe.write_bytes(raw)
            manifest['exe_sha256']=hashlib.sha256(raw).hexdigest();(build/'build.private.json').write_text(json.dumps(manifest))
            with patch.object(gpu,'ROOT',root),self.assertRaisesRegex(ValueError,'x86'):
                gpu.verify_build(build)

    def test_nonwork_build_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            root,build,_=self.fixture(d)
            with patch.object(gpu,'ROOT',root),self.assertRaises(ValueError):gpu.verify_build(root/'native')


if __name__=='__main__':unittest.main()
