"""Explicit future V2 recipient generation, separate from the V1 launcher path.

This helper does not supply qualification/capabilities, activate a runtime or
modify original resources. Every selected vendor alias, recipe, output path and
existing cache file is checked before any cache write. IO failure can leave
only immutable unreferenced artifacts; no configuration is returned on failure.
"""
from __future__ import annotations

import hashlib
import json
import mmap
from pathlib import Path
import struct

from . import inventory,leaf_lobe,recipes,shaderlib,staging,terrain_distance

CONFIG_BYTES=6180
CONFIG_VERSION=2
WORKER_BUDGET=8192
PUBLIC_MODULES_V2=staging.PUBLIC_MODULES+("leaf_lobe.py","terrain_distance.py","staging_v2.py")
WATER_RESOURCES={
    staging.WATER_RESOURCE:staging.WATER_RESOURCE_SHA256,
    "Graphics/Shaders/Water_ATIx_High.ShaderLib":"3a018f42a0de1a14338076beccedcb27395e345a02560610c1ed5fcf9ec6f0f0",
    "Graphics/Shaders/Water_NV_High.ShaderLib":"2173334cdbe9223fb412cf93e96b3178176039c11421b87dec8417da08e10788",
}
# Explicit versioned recipe identities and generated-program pins. Original
# resource/VS/PS pins remain mandatory in the authored generators themselves.
PROGRAMS={
    "leaf":("leaf-angular-lobe-v2","vertex",3828,"119352b9def2ba290c8a5bc99b18b77cf4730461e97d0fbf677fba104e1e472a"),
    "bump_vertex":("terrain-near-distance-BumpLayerShader-v2","vertex",2004,"46a8dd89646f84cb761f832f47739f3d0c4fe5494688b3104b56c43fac8d3f07"),
    "parallax_vertex":("terrain-near-distance-ParallaxLayerShader-v2","vertex",2004,"d985366573816225b04473b762fd6f899241849185274347f513c1f4cf7de6c6"),
    "bump_pixel":("terrain-normal-BumpLayerShader-v2","pixel",1836,"c2ae22a6739e071774edd071870e6dc8fd058b5bf6eec6490bc1a4603fea2fa3"),
    "parallax_pixel":("terrain-normal-ParallaxLayerShader-v2","pixel",3596,"d0d082bbb9b8d2de1388b790165bcebc5fda571d88895db3e8b36004bf2e52fa"),
}
PATH_ORDER=("leaf","bump_pixel","parallax_pixel","report","bump_vertex","parallax_vertex")


def _path(value: str,ending: str) -> bytes:
    encoded=staging._wide(value)
    normal=value.replace("/","\\")
    drive=len(normal)>3 and normal[0].isascii() and normal[0].isalpha() and normal[1:3]==":\\"
    unc=normal.startswith("\\\\") and not normal.startswith(("\\\\?","\\\\."))
    if not drive and not unc:raise ValueError("V2 paths must be canonical absolute Windows paths")
    parts=normal[3 if drive else 2:].split("\\")
    if (unc and len(parts)<3) or any(not part or part in (".","..") or part.endswith(("."," "))
        or any(ord(c)<32 or c in '\"<>|?*:' for c in part) for part in parts):
        raise ValueError("Invalid V2 native path shape")
    for part in parts:
        if len(part.encode('utf-16-le'))//2>255:raise ValueError("V2 path component exceeds portable Windows bound")
        base=part.split('.',1)[0].upper()
        if base in ('CON','PRN','AUX','NUL','CLOCK$') or len(base)==4 and base[:3]in ('COM','LPT') and base[3]in '123456789\u00b9\u00b2\u00b3':
            raise ValueError("V2 path contains a reserved Windows device name")
    if not value.lower().endswith(ending) or ending==".private.json" and len(value.encode('utf-16-le'))//2>=508:
        raise ValueError("Invalid V2 native path suffix or report bound")
    return encoded


def _river(name,data):
    if WATER_RESOURCES.get(name)!=shaderlib.digest(data):raise ValueError("Unsupported V2 recipient water resource")
    library=shaderlib.parse(data);blocks={b.stage:b for b in library.blocks if b.shader=="RiverColor"}
    if set(blocks)!={"vertex","pixel"} or shaderlib.digest(blocks["vertex"].code)!=staging.RIVER_VERTEX or shaderlib.digest(blocks["pixel"].code)!=staging.RIVER_PIXEL:
        raise ValueError("Unsupported V2 original river pair")
    return {"source_resource":name,"source_resource_sha256":library.resource_sha256,
            "original_vertex_sha256":staging.RIVER_VERTEX,"original_pixel_sha256":staging.RIVER_PIXEL,"scope":"river"}


def _versioned(role,item,*,programs=None):
    catalog=PROGRAMS if programs is None else programs
    recipe,stage,size,digest=catalog[role];key="generated_"+stage+"_sha256"
    if (not isinstance(item,recipes.Generated) or len(item.program)!=size or shaderlib.digest(item.program)!=digest
            or item.manifest.get(key)!=digest or item.manifest.get("active")is not False):
        raise ValueError("V2 generated program identity mismatch")
    source=item.manifest.get("source_resource");source_hash=item.manifest.get("source_resource_sha256")
    resources=recipes.RESOURCES if role=="leaf" else recipes.TERRAIN_RESOURCES
    if source not in resources or resources[source]!=source_hash:raise ValueError("V2 original resource identity mismatch")
    pair=(recipes.ORIGINAL_VERTEX,recipes.ORIGINAL_PIXEL) if role=="leaf" else recipes.TERRAIN_PAIRS["ParallaxLayerShader"if role.startswith("parallax")else"BumpLayerShader"]
    if (item.manifest.get("original_vertex_sha256"),item.manifest.get("original_pixel_sha256"))!=pair:
        raise ValueError("V2 original shader pair mismatch")
    return recipes.Generated(item.program,{**item.manifest,"recipe":recipe,"material_config_version":2,
        "recipe_version":3 if recipe.endswith("-v3") else 2,"active":False,"runtime_qualified":False,"production_runtime_enabled":False})


def _plan(cache_root,report_root,role,item,*,programs=None):
    recipe,stage,_,digest=(PROGRAMS if programs is None else programs)[role]
    target=staging.plain(cache_root/"LocalData/MaterialShaders"/recipe)
    if not target.is_relative_to(report_root):raise ValueError("V2 cache escapes trusted native report root")
    suffix=".vs.bin"if stage=="vertex"else".ps.bin"
    program=staging.plain(target/(digest+suffix))
    source_digest=shaderlib.checked_digest(item.manifest["source_resource_sha256"])
    receipt=staging.plain(target/(digest+"-"+source_digest+".json"))
    _path(str(program),suffix)
    _path(str(receipt),".json")
    encoded=(json.dumps(item.manifest,indent=2,sort_keys=True)+"\n").encode("utf8")
    return {"role":role,"program":program,"receipt":receipt,"bytes":item.program,"receipt_bytes":encoded,
        "artifact":{"program_relative_path":program.relative_to(cache_root).as_posix(),
                    "receipt_relative_path":receipt.relative_to(cache_root).as_posix(),
                    "generated_program_sha256":digest,"generated_"+stage+"_sha256":digest,
                    "active":False,"runtime_binding_verified":False}}


def _existing(path,data):
    path=staging.plain(path)
    if path.exists() and (not path.is_file() or path.stat().st_size!=len(data) or path.read_bytes()!=data):
        raise ValueError("Existing V2 immutable cache identity mismatch")


def _prepare(recipient_root,selection,display,*,config_version,available_config_versions,worker_overhead_bytes,
            available_features,available_scales,native_report,verified_platform=None,private_case=False,
            programs=None,terrain_vertex_generator=None,terrain_scales=(1.20,1.10),recipe_variant=2):
    catalog=PROGRAMS if programs is None else programs
    vertex_generator=terrain_distance.generate if terrain_vertex_generator is None else terrain_vertex_generator
    settings=staging._settings();selected=settings.validate(selection)
    settings.require_features(selected,available_features)
    flags=sum(bit for bit,key in ((1,"improved_water"),(2,"improved_terrain"),(4,"improved_vegetation"))if selected[key])
    if not flags:return None
    if type(config_version)is not int or config_version!=CONFIG_VERSION:
        raise ValueError("Explicit material config version 2 required; no V1 upgrade")
    if (not isinstance(available_config_versions,(tuple,list,set,frozenset))
            or any(type(v)is not int or v not in (1,2) for v in available_config_versions) or 2 not in available_config_versions):
        raise ValueError("native_material_config_v2_unavailable")
    # Includes every non-config byte of the actual worker invocation, measured
    # by the trusted host: DLL/renderer/export/header/padding, not a saved option.
    if type(worker_overhead_bytes)is not int or not 0<worker_overhead_bytes<=WORKER_BUDGET-CONFIG_BYTES:
        raise ValueError("V2 complete worker payload exceeds budget")
    if (not isinstance(available_scales,(tuple,list,set,frozenset))
            or any(type(v)is not int or v not in settings.SCALES for v in available_scales)
            or selected["render_scale"]not in available_scales):raise ValueError("native_graphics_unavailable")
    if type(private_case)is not bool:raise ValueError("Invalid V2 private case mode")
    dimensions=settings.render_size(selected,display)
    edition,archive_name=staging._layout(verified_platform)
    root=staging.plain(recipient_root);source=staging.plain(root/archive_name)
    if not root.is_dir() or not source.is_relative_to(root) or not source.is_file():raise ValueError("Recipient original graphics archive is absent")
    if not Path(native_report).is_absolute():raise ValueError("V2 native report must be absolute")
    report=staging.plain(native_report);local=staging.plain(root/"LocalData")
    if report.suffix.lower()!=".json":raise ValueError("Invalid V2 native report location")
    if private_case:
        work=staging.plain(inventory.ROOT/"work")
        if report.parent==work or not report.parent.is_relative_to(work) or not report.parent.is_dir() or report.parent.is_relative_to(root/Path(archive_name).parts[0]):
            raise ValueError("V2 private output must stay in a separate work case")
        cache_root=report.parent
    else:
        if report.parent!=local:raise ValueError("V2 production report must be directly under recipient LocalData")
        cache_root=root
    effects_report=report.parent/"material-effects.private.json"
    if effects_report==report:raise ValueError("V2 native/effects report collision")
    _path(str(report),".json")
    _path(str(effects_report),".private.json")
    for target in (report,effects_report):
        if target.exists() and not target.is_file():raise ValueError("V2 report target must be a regular file or absent")
    generated=[];source_receipts=[]
    with source.open("rb") as stream,mmap.mmap(stream.fileno(),0,access=mmap.ACCESS_READ) as raw:
        if len(raw)<32 or raw[:4]!=b"BAF\x06":raise ValueError("Unsupported V2 recipient archive")
        header_size=struct.unpack_from("<I",raw,8)[0]
        if header_size not in (16,64) or len(raw)<16+header_size:raise ValueError("Invalid V2 recipient archive header")
        header=inventory.bba.crypt(raw[16:16+header_size],inventory.bba.HEAD_KEYS,1231853475,7)
        if struct.unpack_from("<I",header,8)[0]>32*1024*1024:raise ValueError("V2 source directory exceeds bound")
        archive=inventory.bba.Archive(raw,copy=False)
        directory_hash=hashlib.sha256(archive.header+archive.directory).hexdigest()
        if flags&1:
            for name in WATER_RESOURCES:source_receipts.append(_river(name,inventory.read_payload(archive,name)))
        if flags&2:
            for name in recipes.TERRAIN_RESOURCES:
                data=inventory.read_payload(archive,name)
                for record,prefix in (("BumpLayerShader","bump"),("ParallaxLayerShader","parallax")):
                    generated.append((prefix+"_vertex",_versioned(prefix+"_vertex",vertex_generator(name,data,record),programs=catalog)))
                    generated.append((prefix+"_pixel",_versioned(prefix+"_pixel",recipes.generate_terrain(name,data,record),programs=catalog)))
        if flags&4:
            for name in recipes.RESOURCES:
                generated.append(("leaf",_versioned("leaf",leaf_lobe.generate(name,inventory.read_payload(archive,name)),programs=catalog)))
        reread=inventory.bba.Archive(raw,copy=False)
        if hashlib.sha256(reread.header+reread.directory).hexdigest()!=directory_hash:raise ValueError("V2 source directory changed during generation")
    plans=[_plan(cache_root,report.parent,role,item,programs=catalog) for role,item in generated]
    paths={plan["role"]:plan["program"] for plan in plans};paths["report"]=effects_report
    config=struct.pack("<5I4f",CONFIG_BYTES,2,flags,dimensions["render_width"],dimensions["render_height"],1.15,.94,*terrain_scales)
    for role in PATH_ORDER:config+=_path(str(paths[role]),".private.json"if role=="report"else".vs.bin"if role in ("leaf","bump_vertex","parallax_vertex")else".ps.bin")if role in paths else bytes(1024)
    if len(config)!=CONFIG_BYTES or len(config)+worker_overhead_bytes>WORKER_BUDGET:raise AssertionError("V2 transport shape mismatch")
    pending={};artifacts={}
    for plan in plans:
        for path,data in ((plan["program"],plan["bytes"]),(plan["receipt"],plan["receipt_bytes"])):
            if path in pending and pending[path]!=data:raise ValueError("Conflicting V2 vendor aliases")
            pending[path]=data
        role=plan["role"]
        if role not in artifacts:artifacts[role]={**plan["artifact"],"source_receipt_paths":[]}
        artifacts[role]["source_receipt_paths"].append(plan["artifact"]["receipt_relative_path"])
    # Check every existing target before creating directories or any new file.
    for path,data in pending.items():_existing(path,data)
    report.parent.mkdir(parents=True,exist_ok=True)
    for path,data in pending.items():
        path.parent.mkdir(parents=True,exist_ok=True);staging.plain(path)
        if path.exists():_existing(path,data)
        else:
            with path.open("xb") as stream:stream.write(data)
    source_receipts.extend({key:item.manifest[key]for key in ("source_resource","source_resource_sha256","original_vertex_sha256","original_pixel_sha256","recipe")}for _,item in generated)
    result={"format":"s7-recipient-material-stage-v2","config_version":2,"config":config,"material_flags":flags,
        "worker_overhead_bytes":worker_overhead_bytes,"worker_payload_bytes":len(config)+worker_overhead_bytes,
        "render_scale":selected["render_scale"],"dimensions":dimensions,"artifact_root":str(cache_root),"artifacts":artifacts,
        "source_receipts":source_receipts,"source_directory_sha256":directory_hash,"full_archive_fingerprint_verified":False,
        "platform_edition":edition,"source_archive_relative_path":archive_name,"all_vendor_aliases_checked":True,
        "required_shader_quality":"high","water_scope":"river"if flags&1 else None,"ocean_enabled":False,
        "active":False,"runtime_qualified":False,"numeric_policy_qualified":False,"production_runtime_enabled":False,
        "all_sources_generated_before_cache_writes":True,"all_paths_and_existing_cache_checked_before_writes":True}
    if recipe_variant!=2:result["terrain_recipe_variant"]=recipe_variant
    return result


def prepare(recipient_root,selection,display,*,config_version,available_config_versions,worker_overhead_bytes,
            available_features,available_scales,native_report,verified_platform=None,private_case=False):
    """Retained V2 default recipe and serialized pins; no implicit macro upgrade."""
    return _prepare(recipient_root,selection,display,config_version=config_version,
        available_config_versions=available_config_versions,worker_overhead_bytes=worker_overhead_bytes,
        available_features=available_features,available_scales=available_scales,native_report=native_report,
        verified_platform=verified_platform,private_case=private_case)
