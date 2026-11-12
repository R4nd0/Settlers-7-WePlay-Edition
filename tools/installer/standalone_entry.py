"""Small x86 entry bridge shared by the three fingerprint-bound starters."""
import struct


def bridge(remote, image, status, dll, imports, *, initializer='WePlayInitializeStandalone'):
    data = bytearray(4096)
    data[480:490] = b'WPS7ENTRY1'
    at = 512
    constants = {}
    for name, value in [('dll', str(dll).encode('utf-16le') + b'\0\0'),
                        ('init', initializer.encode('ascii') + b'\0')]:
        if at + len(value) > len(data):
            raise ValueError('Der Adapterpfad ist zu lang.')
        constants[name] = remote + at
        data[at:at + len(value)] = value
        at += len(value)
    code, failures, labels = bytearray(), [], {}

    def emit(*values): code.extend(values)
    def word(value): code.extend(struct.pack('<I', value & 0xffffffff))
    def push(value): emit(0x68); word(value)
    def iat(name): emit(0xff, 0x15); word(image + imports[name])
    def fail_if(condition):
        emit(0x0f, condition); failures.append(len(code)); word(0)

    emit(0x9c, 0x60)  # Preserve entry flags and every original register.
    push(constants['dll']); iat('LoadLibraryW'); emit(0x85, 0xc0); fail_if(0x84)
    push(constants['init']); emit(0x50); iat('GetProcAddress'); emit(0x85, 0xc0); fail_if(0x84)
    push(status + 32); emit(0xff, 0xd0, 0x85, 0xc0); fail_if(0x84)
    emit(0xa3); word(status + 8)  # Initializer supplies the verified native OEP.
    emit(0xc7, 5); word(status); word(1)
    iat('GetTickCount'); emit(0x89, 0xc6)
    labels['wait'] = len(code)
    emit(0x83, 0x3d); word(status + 12); emit(1)
    emit(0x0f, 0x84); released = len(code); word(0)
    push(10); iat('Sleep'); iat('GetTickCount')
    emit(0x29, 0xf0, 0x3d); word(15000); fail_if(0x83)
    emit(0xe9); word(labels['wait'] - len(code) - 4)
    labels['released'] = len(code)
    emit(0x61, 0x9d, 0xff, 0x25); word(status + 8)
    failure = len(code)
    emit(0xc7, 5); word(status); word(2)
    push(0xe0537020); emit(0x6a, 0xff); iat('TerminateProcess'); emit(0xcc)
    for position in failures:
        struct.pack_into('<i', code, position, failure - position - 4)
    struct.pack_into('<i', code, released, labels['released'] - released - 4)
    if len(code) > 480:
        raise ValueError('Der Startercode ist zu groß.')
    data[:len(code)] = code
    return bytes(data)
