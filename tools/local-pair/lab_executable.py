"""Fixed development-only profile variants.

The normal adapter rejects these hashes. The LocalPairLab build accepts A/B.
The LocalThreeLab option adds E/F/G. The derived file can be written to an
isolated test copy. Nine string-length operands accompany the longer profile
suffix and preserve the original instruction behavior.
"""
import hashlib

ORIGINAL = '4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f'
HASHES = {
    'A': '6dd278274aa885ff402a277f6d2f3ba05600e1ff841462e038f44bf233ce992f',
    'B': 'dbc108237301f329fed5372f79915c82acd9085f58d32adefa4efe434208ef72',
}
THREE_HASHES = {
    'E': '76f30551e6fb91c3862064cf09faff01a252af88b78087e3c2e881822ed0ae9b',
    'F': '1e87f3d7844faac6704a4855324da46c92463207854ce044941cf610d9f0f4f4',
    'G': '2d329f7b4fdfe861a0af346cb3f01a8e508c535842e35465900433bf61682778',
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


def derive(original, slot, *, three_clients=False):
    hashes = THREE_HASHES if three_clients else HASHES
    if slot not in hashes or hashlib.sha256(original).hexdigest() != ORIGINAL:
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
    if len(result) != len(original) or hashlib.sha256(result).hexdigest() != hashes[slot]:
        raise ValueError('Unexpected laboratory executable')
    return bytes(result)
