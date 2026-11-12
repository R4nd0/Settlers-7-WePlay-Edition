"""Read-only native Steam local-facade guards; no game/account state is read."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

HASHES = {
    'cf7f5f25665d8e98ff3b17a70e658be4458f67b29c42455c83715a4bb68c7d2d',
    'ce9f64801cd6e837df287e9a5c180d653f4e14814cd2806d2cb8b898106fc810',
}
EXPORTS = {'WePlayLocalSteamBoundary', 'SteamAPI_Init', 'SteamAPI_GetHSteamUser',
           'SteamAPI_GetHSteamPipe', 'SteamInternal_CreateInterface', 'SteamAPI_Shutdown',
           'SteamAPI_RunCallbacks', 'SteamInternal_ContextInit', 'SteamAPI_RegisterCallback',
           'SteamAPI_UnregisterCallback'}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def check(executable, native):
    import pefile
    native = Path(native).resolve()
    receipt = json.loads((native.parent/'verified.private.json').read_text(encoding='utf-8-sig'))
    if receipt.get('native_tests_passed') is not True or receipt.get('native_scenarios') != 12:
        raise ValueError('Candidate native checks are incomplete')
    for name in ('steam_api.dll', 'uplay_r1_loader.dll'):
        if sha(native/name) != receipt['files'].get(name):
            raise ValueError('Candidate binary changed after native verification')
    for name in ('steam_local_facade.h', 'steam_local_api.cpp', 'steam_local_api.def', 'api_tests.cpp', 'facade_tests.cpp'):
        if sha(native.parent/'source'/name) != sha(Path(__file__).with_name(name)):
            raise ValueError('Candidate source checkpoint does not match current local-facade source')
    pin = sha(executable)
    if pin not in HASHES:
        raise ValueError('Requires a fingerprinted, isolated native Steam lab C/D image')
    pe = pefile.PE(str(executable))
    if (pe.FILE_HEADER.Machine, pe.FILE_HEADER.TimeDateStamp, pe.OPTIONAL_HEADER.ImageBase,
            pe.OPTIONAL_HEADER.SizeOfImage) != (0x14c, 0x5d305b24, 0x400000, 0x1106000):
        raise ValueError('Unexpected Steam PE identity')
    header = Path(__file__).with_name('steam_local_facade.h').read_text(encoding='utf-8')
    guards = re.findall(r'\{(0x[0-9a-f]+),(\d+),(0x[0-9a-f]+)ull,(\w+),_countof\(\w+\)\}', header)
    if len(guards) != 3:
        raise ValueError('Guard table changed; review the verifier')
    real_reloc = {x.rva+0x400000 for block in pe.DIRECTORY_ENTRY_BASERELOC
                  for x in block.entries if x.type == 3}
    checked = []
    for address, size, expected, reloc_name in guards:
        address, size, expected = int(address, 16), int(size), int(expected, 16)
        data = pe.get_data(address-0x400000, size)
        offsets = [int(n) for n in re.search(reloc_name+r'\[\] = \{([^}]+)\}', header)[1].split(',')]
        if {address+offset for offset in offsets} != {r for r in real_reloc if address <= r < address+size}:
            raise ValueError('Native relocation guard mismatch')
        digest = 14695981039346656037
        for byte in data:
            digest = ((digest ^ byte)*1099511628211) & 0xffffffffffffffff
        if digest != expected:
            raise ValueError('Complete native function changed at '+hex(address))
        checked.append(dict(address=hex(address), bytes=size, full_function_guard=True))
    image = pe.get_memory_mapped_image()
    imports = []
    for entry in pe.DIRECTORY_ENTRY_IMPORT:
        if entry.dll.lower() != b'steam_api.dll':
            continue
        for imp in entry.imports:
            if imp.name is None or imp.name.decode() not in EXPORTS:
                raise ValueError('Unknown Steam import')
            callsites = [m.start()+0x400000 for m in re.finditer(re.escape(b'\xff\x15'+struct.pack('<I', imp.address)), image)]
            if not callsites or any(not (0x4c7e50 <= a < 0x4c8d10 or 0x4cb2d0 <= a < 0x4cb600) for a in callsites):
                raise ValueError('Steam import callsite coverage changed')
            imports.append(dict(name=imp.name.decode(), callsites=[hex(a) for a in callsites]))
    if len(imports) != 9:
        raise ValueError('Steam imports changed')
    pe.close()
    boundary = Path(native)/'steam_api.dll'
    pe = pefile.PE(str(boundary))
    exports = {e.name.decode() for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
    if exports != EXPORTS or any(e.forwarder for e in pe.DIRECTORY_ENTRY_EXPORT.symbols):
        raise ValueError('Local boundary exports changed')
    dependencies = {e.dll.decode().lower() for e in pe.DIRECTORY_ENTRY_IMPORT}
    if dependencies != {'kernel32.dll'} or pe.FILE_HEADER.Machine != 0x14c:
        raise ValueError('Local boundary has unexpected dependencies')
    pe.close()
    return dict(executable_sha256=pin, guards=checked, steam_imports=imports,
                boundary_sha256=sha(boundary), boundary_imports=sorted(dependencies),
                adapter_sha256=sha(Path(native)/'uplay_r1_loader.dll'),
                original_steam_exe_qualified=False, network_capture=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--native', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(check(args.executable, args.native), indent=2))
