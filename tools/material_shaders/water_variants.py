"""Explicit fixed A/B WaterProgram staging; immutable A is never repinned.

The1076-byte transport selects a variant by its exact program SHA. This helper
only prepares recipient-owned immutable artifacts; it cannot activate a shader.
"""
from __future__ import annotations

import json
import mmap
from pathlib import Path
import struct

from . import inventory,shaderlib,staging,water,water_balanced,water_staging

POLICIES={
    'A':dict(id=1,recipe=water.RECIPE_ID,sha256=water_balanced.BASE_A_SHA256,generate=water.generate),
    'B':dict(id=2,recipe=water_balanced.RECIPE_ID,sha256=water_balanced.PROGRAM_SHA256,generate=water_balanced.generate),
}


def _policy(variant):
    if type(variant) is not str or variant not in POLICIES:
        raise ValueError('Select fixed water variant A or B explicitly')
    return POLICIES[variant]


def configuration(render, *, variant, enabled=False, pixel_path=None):
    policy=_policy(variant);render=water_staging._render(render)
    if type(enabled) is not bool:raise ValueError('Explicit water enable Boolean required')
    if pixel_path is None:
        if enabled:raise ValueError('Enabled water requires the selected staged program')
        name='';pin=bytes(32)
    else:
        path=staging.plain(pixel_path)
        if (path.name!=policy['sha256']+'.ps.bin' or path.parent.name!=policy['recipe']
                or path.parent.parent.name!='MaterialShaders'):
            raise ValueError('Water variant, hash and recipe path disagree')
        name=str(path);pin=bytes.fromhex(policy['sha256'])
    result=struct.pack('<5I',1076,1,*render,int(enabled))+staging._wide(name)+pin
    if len(result)!=1076:raise ValueError('Water1076 transport changed')
    return result


def prepare(recipient_root,render,*,variant,native_report,verified_platform,available_features,private_case=False):
    policy=_policy(variant);render=water_staging._render(render)
    if not isinstance(available_features,(tuple,list,set,frozenset)) or 'improved_water' not in available_features:
        raise ValueError('native_graphics_unavailable')
    if type(private_case) is not bool:raise ValueError('Explicit private case mode required')
    edition,archive_name=staging._layout(verified_platform);root=staging.plain(recipient_root);report=staging.plain(native_report)
    if not root.is_dir() or not report.name.endswith('.private.json') or not report.parent.is_dir():
        raise ValueError('Trusted existing case/LocalData and private report required')
    if private_case:
        work=staging.plain(inventory.ROOT/'work')
        if (report.parent==work or not report.parent.is_relative_to(work) or report.parent.is_relative_to(root)
                or report.exists() or (report.parent/'checkpoint.private.json').exists() or (report.parent/'result.private.json').exists()):
            raise ValueError('Prepare a new private case before checkpoint/launch')
        local=staging.plain(report.parent/'LocalData')
    else:
        local=staging.plain(root/'LocalData')
        if report.parent!=local:raise ValueError('Water output must stay in recipient LocalData')
    source=staging.plain(root/archive_name)
    if not source.is_relative_to(root) or not source.is_file():raise ValueError('Exact recipient graphics archive absent')
    generated=[]
    with source.open('rb') as stream:
        with mmap.mmap(stream.fileno(),0,access=mmap.ACCESS_READ) as raw:
            archive=inventory.bba.Archive(raw,copy=False)
            for resource_name in water.RESOURCES:
                item=policy['generate'](resource_name,inventory.read_payload(archive,resource_name))
                if (len(item.program)!=3840 or shaderlib.digest(item.program)!=policy['sha256']
                        or item.manifest.get('recipe')!=policy['recipe']
                        or item.manifest.get('source_resource')!=resource_name
                        or item.manifest.get('source_resource_sha256')!=water.RESOURCES[resource_name]
                        or item.manifest.get('generated_pixel_sha256')!=policy['sha256']
                        or item.manifest.get('active') is not False
                        or item.manifest.get('public_binary_distribution_permitted') is not False):
                    raise ValueError('Generated water does not match selected fixed policy')
                generated.append(item)
    if not generated or any(item.program!=generated[0].program for item in generated):
        raise ValueError('Water aliases disagree')
    cache=staging.plain(local/'MaterialShaders'/policy['recipe'])
    if not cache.is_relative_to(local):raise ValueError('Water cache escaped LocalData')
    program=staging.plain(cache/(policy['sha256']+'.ps.bin'))
    config=configuration(render,variant=variant,enabled=True,pixel_path=program)
    files=[(program,generated[0].program)]
    for item in generated:
        path=staging.plain(cache/(policy['sha256']+'-'+item.manifest['source_resource_sha256']+'.json'))
        files.append((path,(json.dumps(item.manifest,sort_keys=True,indent=2)+'\n').encode('utf8')))
    exists=[water_staging._existing(path,data) for path,data in files]
    cache.mkdir(parents=True,exist_ok=True)
    for (path,data),present in zip(files,exists):
        if not present:
            with path.open('xb') as stream:stream.write(data)
    return dict(config=config,format='s7-recipient-water-variant-stage-v1',variant=variant,variant_id=policy['id'],
        recipe=policy['recipe'],program_path=str(program),program_sha256=policy['sha256'],program_bytes=3840,
        edition=edition,render=list(render),config_bytes=1076,source_resources=dict(water.RESOURCES),private_case=private_case,
        active=False,runtime_verified=False,visible_quality_qualified=False,no_legacy_river_scaling_permitted=True,
        full_archive_fingerprint_verified=False)
