"""Execute the real entry bridge against an owned non-game x86 fixture."""
import argparse
from pathlib import Path
import os
import struct

import isolated_process as owner


def fixture_profile(path):
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 60)[0]
    optional = pe + 24
    count = struct.unpack_from('<H', data, pe+6)[0]
    optional_size = struct.unpack_from('<H', data, pe+20)[0]
    sections = [struct.unpack_from('<8sIIII', data, optional+optional_size+i*40) for i in range(count)]

    def raw(rva):
        return next(rva-va+offset for _, size, va, stored, offset in sections if va <= rva < va+min(size, stored))

    entry = struct.unpack_from('<I', data, optional+16)[0]
    imports, descriptor = {}, struct.unpack_from('<I', data, optional+104)[0]
    for _ in range(256):
        names, _, _, name, thunk = struct.unpack_from('<5I', data, raw(descriptor))
        if not name: break
        for index in range(512):
            item = struct.unpack_from('<I', data, raw(names)+index*4)[0]
            if not item: break
            if not item & 0x80000000:
                symbol = data[raw(item)+2:].split(b'\0', 1)[0].decode('ascii')
                imports[symbol] = thunk+index*4
        descriptor += 20
    return (99, struct.unpack_from('<I', data, pe+8)[0], struct.unpack_from('<I', data, optional+56)[0],
            entry, entry, data[raw(entry):raw(entry)+5].hex(), tuple(imports[key] for key in owner.IMPORTS))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    root = owner.plain(args.directory)
    executable, dll = root/'standalone_entry_fixture.exe', root/'standalone_entry_fixture.dll'
    profile = fixture_profile(executable)
    environment = dict(os.environ)
    if (root/'mss32.dll').is_file():
        environment['WEPLAY_TEST_MILES'] = '1'
    initialized=[]
    def before_entry(child):
        child.owned()
        if not child.native_initialized or child.poll() is not None:
            raise RuntimeError('Controlled initialization callback did not own a live ready process')
        initialized.append(child.pid)
    child = owner._launch(executable, dll, owner.digest(executable), profile, environment, (),on_initialized=before_entry)
    if child.wait(timeout=15) != 0 or not child.native_initialized:
        raise RuntimeError('Native entry fixture failed: '+str(child.returncode))
    if initialized!=[child.pid]:raise RuntimeError('Initialization callback was not invoked exactly once')
    # A normal/direct load cannot pass the release DLL's startup gate.
    import subprocess
    subprocess.run([str(executable), '--direct'], cwd=root, check=True, timeout=15)
    print('PASS direct-load rejection; no game was started.')


if __name__ == '__main__':
    main()
