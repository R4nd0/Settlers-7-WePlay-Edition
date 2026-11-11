"""Compare authored records with the pinned original Gold gathering writer.

No game process or network. Native holder/scalar/string framing, stream length
finalization and raw dynamic buffer append run in bounded x86 emulation. Borrowed QString lifetime, stream
arena ownership, allocation/reserve, CRT memcpy and WideCharToMultiByte are
stubbed. This does not verify matchmaking semantics or native session dispatch.
"""
import argparse
import json
from pathlib import Path
import struct

from emulate_checksum import original_bytes


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def examine(path):
    import pefile
    import gathering_codec as codec
    from login_server import Reader
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EBX, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI
    raw = original_bytes(path)
    pe = pefile.PE(data=raw)
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(0x400000, pe.OPTIONAL_HEADER.SizeOfImage + 4095 & ~4095)
    u.mem_write(0x400000, pe.get_memory_mapped_image())
    S = 0x20000000
    STACK = 0x30000000
    STOP = S + 0xff000
    u.mem_map(S, 0x100000)
    u.mem_map(STACK, 0x10000)
    STREAM = S
    OUTBUF = S + 256
    OBJECT = S + 512
    ADAPTER = S + 1024
    DATA = S + 0x10000
    ATTR = S + 0x20000
    DESC = S + 0x30000
    TAG = S + 0x31000
    WIDE_STUB = S + 0xf1000
    pool = S + 0x40000

    def put(at, *values):
        u.mem_write(at, struct.pack('<' + 'I' * len(values), *values))

    def word(at):
        return struct.unpack('<I', u.mem_read(at, 4))[0]

    def returned(value=0, cleanup=0):
        esp = u.reg_read(UC_X86_REG_ESP)
        pc = word(esp)
        u.reg_write(UC_X86_REG_ESP, esp + 4 + cleanup)
        u.reg_write(UC_X86_REG_EAX, value)
        u.reg_write(UC_X86_REG_EIP, pc)

    def allocate(size):
        nonlocal pool
        require(0 < size <= 0x10000, 'Native gathering oracle guard failed')
        at = pool
        pool += size + 15 & ~15
        require(pool < S + 0xe0000, 'Native gathering oracle guard failed')
        u.mem_write(at, b'\x00' * size)
        return at

    def code(uc, address, size, unused):
        esp = u.reg_read(UC_X86_REG_ESP)
        self = u.reg_read(UC_X86_REG_ECX)
        if address in (0xb94140, 0xb94160):
            source = word(esp + 4)
            put(self, source if address == 0xb94140 else word(source))
            returned(self, 4)
        elif address in (0xb94180, 0xc64410):
            returned()
        elif address == 0xc64840:
            buf = allocate(32)
            data = allocate(0x10000)
            put(buf, 0, 1, data, 4, 0x10000)
            u.mem_write(self, b'\x00' * 48)
            put(self + 4, buf)
            put(self + 8, 4)
            returned(self)
        elif address == 0xc459a0:
            require(word(esp + 4) <= word(self + 16), 'Native gathering oracle guard failed')
            returned(1, 4)
        elif address == 0xb93760:
            returned(word(esp + 4))
        elif address == 0xb93820:
            returned(allocate(word(esp + 4)))
        elif address == 0xccd820:
            returned()
        elif address == WIDE_STUB:
            cp, flags, src, units, dest, capacity, default, used = [word(esp + x) for x in range(4, 36, 4)]
            require(cp == 65001 and flags == 0 and (units == 0xffffffff) and (not default) and (not used), 'Native gathering oracle guard failed')
            wide = bytearray()
            for offset in range(0, 4098, 2):
                unit = bytes(u.mem_read(src + offset, 2))
                if unit == b'\x00\x00':
                    break
                wide.extend(unit)
            else:
                raise AssertionError('Unbounded fixture string')
            encoded = bytes(wide).decode('utf-16le').encode('utf-8') + b'\x00'
            if dest:
                require(capacity >= len(encoded) and S <= dest < dest + len(encoded) < S + 0xf0000, 'Native gathering oracle guard failed')
                u.mem_write(dest, encoded)
            else:
                require(capacity == 0, 'Native gathering oracle guard failed')
            returned(len(encoded), 32)
        elif address == 0xbaee6c:
            dest, src, count = [word(esp + x) for x in (4, 8, 12)]
            require(count <= 0x10000 and S <= dest <= dest + count < S + 0xf0000, 'Native gathering oracle guard failed')
            if count:
                u.mem_write(dest, bytes(u.mem_read(src, count)))
            returned(dest)

    def writes(uc, access, address, size, value, unused):
        require(S <= address <= address + size < S + 0xf0000 or STACK <= address <= address + size <= STACK + 0x10000, hex(address))
    u.hook_add(UC_HOOK_CODE, code)
    u.hook_add(UC_HOOK_MEM_WRITE, writes)
    put(0x10cb4e0, 1)
    original_codepage = word(0x1038f34)
    require(original_codepage == 65001, original_codepage)
    put(0xebf250, WIDE_STUB)
    cases = []
    for description in (None, '', 'WePlay', 'Gr\u00fc\u00dfe \u6c34'):
        for n in (0, 1, 140, 142, 8192):
            for scalar in (0, 0x12345678, 0xffffffff):
                value = codec.Gathering((scalar, 2, 3, 4, 5, 6, 7), (0, 65535),
                    description, bytes(i & 255 for i in range(n)))
                cases.append((value, None))
        for scalar in (0, 0x12345678, 0xffffffff):
            base = codec.Gathering((scalar, 2, 3, 4, 5, 6, 7), (0, 65535), description, b'')
            for flags in ((0, 0, 0, 0), (1, 2, 128, 255), (128, 0, 255, 0), (0, 255, 0, 128)):
                for extra in (None, scalar):
                    value = codec.SessionGathering(base, ('map', 'hash', 'template', description),
                        (scalar, 2, 3, 4, 5, 6), tuple(bool(v) for v in flags), extra)
                    cases.append((value, flags))
    count = 0
    for value, flags in cases:
        pool = S + 0x40000
        session = isinstance(value, codec.SessionGathering)
        base = value.base if session else value
        u.mem_write(OBJECT, b'\x00' * 256)
        table = (0xf5a1cc if value.matchmaking_word is None else 0xf5a1f0) if session else 0xeed7a0
        put(OBJECT, table)
        a, b, c, d, e, f, g = base.words
        u.mem_write(OBJECT + 4, struct.pack('<IIIHHIIII', a, b, c, *base.shorts, d, e, f, g))
        if base.description is not None:
            u.mem_write(DESC, base.description.encode('utf-16le') + b'\x00\x00')
        put(OBJECT + 0x24, DESC if base.description is not None else 0)
        if session:
            for index, (offset, text) in enumerate(zip((0x28, 0x2c, 0x30, 0x44), value.strings)):
                address = S + 0x32000 + index * 0x1000
                if text is not None:
                    u.mem_write(address, text.encode('utf-16le') + b'\0\0')
                put(OBJECT + offset, address if text is not None else 0)
            put(OBJECT + 0x34, *value.words[:4])
            u.mem_write(OBJECT + 0x48, bytes(flags[:3]))
            put(OBJECT + 0x4c, *value.words[4:])
            u.mem_write(OBJECT + 0x54, bytes(flags[3:]))
            if value.matchmaking_word is not None:
                put(OBJECT + 0x58, value.matchmaking_word)
        else:
            n = len(value.attributes)
            if n:
                u.mem_write(ATTR, value.attributes)
            put(OBJECT + 0x28, 0, 1, ATTR, n, n)
            u.mem_write(TAG, ('DynamicGathering' + '\0').encode('utf-16le'))
            put(OBJECT + 0x3c, TAG)
        put(OUTBUF, 0, 1, DATA, 0, 0x10000)
        put(STREAM + 4, OUTBUF)
        stack = STACK + 0xf000
        put(stack, STOP, STREAM, OBJECT)
        saved = {UC_X86_REG_EBX: 0x1234abcd, UC_X86_REG_EBP: 0x2345bcde,
            UC_X86_REG_ESI: 0x3456cdef, UC_X86_REG_EDI: 0x4567def0}
        for reg, v in {**saved, UC_X86_REG_ECX: ADAPTER, UC_X86_REG_ESP: stack}.items():
            u.reg_write(reg, v)
        u.emu_start(0xcb9e70, STOP, count=200000, timeout=2000000)
        require(u.reg_read(UC_X86_REG_EIP) == STOP and u.reg_read(UC_X86_REG_ESP) == stack + 12,
            'Native gathering return/stack mismatch')
        require(all(u.reg_read(k) == v for k, v in saved.items()), 'Native gathering callee-save mismatch')
        native = bytes(u.mem_read(DATA, word(OUTBUF + 0xc)))
        require(native == codec.encode(value), ('gathering-wire-mismatch', len(native), len(codec.encode(value))))
        require(codec.decode(native, Reader) == value, 'Native gathering decode mismatch')
        count += 1
    result = {'cases': count, 'formats': ['DynamicGathering', 'S7Session', 'S7SessionMM'],
        'native_holder_writer': True, 'native_scalar_writer': True, 'native_string_framing': True,
        'native_boolean_normalization': True, 'original_codepage': original_codepage,
        'native_raw_attribute_append': True, 'abi_preserved': True, 'actual_game_or_network': False,
        'native_stream_length_finalization': True,
        'stubs': ['borrowed-qstring-construction-copy-and-dtor', 'bounded-stream-lifecycle',
            'arena-allocation', 'buffer-reserve-no-growth', 'CRT-memcpy', 'WideCharToMultiByte']}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original_gold_exe", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    work = Path(__file__).resolve().parents[2] / "work"
    if not output.is_relative_to(work.resolve()) or output.exists() or not output.parent.is_dir():
        parser.error("Use a new output file in an existing directory under repository work/")
    result = examine(args.original_gold_exe)
    output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
