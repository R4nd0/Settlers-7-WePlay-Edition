"""Recipient-generated River optics candidate; no original program is bundled.

Replaces the original restricted cosine-squared reflection response with a
bounded Schlick response, shapes the existing optical depth transfer and
broadens/limits its existing specular lobe. Textures, flow, clipping, atmosphere,
alpha and scene geometry remain the recipient's original operations.

This is an offline candidate. Native staging, GPU execution and visible quality
qualification are deliberately separate from bytecode generation.
"""
from __future__ import annotations

import math
import struct

from . import bytecode, recipes, shaderlib
from .semantics import reflect

RECIPE_ID = 'river-depth-fresnel-v1-candidate'
ORIGINAL_VERTEX = 'f190d66ee1ae43637341a76e5b644cc63bf3c2a5b5c94ea9286764e13f1c16c0'
ORIGINAL_PIXEL = 'dd65ba0b9d79c7c4d9ad9904404388230556b194eedb8cbdfbd4fa99f44eee5b'
RESOURCES = {
    'Graphics/Shaders/Water_ATI_High.ShaderLib': 'f5c575f9015fefd2f33e4a7fdce00bcadc68b54590844b3c08d1c4e48df1efe2',
    'Graphics/Shaders/Water_ATIx_High.ShaderLib': '3a018f42a0de1a14338076beccedcb27395e345a02560610c1ed5fcf9ec6f0f0',
    'Graphics/Shaders/Water_NV_High.ShaderLib': '2173334cdbe9223fb412cf93e96b3178176039c11421b87dec8417da08e10788',
}
F0, GRAZING_CAP = .02, .85
SPECULAR_EXPONENTS = (96.0, 32.0)
SPECULAR_PEAK_SCALE = sum(SPECULAR_EXPONENTS) / (256.0 + 96.0)


def _pack(*words):
    return struct.pack('<' + 'I' * len(words), *words)


def _dst(index, mask=15, *, saturate=False):
    return 0x80000000 | mask << 16 | index | (0x00100000 if saturate else 0)


def _src(index, swizzle=0xe4, *, constant=False, input=False, negate=False):
    return (0xa0000000 if constant else 0x90000000 if input else 0x80000000) | swizzle << 16 | index | (0x01000000 if negate else 0)


def _fresnel_block(constant):
    # r1.xyz is the original normalized Fresnel normal; -v3 is the original
    # normalized view direction. r0.x/r1.x are already scratch in this block.
    # The optical constant is [F0,cap-F0,3,2]; use the original c4.x=-1
    # only at this exact source identity for the complement's positive one.
    return _pack(
        0x03000008, _dst(0, 1, saturate=True), _src(3, input=True, negate=True), _src(1),
        0x03000002, _dst(0, 1), _src(4, 0, constant=True, negate=True), _src(0, 0, negate=True),
        0x03000005, _dst(1, 1), _src(0, 0), _src(0, 0),
        0x03000005, _dst(1, 1), _src(1, 0), _src(1, 0),
        0x03000005, _dst(0, 1), _src(0, 0), _src(1, 0),
        0x04000004, _dst(0, 1), _src(0, 0), _src(constant, 0x55, constant=True), _src(constant, 0, constant=True),
        0x03000005, _dst(0, 1, saturate=True), _src(0, 0), _src(6, 0x55, input=True),
    )


def _depth_block(constant, temporary):
    # The original pow_sat already supplied L in[0,1]. Smoothstep changes the
    # depth transfer before both refraction tint and caustic attenuation use L.
    return _pack(
        0x03000005, _dst(temporary, 1), _src(1, 0x55), _src(1, 0x55),
        0x04000004, _dst(temporary, 2), _src(1, 0x55, negate=True), _src(constant, 0xff, constant=True), _src(constant, 0xaa, constant=True),
        0x03000005, _dst(1, 2, saturate=True), _src(temporary, 0), _src(temporary, 0x55),
    )


def _unique(items, label):
    if len(items) != 1:
        raise ValueError('No unique original River ' + label)
    return items[0]


def _outputs(program):
    return [item.words for item in bytecode.decode(program)
            if item.opcode not in (31,47,48,81,0xfffe) and len(item.words)>1
            and bytecode.register_type(item.words[1]) in (8,9)]


def _transform(program):
    instructions = bytecode.decode(program)
    occupied, temporaries = set(), set()
    for binding in reflect(program):
        if binding['register_set'] == 'float4':
            start, count = binding['register_index'], binding['register_count']
            if not 0 <= start < 224 or not 0 < count <= 224-start:
                raise ValueError('Invalid original River constant metadata')
            occupied.update(range(start, start+count))
    for item in instructions:
        if item.opcode == 0xfffe:
            continue
        if item.words[0] & 0x10000000 or item.opcode in (20,21,22,23,24,25,26,27,28,29,30,38,39,40,41,42,43,44,45,96):
            raise ValueError('Unsupported relative/control-flow River program')
        operands = item.words[1:2] if item.opcode in (47,48,81) else item.words[2:] if item.opcode==31 else item.words[1:]
        for token in operands:
            if not token & 0x80000000 or token & 0x2000:
                raise ValueError('Unsupported River operand')
            kind, index = bytecode.register_type(token), token & 0x7ff
            if kind in (11,12,13):
                raise ValueError('Extended River constant bank')
            if kind == 2: occupied.add(index)
            elif kind == 0: temporaries.add(index)
    constants = [i for i in range(223,-1,-1) if i not in occupied]
    temporary = next((i for i in range(32) if i not in temporaries), None)
    if len(constants) < 2 or temporary is None:
        raise ValueError('River optical extension has no unused registers')
    optical, energy = constants[:2]
    depth = _unique([i for i in instructions if i.words ==
        (0x03000020,0x80120001,0x80000000,0x90aa0006)], 'optical-depth transfer')
    start = _unique([n for n,i in enumerate(instructions) if i.words ==
        (0x03000008,0x80010000,0x91e40003,0x80e40001)], 'view-normal dot')
    fresnel = instructions[start:start+8]
    if ([i.opcode for i in fresnel] != [8,5,5,11,2,2,88,5]
            or fresnel[-1].words != (0x03000005,0x80010000,0x80000000,0x90550006)):
        raise ValueError('Unsupported original River reflection transfer')
    spec = _unique([i for i in instructions if i.opcode==81 and i.words[1]==0xa00f000e], 'specular declaration')
    spec_values = struct.unpack('<4f', _pack(*spec.words[2:]))
    if spec_values[2:] != (256.0,96.0):
        raise ValueError('Unexpected original River specular exponents')
    complement = _unique([i for i in instructions if i.opcode==81 and i.words[1]==0xa00f0004], 'complement declaration')
    if struct.unpack('<f',_pack(complement.words[2]))[0] != -1.0:
        raise ValueError('Unexpected original River complement constant')
    join = _unique([i for i in instructions if i.words ==
        (0x03000005,0x80070001,0x80000001,0x80f90001)], 'specular product')
    first = next(i.offset for i in instructions if i.opcode not in (31,47,48,81,0xfffe))
    definitions = struct.pack('<2I4f2I4f',0x05000051,0xa00f0000|optical,F0,GRAZING_CAP-F0,3,2,
                              0x05000051,0xa00f0000|energy,SPECULAR_PEAK_SCALE,0,0,0)
    edits = [
        (first, first, definitions),
        (depth.offset+16, depth.offset+16, _depth_block(optical, temporary)),
        (fresnel[0].offset, fresnel[-1].offset+16, _fresnel_block(optical)),
        (spec.offset, spec.offset+24, struct.pack('<2I4f',spec.words[0],spec.words[1],*spec_values[:2],*SPECULAR_EXPONENTS)),
        (join.offset+16, join.offset+16, _pack(0x03000005,_dst(1,7),_src(1),_src(energy,0,constant=True))),
    ]
    changed = bytearray(); cursor = 0
    for begin, end, replacement in sorted(edits):
        if begin < cursor:
            raise ValueError('Overlapping River optics edits')
        changed.extend(program[cursor:begin]); changed.extend(replacement); cursor=end
    changed.extend(program[cursor:]); changed=bytes(changed)
    decoded = bytecode.decode(changed)
    # Conservative upper bound: our additions are scalar ALU. Existing nrm,
    # pow and lrp can consume multiple instruction slots; count each as4.
    slots = sum(4 if i.opcode in (32,36,18) else 1 for i in decoded if i.opcode not in (31,47,48,81,0xfffe))
    if slots > 512:
        raise ValueError('River optics exceeds minimum SM3 instruction budget')
    if _outputs(program) != _outputs(changed):
        raise ValueError('River output/alpha writers changed unexpectedly')
    original_textures = [i.words for i in instructions if i.opcode in (66,95)]
    if original_textures != [i.words for i in decoded if i.opcode in (66,95)]:
        raise ValueError('River texture reads changed unexpectedly')
    return changed, dict(effect='river_depth_fresnel_specular',
        fresnel_model='bounded_schlick', normal_incidence_reflectance=F0,
        grazing_cap=GRAZING_CAP, depth_transfer='L*L*(3-2*L)',
        original_specular_exponents=[256,96], candidate_specular_exponents=list(SPECULAR_EXPONENTS),
        specular_peak_scale=SPECULAR_PEAK_SCALE, local_constants=[optical,energy],
        local_temporary=temporary, texture_reads=len(original_textures), added_texture_reads=0,
        conservative_instruction_slots=slots, original_output_alpha_atmosphere_preserved=True,
        original_flow_and_refraction_coordinates_preserved=True, original_cubemap_only=True,
        runtime_verified=False, replacement_ready=False)


def response(cosine, optical_depth_tint, reflectiveness=1.0):
    """Finite scalar reference for tests/receipts; not a live renderer model."""
    values = (cosine, optical_depth_tint, reflectiveness)
    if any(type(v) not in (float,int) or not math.isfinite(v) for v in values):
        raise ValueError('Water response requires finite scalar inputs')
    cosine, tint = max(0,min(1,cosine)), max(0,min(1,optical_depth_tint))
    return dict(reflection=max(0,min(1,(F0+(GRAZING_CAP-F0)*(1-cosine)**5)*reflectiveness)),
                optical_depth_tint=tint*tint*(3-2*tint))


def generate(resource_name, original_resource):
    expected = RESOURCES.get(resource_name)
    if expected is None or shaderlib.digest(original_resource) != expected:
        raise ValueError('Unsupported original River resource identity')
    blocks = {b.stage:b for b in shaderlib.parse(original_resource).blocks if b.shader=='RiverColor'}
    if (set(blocks) != {'vertex','pixel'} or len(blocks['vertex'].code)!=2592 or len(blocks['pixel'].code)!=3740
            or shaderlib.digest(blocks['vertex'].code)!=ORIGINAL_VERTEX or shaderlib.digest(blocks['pixel'].code)!=ORIGINAL_PIXEL):
        raise ValueError('Original River VS/PS pair identity mismatch')
    program, details = _transform(blocks['pixel'].code)
    return recipes.Generated(program, dict(format='s7-recipient-material-program-v1',recipe=RECIPE_ID,
        maintainer='WePlay Community', source_resource=resource_name,source_resource_sha256=expected,
        original_vertex_sha256=ORIGINAL_VERTEX,original_pixel_sha256=ORIGINAL_PIXEL,
        generated_pixel_sha256=shaderlib.digest(program),generated_bytes=len(program),
        generated_from_recipient_owned_resource=True,public_binary_distribution_permitted=False,
        classify_original_pair_before_temporary_bind=True,restore_original_shader_after_draw=True,
        active=False,production_staging_supported=False,**details))
