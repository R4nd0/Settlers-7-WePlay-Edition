"""Gold's fingerprint-bound x86 entry bridge; called before its original entry."""
import struct


def bridge(remote, image, status, dll):
    data = bytearray(4096)
    constants = {}
    at = 512
    for key, value in [('kernel', b'kernel32.dll\0'), ('load', b'LoadLibraryW\0'),
                       ('init', b'GoldInitialize\0'), ('dll', dll.encode('utf-16le') + b'\0\0')]:
        if at + len(value) > len(data):
            raise ValueError('Gold adapter path exceeds the entry bridge limit.')
        constants[key] = remote + at
        data[at:at + len(value)] = value
        at += len(value)
    code, branches = bytearray(), []

    def emit(*values): code.extend(values)
    def word(value): code.extend(struct.pack('<I', value & 0xffffffff))
    def push(value): emit(0x68); word(value)
    def iat(rva): emit(0xff, 0x15); word(image + rva)
    def checked(condition=0x84):
        emit(0x0f, condition); branches.append(len(code)); word(0)

    emit(0x9c, 0x60)  # Preserve the original entry flags/registers.
    push(constants['kernel']); iat(0xabf170); emit(0x85, 0xc0); checked()
    push(constants['load']); emit(0x50); iat(0xabf124); emit(0x85, 0xc0); checked()
    push(constants['dll']); emit(0xff, 0xd0, 0x85, 0xc0); checked()
    push(constants['init']); emit(0x50); iat(0xabf124); emit(0x85, 0xc0); checked()
    emit(0x6a, 0, 0xff, 0xd0, 0x83, 0xf8, 1); checked(0x85)
    emit(0xc7, 5); word(status); word(1)
    emit(0x61, 0x9d)
    # Replay the exact original CALL, then continue at the original next byte.
    emit(0xe8); word(image + 0x7af83e - (remote + len(code) + 4))
    emit(0xe9); word(image + 0x7aee60 - (remote + len(code) + 4))
    failure = len(code)
    emit(0xc7, 5); word(status); word(2)
    push(0xe05370a2); emit(0x6a, 0xff); iat(0xabf240); emit(0xcc)
    for pos in branches:
        struct.pack_into('<i', code, pos, failure - pos - 4)
    if len(code) > 512:
        raise ValueError('Gold entry bridge overlaps its data.')
    data[:len(code)] = code
    return bytes(data), len(code)
