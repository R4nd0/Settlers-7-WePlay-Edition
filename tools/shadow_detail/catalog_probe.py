"""Stage scalar-only high material+shadow catalog and read-only mixed probe.

No shader bodies are exported, no effect is activated, and no game is launched.
Shadow records use legacy numeric family2 with explicit shadow metadata because
the existing native observer ABI accepts only three numeric family values.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import mmap
from pathlib import Path
import struct

from tools.material_shaders import catalog, inventory, semantics, shaderlib
from tools.graphics_lab.reconfigure import validate_config

RESOURCES = catalog.DEFAULT_HIGH + tuple(('terrain', f'Graphics/Shaders/Shadow_{vendor}_High.ShaderLib')
                                         for vendor in ('ATI', 'ATIx', 'NV'))
RIVER = 'Graphics/Shaders/Water_ATI_High.ShaderLib'
SHADOW = 'Graphics/Shaders/Shadow_ATI_High.ShaderLib'
SHADOW_VS = '7744611870305c0b6b9a330010cda3599c34080774b7f11bef367fdcd21eb698'
SHADOW_PS = '6be44bdf12ff3d9b94cadc9fed076c55e26207d4a4233120e5c073a76dcd38ea'
RIVER_VS = 'f190d66ee1ae43637341a76e5b644cc63bf3c2a5b5c94ea9286764e13f1c16c0'
RIVER_PS = 'dd65ba0b9d79c7c4d9ad9904404388230556b194eedb8cbdfbd4fa99f44eee5b'


def mixed_config(metadata, bindings):
    entries = metadata.get('entries')
    if not isinstance(entries, list) or not 1 <= len(entries) <= 512:
        raise ValueError('Invalid bounded observer catalog.')
    def pair(resource, record):
        rows = [r for r in entries if r['resource_name'] == resource and r['shader_record'] == record]
        if len(rows) != 2 or {r['stage'] for r in rows} != {'vertex', 'pixel'}:
            raise ValueError('An exact original VS/PS pair is required.')
        return {r['stage']: r for r in rows}
    river, shadow = pair(RIVER, 'RiverColor'), pair(SHADOW, 'MaskShader')
    if (river['vertex']['compiled_sha256'] != RIVER_VS or river['pixel']['compiled_sha256'] != RIVER_PS
            or river['vertex']['compiled_bytes'] != 2592 or river['pixel']['compiled_bytes'] != 3740):
        raise ValueError('Original River program identity changed.')
    if (shadow['vertex']['compiled_sha256'] != SHADOW_VS or shadow['pixel']['compiled_sha256'] != SHADOW_PS
            or shadow['vertex']['compiled_bytes'] != 856 or shadow['pixel']['compiled_bytes'] != 2216):
        raise ValueError('Original shadow program identity changed.')
    def constant(stages, stage, name, index, count):
        rows = [r for r in bindings if r['compiled_sha256'] == stages[stage]['compiled_sha256'] and r['stage'] == stage]
        if not rows:
            raise ValueError('Missing exact compiled CTAB reflection.')
        values = [b for b in rows[0]['bindings'] if b['name'] == name]
        if len(values) != 1 or values[0]['register_set'] != 'float4' or values[0]['register_index'] != index or values[0]['register_count'] != count:
            raise ValueError('Compiled constant binding changed.')
    for stage, name, index, count in (
        ('vertex', 'g_VSMainViewProjectionMatrix', 12, 4), ('vertex', 'g_VSEye', 16, 1),
        ('pixel', 'g_PSSunDirection', 5, 1), ('pixel', 'g_PSSunSpecularColor', 7, 1), ('pixel', 'g_PSCamera', 0, 1)):
        constant(river, stage, name, index, count)
    constant(shadow, 'pixel', 'g_InverseShadowMapSize', 57, 1)
    constant(shadow, 'pixel', 'g_KernelRadius', 33, 1)
    reflected = [r for r in bindings if r['compiled_sha256'] == SHADOW_PS and r['stage'] == 'pixel']
    sampler = [b for b in reflected[0]['bindings'] if b['name'] == 'g_ShadowMap']
    if len(sampler) != 1 or sampler[0]['register_set'] != 'sampler' or sampler[0]['register_index'] != 1 or sampler[0]['register_count'] != 1:
        raise ValueError('Compiled shadow sampler binding changed.')
    targets = [
        (river, [(1, i, 15) for i in range(12, 16)] + [(1, 16, 7), (2, 5, 7), (2, 7, 7), (2, 0, 8)], 0),
        (shadow, [(2, 57, 3), (2, 33, 15)], 1 << 1),
    ]
    raw = struct.pack('<4IQ', 952, 1, len(targets), 1, 0)
    summaries = []
    for stages, requests, samplers in targets:
        raw += struct.pack('<5I', stages['vertex']['id'], stages['pixel']['id'], 3, len(requests), samplers)
        raw += b''.join(struct.pack('<3I', *request) for request in requests) + bytes((8 - len(requests)) * 12)
        summaries.append({'vertex_id': stages['vertex']['id'], 'pixel_id': stages['pixel']['id'],
                          'vertex_sha256': stages['vertex']['compiled_sha256'], 'pixel_sha256': stages['pixel']['compiled_sha256'],
                          'requests': requests, 'sampler_mask': samplers})
    raw += bytes((8 - len(targets)) * 116)
    digest = hashlib.sha256(raw).hexdigest()
    validate_config(raw, digest)
    return raw, {'format': 's7-shadow-camera-observation-config-v1', 'config_sha256': digest,
                 'catalog_sha256': metadata['catalog_sha256'], 'targets': summaries, 'frame_stride': 1,
                 'sample_limit': 3, 'read_only_observer': True, 'shadow_effect_enabled': False,
                 'sun_elevation': None, 'exposure': None, 'runtime_binding_observed': False}


def build(archive_path, expected_directory):
    archive_path = Path(archive_path).resolve()
    if not archive_path.is_relative_to((inventory.ROOT / 'work').resolve()):
        raise ValueError('Use the explicit owned graphics archive under work/.')
    binary, metadata = catalog.from_archive(archive_path, expected_directory, RESOURCES)
    bindings = []
    expected = {r['resource_name']: r['resource_sha256'] for r in metadata['resources']}
    with archive_path.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as raw:
        archive = inventory.bba.Archive(raw, copy=False)
        if hashlib.sha256(archive.header + archive.directory).hexdigest() != expected_directory:
            raise ValueError('Archive directory changed before reflection.')
        for _, name in RESOURCES:
            payload = inventory.read_payload(archive, name)
            if hashlib.sha256(payload).hexdigest() != expected[name]:
                raise ValueError('Selected original shader library changed.')
            for block in shaderlib.parse(payload).blocks:
                bindings.append({'resource': name, 'shader_record': block.shader, 'stage': block.stage,
                    'compiled_sha256': hashlib.sha256(block.code).hexdigest(), 'bindings': semantics.reflect(block.code)})
    for row in metadata['entries']:
        if '/Shadow_' in row['resource_name']:
            row.update(observation_group='shadow', legacy_family_id=2)
    for row in metadata['resources']:
        if '/Shadow_' in row['resource_name']:
            row.update(family='shadow', legacy_family_id=2)
    metadata['numeric_family_compatibility'] = {'1': 'water', '2': 'terrain or explicitly labeled shadow', '3': 'vegetation'}
    config, proof = mixed_config(metadata, bindings)
    return binary, metadata, config, proof, bindings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--directory-sha256', required=True)
    parser.add_argument('--output-directory', type=Path, required=True)
    args = parser.parse_args()
    binary, metadata, config, proof, bindings = build(args.archive, args.directory_sha256)
    catalog.write(binary, metadata, args.output_directory)
    for name, raw in (('mixed-capture.bin', config),):
        with (args.output_directory / name).open('xb') as stream:
            stream.write(raw)
    for name, value in (('mixed-capture.private.json', proof), ('ctab-bindings.private.json', {'entries': bindings})):
        with (args.output_directory / name).open('x', encoding='utf-8') as stream:
            json.dump(value, stream, indent=2)
    print(json.dumps({'entries': metadata['entry_count'], 'catalog_sha256': metadata['catalog_sha256'],
                      'config_sha256': proof['config_sha256'], 'shadow_effect_enabled': False}))


if __name__ == '__main__':
    main()
