"""Real native compressor interoperability with a Windows-generated vector."""
import ctypes
import gzip
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/lan'))
import canonical_deflate


@unittest.skipUnless(sys.platform=='linux' and os.environ.get('WEPLAY_TEST_COMPRESSION'),
                     'requires a locally built synthetic-test compressor')
class NativeCompressionTests(unittest.TestCase):
    def setUp(self):
        self.library=Path(os.environ['WEPLAY_TEST_COMPRESSION']).resolve(strict=True)
        canonical_deflate.load.cache_clear();self.addCleanup(canonical_deflate.load.cache_clear)
        # CI rebuilds use another compiler: test that trusted source build's
        # output against the fixed Windows vector, without changing release pins.
        metadata=dict(format='weplay-canonical-compressor-v1',version='2.2.4',
                      files={self.library.name:hashlib.sha256(self.library.read_bytes()).hexdigest()})
        original=Path.read_text
        def read(path,*args,**kwargs):
            return json.dumps(metadata) if path.name=='compression-runtime.json' else original(path,*args,**kwargs)
        self.override=patch.object(Path,'read_text',read);self.override.start();self.addCleanup(self.override.stop)

    def test_real_linux_stream_matches_windows_zlib_ng_224_and_roundtrips(self):
        data=bytes(range(256))*24+b'WePlay canonical gzip fixture'
        encoded=canonical_deflate.compress(data,self.library)
        self.assertEqual(hashlib.sha256(encoded).hexdigest(),'6f4f0393da748dcecd6b9fa204acaef95edb2c63720668bb3f233aaed2e5f399')
        self.assertEqual(gzip.decompress(encoded),data)
        self.assertEqual(encoded[9],255)

    def test_native_wrapper_rejects_null_buffers(self):
        library=canonical_deflate.load(str(self.library));written=ctypes.c_size_t()
        self.assertNotEqual(library.weplay_gzip(None,0,None,64,ctypes.byref(written)),0)

    def test_unqualified_library_is_rejected_before_dlopen(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/self.library.name;path.write_bytes(b'foreign, not an ELF')
            with patch.object(canonical_deflate.ctypes,'CDLL') as loader:
                with self.assertRaises(ValueError):canonical_deflate.load(str(path))
            loader.assert_not_called()


if __name__=='__main__':unittest.main()
