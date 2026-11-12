"""Audit S7's debugger remnants and LuaJIT hooks without starting a game.

Requires pefile, capstone and unicorn. Only the three fingerprinted, owned
analysis images below are accepted. Steam uses the existing unpacked analysis
image, never a launch image. All emulated Lua state and dispatch storage is
synthetic; this does not test breakpoints in a running game.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import capstone
import pefile


PROFILES = {
    'ubisoft': {
        'sha256': '4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f',
        'size': 17333680, 'vtable': 0x113414c,
        'callbacks': (0x53b6a0, 0x53b6b0, 0x4c5990),
        'bindings': (0x48d700, 0x48d740, 0x48d780),
        'handler': 0x544c40, 'accessor': 0x4c74e0,
        'open_debug': 0x46c3f0, 'sethook': 0x445ea0,
    },
    'steam_analysis': {
        'sha256': 'b45794aef1ddef76fea13501fa7630023f0c11653c2f644a06c0ae86cb0812b4',
        'size': 17460224, 'vtable': 0x1132654,
        'callbacks': (0x53ac90, 0x53aca0, 0x4c5ac0),
        'bindings': (0x48d6c0, 0x48d700, 0x48d740),
        'handler': 0x544230, 'accessor': 0x4c7610,
        'open_debug': 0x46c400, 'sethook': 0x445eb0,
    },
    'gold': {
        'sha256': 'ec27c7fab2c83a1444f043eb4730f0065393ca2e91607f71e5fd78ee5a5c7c94',
        'size': 14141768, 'vtable': 0xf32c54,
        'callbacks': (0x5a75b0, 0x5a75c0, 0x5a75d0),
        'bindings': (0x109f008, 0x109f018, 0x109f028),
        'handler': (0x5b0420, 0x5b0440, 0x5b0460),
        'open_debug': 0xbf6220, 'sethook': 0xbcd6d0,
    },
}
NAMES = ('getregistry', 'getmetatable', 'setmetatable', 'getfenv', 'setfenv',
         'getinfo', 'getlocal', 'setlocal', 'getupvalue', 'setupvalue',
         'sethook', 'gethook', 'debug', 'traceback')
BINDINGS = ('ShowDebugger', 'DebugBreak', 'DebugShowExecuteLineDialog')
LOADER_MARKERS = ('BBLuaDebugger', 'LuaDebugger.dll', '_AddLuaState@8',
                  '_RemoveLuaState@4', '_NewFile@16', '_IsBreaked@0')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def audit(label, path):
    profile = PROFILES[label]
    require(path.stat().st_size == profile['size'], 'Unexpected image size: '+label)
    raw = path.read_bytes()
    require(hashlib.sha256(raw).hexdigest() == profile['sha256'],
            'Unexpected image hash: '+label)
    pe = pefile.PE(data=raw)
    base = pe.OPTIONAL_HEADER.ImageBase
    require(base == 0x400000 and pe.FILE_HEADER.Machine == 0x14c, 'Expected x86 PE')
    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    def read(address, size):
        data = pe.get_data(address-base, size)
        require(len(data) == size, 'Truncated mapped range')
        return data

    def word(address):
        return struct.unpack('<I', read(address, 4))[0]

    def function(address, limit=4096):
        result = []
        for instruction in cs.disasm(read(address, limit), address):
            result.append(instruction)
            if instruction.mnemonic == 'ret':
                return result
        raise ValueError('No bounded function return at '+hex(address))

    # Four-slot debugger interface: destructor followed by three empty methods.
    require(tuple(word(profile['vtable']+4+i*4) for i in range(3)) == profile['callbacks'],
            'Debugger vtable changed')
    for address, cleanup in zip(profile['callbacks'], (20, 12, 8)):
        require(read(address, 3) == b'\xc2'+struct.pack('<H', cleanup),
                'Debugger callback is no longer an empty return')

    # Check the actual native binding, rather than relying on a surviving name.
    handlers = profile['handler']
    if isinstance(handlers, int):
        handlers = (handlers,)*3
    for name, record, handler in zip(BINDINGS, profile['bindings'], handlers):
        string = raw.index(name.encode()+b'\0')
        name_va = base+pe.get_rva_from_offset(string)
        if label == 'gold':
            require(word(record+4) == name_va and word(record+8) == handler,
                    'Gold debugger binding changed')
            require(read(handler, 3) == b'\x33\xc0\xc3', 'Gold handler changed')
        else:
            instructions = function(record, 64)
            stores = [i for i in instructions if i.mnemonic == 'mov']
            require(any(struct.pack('<I', name_va) in i.bytes for i in stores)
                    and any(struct.pack('<I', handler) in i.bytes for i in stores),
                    'History debugger binding changed')
            getter = profile['accessor']
            body = function(handler)
            require([i.mnemonic for i in body] == ['call', 'cmp', 'je', 'call', 'xor', 'ret']
                    and [int(i.op_str, 16) for i in body if i.mnemonic == 'call'] == [getter, getter]
                    and body[-2].op_str == 'eax, eax', 'History handler changed')

    # LuaJIT names use length-prefixed library metadata, not C strings.
    encoded = b''.join(bytes([len(name)])+name.encode() for name in NAMES)+b'\xff'
    require(raw.count(encoded) == 1, 'Expected one complete debug library table')
    names_offset = raw.index(encoded)
    init_va = base+pe.get_rva_from_offset(names_offset-3)
    initializer = function(profile['open_debug'], 64)
    pushes = [int(i.op_str, 16) for i in initializer if i.mnemonic == 'push'
              and i.op_str.startswith('0x')]
    require(len(pushes) == 3 and pushes[1] == init_va, 'Debug initializer changed')
    require(read(pushes[2], 6) == b'debug\0', 'Unexpected debug library name')
    functions = struct.unpack('<14I', read(pushes[0], 56))
    bodies = [function(address) for address in functions]
    require(all(len(body) > 1 for body in bodies), 'Debug library contains empty function')
    hook_calls = [int(i.op_str, 16) for i in bodies[10] if i.mnemonic == 'call'
                  and i.op_str.startswith('0x')]
    require(hook_calls[-1] == profile['sethook'], 'Lua debug hook API changed')
    loader_hits = [name for name in LOADER_MARKERS
                   if name.lower().encode() in raw.lower()
                   or name.lower().encode('utf-16-le') in raw.lower()]
    require(not loader_hits, 'S6 debugger loader marker unexpectedly present')
    imported_lua = [entry.dll.decode() for entry in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', ())
                    if b'lua' in entry.dll.lower()]
    exported_lua = [entry.name.decode() for entry in
                    getattr(getattr(pe, 'DIRECTORY_ENTRY_EXPORT', None), 'symbols', ())
                    if entry.name and b'lua' in entry.name.lower()]

    result = {
        'image_sha256': profile['sha256'], 'image_bytes': len(raw),
        'debugger_callbacks_empty': True, 'public_debugger_handlers_noop': True,
        's6_loader_marker_hits': loader_hits,
        'lua_import_modules': imported_lua, 'lua_named_exports': exported_lua,
        'luaopen_debug': hex(profile['open_debug']), 'lua_sethook': hex(profile['sethook']),
        'debug_library_functions': dict(zip(NAMES, map(hex, functions))),
        'original_hook_calls_emulated': emulate(pe, profile['sethook']),
    }
    pe.close()
    return result


def emulate(pe, entry):
    """Run the original hook setter AND original dispatch update; no import stubs."""
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_MEM_WRITE
    from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_EBP,
        UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_ESP, UC_X86_REG_EIP)
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    base = pe.OPTIONAL_HEADER.ImageBase
    u.mem_map(base, (pe.OPTIONAL_HEADER.SizeOfImage+4095) & ~4095)
    u.mem_write(base, pe.get_memory_mapped_image())
    scratch = 0x20000000
    u.mem_map(scratch, 0x40000)
    state, global_state = scratch+0x1000, scratch+0x2000
    stack, stop, callback = scratch+0x30000, scratch+0x3f000, scratch+0x3e000
    u.mem_write(state+8, struct.pack('<I', global_state))
    u.mem_write(global_state+0x81, b'\x20')

    def write(uc, access, address, size, value, data):
        require((global_state <= address <= address+size <= global_state+0x10000)
                or (stack-4096 <= address <= address+size <= stack+20),
                'Original code wrote outside modeled global state/stack')

    u.hook_add(UC_HOOK_MEM_WRITE, write)
    kept = {UC_X86_REG_EBX: 0x11111111, UC_X86_REG_ESI: 0x22222222,
            UC_X86_REG_EDI: 0x33333333, UC_X86_REG_EBP: 0x44444444}
    cases = ((callback, 4, 0), (callback, 8, 7), (callback, 3, 0),
             (callback, 0x1f, 11), (0, 15, 3), (callback, 0, 0))
    for hook, mask, count in cases:
        u.mem_write(stack, struct.pack('<IIIII', stop, state, hook, mask, count))
        for reg, value in {**kept, UC_X86_REG_ESP: stack}.items():
            u.reg_write(reg, value)
        u.emu_start(entry, stop, count=10000, timeout=1000000)
        readword = lambda address: struct.unpack('<I', u.mem_read(address, 4))[0]
        require(u.reg_read(UC_X86_REG_EIP) == stop and u.reg_read(UC_X86_REG_EAX) == 1
                and u.reg_read(UC_X86_REG_ESP) == stack+4
                and all(u.reg_read(reg) == value for reg, value in kept.items()),
                'Hook setter did not return with its original ABI')
        require(readword(global_state+0xb8) == (hook if mask & 15 else 0)
                and u.mem_read(global_state+0x81, 1)[0] == 0x20 | ((mask & 15) if hook else 0)
                and readword(global_state+0xb0) == count
                and readword(global_state+0xb4) == count,
                'Hook pointer/mask/counters differ from LuaJIT beta4')
    return len(cases)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for label in PROFILES:
        parser.add_argument('--'+label.replace('_', '-'), type=Path, required=True)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    require(output.is_relative_to((root/'work').resolve()) and not output.exists()
            and output.parent.is_dir(), 'Use a fresh output beneath repository work/')
    results = {}
    for label in PROFILES:
        results[label] = audit(label, getattr(args, label))
        print(label+': empty game debugger; 14 LuaJIT debug functions; 6 hook calls passed', flush=True)
    receipt = {'profiles': results, 'synthetic_lua_state': True,
               'game_process_started': False, 'lua_bytecode_executed': False,
               'breakpoint_or_stepping_verified': False,
               'imports_forwarded': False, 'game_files_modified': False}
    with output.open('x', encoding='utf-8') as stream:
        json.dump(receipt, stream, indent=2)


if __name__ == '__main__':
    main()
