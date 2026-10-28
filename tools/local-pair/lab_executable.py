"""Two fixed development-only profile variants; never patch the original in place.

The normal adapter rejects these hashes. Only a LocalPairLab adapter accepts
them. Nine string-length operands accompany the longer profile suffix; no
instruction opcode, control flow, simulation, protocol or timing is changed.
"""
import hashlib

ORIGINAL = '4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f'
HASHES = {
    'A': '6dd278274aa885ff402a277f6d2f3ba05600e1ff841462e038f44bf233ce992f',
    'B': 'dbc108237301f329fed5372f79915c82acd9085f58d32adefa4efe434208ef72',
}

# Each of the only three literal callers reserves length+NUL, copies exactly
# length bytes, then advances the logical end and writes NUL. Guard each full
# instruction, replacing only its immediate: reserve 34->35, copy/end 33->34.
LENGTH_EDITS = (
    (0x125E4C, bytes.fromhex('83c022'), bytes.fromhex('83c023')),
    (0x125E61, bytes.fromhex('6a21'), bytes.fromhex('6a22')),
    (0x125E77, bytes.fromhex('83c121'), bytes.fromhex('83c122')),
    (0x127931, bytes.fromhex('8d4722'), bytes.fromhex('8d4723')),
    (0x127943, bytes.fromhex('6a21'), bytes.fromhex('6a22')),
    (0x127959, bytes.fromhex('83c721'), bytes.fromhex('83c722')),
    (0x127CDF, bytes.fromhex('83c022'), bytes.fromhex('83c023')),
    (0x127CF4, bytes.fromhex('6a21'), bytes.fromhex('6a22')),
    (0x127D0A, bytes.fromhex('83c121'), bytes.fromhex('83c122')),
)


def derive(original, slot):
    if slot not in HASHES or hashlib.sha256(original).hexdigest() != ORIGINAL:
        raise ValueError('Wrong executable or laboratory slot')
    result = bytearray(original)
    edits = (
        (0xD0EC24, b'Global\\Settlers7\0', ('Local\\S7Lab0000' + slot).encode('ascii') + b'\0'),
        # The 33-byte source literal has three NUL bytes before the next
        # \\DLC literal. A 34-byte work suffix plus two NUL bytes fits exactly;
        # guard the whole slot and preserve the adjacent literal separately.
        (0xD31594, b'\\THE SETTLERS 7 - History Edition' + b'\0' * 3,
         ('\\Claude\\Projects\\7S_Patcher\\work\\' + slot).encode('ascii') + b'\0' * 2),
    ) + LENGTH_EDITS
    for offset, before, after in edits:
        if len(before) != len(after) or original[offset:offset + len(before)] != before:
            raise ValueError('Laboratory data guard failed')
        result[offset:offset + len(before)] = after
    if original[0xD315B8:0xD315C0] != b'\\DLC' + b'\0' * 4 or result[0xD315B8:0xD315C0] != original[0xD315B8:0xD315C0]:
        raise ValueError('Adjacent laboratory literal guard failed')
    if len(result) != len(original) or hashlib.sha256(result).hexdigest() != HASHES[slot]:
        raise ValueError('Unexpected laboratory executable')
    return bytes(result)
