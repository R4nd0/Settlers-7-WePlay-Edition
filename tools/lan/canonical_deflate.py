"""Small ctypes boundary to the pinned, native canonical release compressor."""
import ctypes
import hashlib
import json
from functools import lru_cache
from pathlib import Path


@lru_cache(maxsize=2)
def load(path):
    path=Path(path).absolute()
    if any(part.is_symlink() for part in (path,*path.parents)):
        raise ValueError('Canonical compressor must not be a link')
    lock=json.loads(Path(__file__).with_name('compression-runtime.json').read_text(encoding='utf-8'))
    if lock.get('format')!='weplay-canonical-compressor-v1' or lock.get('version')!='2.2.4':
        raise ValueError('Invalid canonical compressor lock')
    with path.open('rb') as stream:
        digest=hashlib.sha256()
        for block in iter(lambda:stream.read(1024*1024),b''):digest.update(block)
    if digest.hexdigest()!=lock.get('files',{}).get(path.name):
        raise ValueError('Canonical compressor fingerprint differs')
    library=ctypes.CDLL(str(path.resolve(strict=True)))
    library.weplay_zlib_version.argtypes=[];library.weplay_zlib_version.restype=ctypes.c_char_p
    library.weplay_gzip_bound.argtypes=[ctypes.c_size_t];library.weplay_gzip_bound.restype=ctypes.c_size_t
    library.weplay_gzip.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)]
    library.weplay_gzip.restype=ctypes.c_int
    if library.weplay_zlib_version()!=b'2.2.4':raise ValueError('Unexpected canonical compressor version')
    return library


def compress(data, path):
    data=bytes(data)
    if len(data)>64*1024*1024:raise ValueError('Canonical compression input exceeds the archive limit')
    library=load(str(path));capacity=library.weplay_gzip_bound(len(data))
    if not len(data)<=capacity<=len(data)+len(data)//8+65536:raise ValueError('Invalid canonical compression bound')
    source=ctypes.create_string_buffer(data);output=ctypes.create_string_buffer(capacity);written=ctypes.c_size_t()
    code=library.weplay_gzip(source,len(data),output,capacity,ctypes.byref(written))
    if code or not 18<=written.value<=capacity:raise ValueError('Canonical compression failed')
    return output.raw[:written.value]
