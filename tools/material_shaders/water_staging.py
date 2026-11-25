"""Explicit recipient-generated WaterProgram preparation; no activation.

The trusted host supplies capabilities and a verified portable platform layout.
All three original vendor resources and the generated identity are checked
before creating immutable private/LocalData artifacts. No original archive,
existing program, report, case checkpoint or running process is modified.
"""
from __future__ import annotations

import json
import mmap
from pathlib import Path
import struct

from . import inventory, shaderlib, staging, water

CONFIG_BYTES = 1076
PROGRAM_SHA256 = '8a27791a9907f73d4f42c847bc74a0a25b57da48b81f8e9cb566eca59bfcf0d1'
PROGRAM_BYTES = 3840


def _render(render):
    if (not isinstance(render,(tuple,list)) or len(render)!=2
            or any(type(v) is not int for v in render)
            or not 800<=render[0]<=3840 or not 600<=render[1]<=2160):
        raise ValueError('WaterProgram requires the bounded original render size')
    return tuple(render)


def configuration(render, *, enabled=False, pixel_path=None, pixel_sha256=None):
    render=_render(render)
    if type(enabled) is not bool:
        raise ValueError('WaterProgram enable must be explicit Boolean')
    if pixel_path is None:
        if enabled or pixel_sha256 is not None:
            raise ValueError('Enabled WaterProgram requires the exact staged pixel program')
        name='';pin=bytes(32)
    else:
        path=staging.plain(pixel_path)
        if (pixel_sha256!=PROGRAM_SHA256 or path.name!=PROGRAM_SHA256+'.ps.bin'
                or path.parent.name!=water.RECIPE_ID or path.parent.parent.name!='MaterialShaders'):
            raise ValueError('Unexpected WaterProgram recipe or generated program identity')
        name=str(path);pin=bytes.fromhex(pixel_sha256)
    wide=staging._wide(name)
    result=struct.pack('<5I',CONFIG_BYTES,1,*render,int(enabled))+wide+pin
    if len(result)!=CONFIG_BYTES:
        raise ValueError('WaterProgram fixed transport size changed')
    return result


def _existing(path, expected):
    path=staging.plain(path)
    if path.exists():
        if not path.is_file() or path.stat().st_size!=len(expected) or path.read_bytes()!=expected:
            raise ValueError('Existing immutable water artifact is changed or corrupt')
        return True
    return False


def prepare(recipient_root, render, *, native_report, verified_platform,
            available_features, private_case=False):
    render=_render(render)
    if (not isinstance(available_features,(tuple,list,set,frozenset))
            or 'improved_water' not in available_features):
        raise ValueError('native_graphics_unavailable')
    if type(private_case) is not bool:
        raise ValueError('Private water case mode must be explicit Boolean')
    edition,archive_name=staging._layout(verified_platform)
    root=staging.plain(recipient_root)
    report=staging.plain(native_report)
    if not root.is_dir() or not report.name.endswith('.private.json') or not report.parent.is_dir():
        raise ValueError('Use an existing trusted case/LocalData directory and private report')
    if private_case:
        work=staging.plain(inventory.ROOT/'work')
        if (report.parent==work or not report.parent.is_relative_to(work)
                or report.parent.is_relative_to(root) or report.exists()
                or (report.parent/'result.private.json').exists()
                or (report.parent/'checkpoint.private.json').exists()):
            raise ValueError('Water preparation requires a new separate private case before checkpoint/launch')
        local=staging.plain(report.parent/'LocalData')
    else:
        local=staging.plain(root/'LocalData')
        if report.parent!=local:
            raise ValueError('Production water artifacts require the recipient LocalData trust root')
    archive_path=staging.plain(root/archive_name)
    if not archive_path.is_relative_to(root) or not archive_path.is_file():
        raise ValueError('Exact recipient graphics archive is absent')
    generated=[]
    with archive_path.open('rb') as stream:
        with mmap.mmap(stream.fileno(),0,access=mmap.ACCESS_READ) as raw:
            archive=inventory.bba.Archive(raw,copy=False)
            for resource_name in water.RESOURCES:
                item=water.generate(resource_name,inventory.read_payload(archive,resource_name))
                if (len(item.program)!=PROGRAM_BYTES or shaderlib.digest(item.program)!=PROGRAM_SHA256
                        or item.manifest.get('generated_pixel_sha256')!=PROGRAM_SHA256
                        or item.manifest.get('source_resource')!=resource_name
                        or item.manifest.get('source_resource_sha256')!=water.RESOURCES[resource_name]
                        or item.manifest.get('active') is not False
                        or item.manifest.get('public_binary_distribution_permitted') is not False):
                    raise ValueError('Water generation does not match the exact native program pin')
                generated.append(item)
    if any(item.program!=generated[0].program for item in generated):
        raise ValueError('Water vendor aliases generate inconsistent programs')
    cache=staging.plain(local/'MaterialShaders'/water.RECIPE_ID)
    if not cache.is_relative_to(local):
        raise ValueError('Water cache escaped the admitted LocalData root')
    program_path=staging.plain(cache/(PROGRAM_SHA256+'.ps.bin'))
    config=configuration(render,enabled=True,pixel_path=program_path,pixel_sha256=PROGRAM_SHA256)
    artifacts=[(program_path,generated[0].program)]
    for item in generated:
        raw=(json.dumps(item.manifest,sort_keys=True,indent=2)+'\n').encode('utf8')
        artifacts.append((staging.plain(cache/(PROGRAM_SHA256+'-'+item.manifest['source_resource_sha256']+'.json')),raw))
    # Validate ALL existing artifacts and path lengths before creating anything.
    present=[_existing(path,raw) for path,raw in artifacts]
    cache.mkdir(parents=True,exist_ok=True)
    for (path,raw),exists in zip(artifacts,present):
        if not exists:
            with path.open('xb') as stream:
                stream.write(raw)
    return dict(config=config,format='s7-recipient-water-stage-v1',edition=edition,
        render=list(render),config_bytes=CONFIG_BYTES,recipe=water.RECIPE_ID,
        program_path=str(program_path),program_sha256=PROGRAM_SHA256,program_bytes=PROGRAM_BYTES,
        source_resources=dict(water.RESOURCES),private_case=private_case,
        active=False,runtime_verified=False,visible_quality_qualified=False,
        no_legacy_river_scaling_permitted=True,all_aliases_verified=True,
        full_archive_fingerprint_verified=False)
