"""Produce an ordinal-preserving export definition from the user's exact Miles DLL."""
import hashlib
from pathlib import Path
import struct

ORIGINAL = '301f351311b96964ca0ec21df68cec38ccaa9c6ffc082987ca24ce35ca39f5bf'


def add_forwarders(gate, original):
    """Append a read-only PE forwarder table to our own newly compiled gate.

    The audio DLL and game are never edited. Building the EAT directly also
    avoids LINK's x86 cdecl alias decoration and preserves all original names.
    """
    lines = definition(original).splitlines()[2:]
    exports = []
    for line in lines:
        left, rest = line.split('=', 1)
        target, ordinal, *attributes = rest.split()
        exports.append((int(ordinal[1:]), None if attributes else left, target))
    data = bytearray(Path(gate).read_bytes())
    pe = struct.unpack_from('<I', data, 60)[0]
    optional = pe+24
    count = struct.unpack_from('<H', data, pe+6)[0]
    optional_size = struct.unpack_from('<H', data, pe+20)[0]
    file_alignment, section_alignment = struct.unpack_from('<II', data, optional+36)[0], struct.unpack_from('<I', data, optional+32)[0]
    header_size = struct.unpack_from('<I', data, optional+60)[0]
    table = optional+optional_size
    slot = table+count*40
    if (data[pe:pe+6] != b'PE\0\0\x4c\x01' or optional_size != 224
            or count > 12 or slot+40 > header_size or any(data[slot:slot+40])
            or struct.unpack_from('<II', data, optional+96) != (0, 0)
            or struct.unpack_from('<II', data, optional+128) != (0, 0)):
        raise ValueError('Unexpected unsigned x86 gate PE layout.')

    def align(value, alignment):
        if alignment not in (512, 4096):
            raise ValueError('Unexpected gate PE alignment.')
        return (value+alignment-1)//alignment*alignment

    sections = [struct.unpack_from('<8sIIII', data, table+i*40) for i in range(count)]
    rva = align(max(va+max(virtual, stored) for _, virtual, va, stored, _ in sections), section_alignment)
    offset = align(len(data), file_alignment)
    base = min(item[0] for item in exports)
    functions = max(item[0] for item in exports)-base+1
    named = sorted((name, ordinal) for ordinal, name, _ in exports if name is not None)
    eat, names, ordinals = 40, 40+functions*4, 40+functions*4+len(named)*4
    body = bytearray(ordinals+len(named)*2)

    def string(value):
        address = rva+len(body)
        body.extend(value.encode('ascii')+b'\0')
        return address

    module_name = string('mss32.dll')
    for ordinal, _, target in exports:
        struct.pack_into('<I', body, eat+(ordinal-base)*4, string(target))
    for index, (name, ordinal) in enumerate(named):
        struct.pack_into('<I', body, names+index*4, string(name))
        struct.pack_into('<H', body, ordinals+index*2, ordinal-base)
    struct.pack_into('<IIHH7I', body, 0, 0, 0, 0, 0, module_name, base, functions, len(named), rva+eat, rva+names, rva+ordinals)
    stored = align(len(body), file_alignment)
    struct.pack_into('<8s8I', data, slot, b'.wp_eat\0', len(body), rva, stored, offset, 0, 0, 0, 0x40000040)
    struct.pack_into('<H', data, pe+6, count+1)
    initialized = struct.unpack_from('<I', data, optional+8)[0]
    struct.pack_into('<I', data, optional+8, initialized+stored)
    struct.pack_into('<I', data, optional+56, align(rva+len(body), section_alignment))
    struct.pack_into('<I', data, optional+64, 0)
    struct.pack_into('<II', data, optional+96, rva, len(body))
    data.extend(b'\0'*(offset-len(data)))
    data.extend(body)
    data.extend(b'\0'*(stored-len(body)))
    Path(gate).write_bytes(data)
    return len(exports)


def definition(path):
    data = Path(path).read_bytes()
    if hashlib.sha256(data).hexdigest() != ORIGINAL:
        raise ValueError('Unknown original Miles library.')
    pe = struct.unpack_from('<I', data, 60)[0]
    optional = pe+24
    count = struct.unpack_from('<H', data, pe+6)[0]
    size = struct.unpack_from('<H', data, pe+20)[0]
    sections = [struct.unpack_from('<8sIIII', data, optional+size+i*40) for i in range(count)]

    def raw(rva):
        return next(rva-va+offset for _, virtual, va, stored, offset in sections if va <= rva < va+min(virtual, stored))

    export = struct.unpack_from('<I', data, optional+96)[0]
    _, _, _, _, _, base, functions, names, function_table, name_table, ordinal_table = struct.unpack_from('<IIHH7I', data, raw(export))
    symbols = {}
    for index in range(names):
        rva = struct.unpack_from('<I', data, raw(name_table)+index*4)[0]
        name = data[raw(rva):].split(b'\0', 1)[0].decode('ascii')
        if any(char not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_@?' for char in name):
            raise ValueError('Unexpected original Miles export.')
        slot = struct.unpack_from('<H', data, raw(ordinal_table)+index*2)[0]
        if slot in symbols:
            raise ValueError('Ambiguous original Miles ordinal.')
        symbols[slot] = name
    result = ['LIBRARY mss32', 'EXPORTS']
    for slot in range(functions):
        if not struct.unpack_from('<I', data, raw(function_table)+slot*4)[0]:
            continue
        ordinal = base+slot
        name = symbols.get(slot, '_weplay_ordinal_'+str(ordinal))
        target = symbols.get(slot, '#'+str(ordinal))
        result.append(name+'=weplay_miles_original.'+target+' @'+str(ordinal)+(' NONAME' if slot not in symbols else ''))
    return '\n'.join(result)+'\n'


if __name__ == '__main__':
    import sys
    if len(sys.argv) == 4 and sys.argv[1] == '--append':
        print('Preserved Miles export names and ordinals:', add_forwarders(sys.argv[2], sys.argv[3]))
    else:
        Path(sys.argv[2]).write_text(definition(sys.argv[1]), encoding='ascii')
