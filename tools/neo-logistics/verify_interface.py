"""Verify a private source archive without distributing any game assets.

Usage: python tools/neo-logistics/verify_interface.py SOURCE_INTERFACE_BBA
Requires the development Lua 5.1 runtime in work/lua-test-runtime.
"""
from pathlib import Path
import hashlib
import sys
import interface_patch

def verify(source):
    sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'work/lua-test-runtime'))
    from lupa.lua51 import LuaRuntime
    original=interface_patch.Archive(source)
    result=interface_patch.build(source)
    patched=interface_patch.Archive(result)
    lua=LuaRuntime()
    changed=[]
    for name in original.entries:
        if name.endswith('.lua') and original.read(name)!=patched.read(name):
            lua.compile(patched.read(name).decode('utf-8-sig'),name=name)
            changed.append(name)
    assert len(changed)==8,changed
    print(f'PASS: {len(changed)} complete Lua 5.1 scripts; unrelated archive members unchanged')
    print('Candidate SHA256:',hashlib.sha256(result).hexdigest())
    return result

if __name__=='__main__':
    if len(sys.argv)!=2:raise SystemExit(__doc__)
    verify(Path(sys.argv[1]).read_bytes())
