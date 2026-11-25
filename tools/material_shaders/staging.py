"""Recipient-owned material preparation for a trusted launcher capability gate.

Capabilities come from the host's verified payload/qualification, never saved
user preferences. This helper does not certify those capabilities, launch a
process, modify game archives or activate an effect. Only immutable LocalData
artifacts are created after every requested source recipe has been validated.
"""
from __future__ import annotations

import hashlib
import mmap
from pathlib import Path
import struct

from . import inventory,recipes,shaderlib

PUBLIC_MODULES=("__init__.py","bytecode.py","foliage.py","inventory.py","recipes.py",
                "semantics.py","shaderlib.py","terrain.py","staging.py",
                "water.py","water_balanced.py","water_staging.py","water_variants.py")
MATERIAL_KEYS=("improved_water","improved_terrain","improved_vegetation")
WATER_RESOURCE="Graphics/Shaders/Water_ATI_High.ShaderLib"
WATER_RESOURCE_SHA256="f5c575f9015fefd2f33e4a7fdce00bcadc68b54590844b3c08d1c4e48df1efe2"
RIVER_VERTEX="f190d66ee1ae43637341a76e5b644cc63bf3c2a5b5c94ea9286764e13f1c16c0"
RIVER_PIXEL="dd65ba0b9d79c7c4d9ad9904404388230556b194eedb8cbdfbd4fa99f44eee5b"
CONFIG_BYTES=4132
LAYOUTS={"history":"Base/BBA/gfx.bba","gold":"Data/Base/bba/gfx.bba"}


def _settings():
    try:
        import native_graphics_settings
    except ImportError:
        from tools.installer import native_graphics_settings
    return native_graphics_settings


def plain(path: Path) -> Path:
    value=Path(path).absolute()
    for part in (value,*value.parents):
        if part.is_symlink() or (hasattr(part,"is_junction") and part.is_junction()):
            raise ValueError("Material staging does not follow links or junctions")
    return value.resolve()


def _wide(value: str) -> bytes:
    if not isinstance(value,str) or "\0" in value:
        raise ValueError("Invalid native material path")
    encoded=value.encode("utf-16-le")
    if len(encoded)>1022:
        raise ValueError("Native material path exceeds transport bound")
    return encoded+bytes(1024-len(encoded))


def _river(data: bytes) -> dict:
    if shaderlib.digest(data)!=WATER_RESOURCE_SHA256:
        raise ValueError("Unsupported recipient water resource")
    library=shaderlib.parse(data)
    blocks={item.stage:item for item in library.blocks if item.shader=="RiverColor"}
    if set(blocks)!={"vertex","pixel"} or shaderlib.digest(blocks["vertex"].code)!=RIVER_VERTEX or shaderlib.digest(blocks["pixel"].code)!=RIVER_PIXEL:
        raise ValueError("Unsupported recipient river shader pair")
    return {"resource":WATER_RESOURCE,"resource_sha256":library.resource_sha256,
            "original_vertex_sha256":RIVER_VERTEX,"original_pixel_sha256":RIVER_PIXEL,"scope":"river"}


def _layout(verified_platform: dict) -> tuple[str,str]:
    """Consume platform_standalone.load(root)[0], already verified by the host.

    This is a strict edition-to-layout selector, not manifest authentication.
    Never infer Gold from another path's existence or accept arbitrary paths.
    """
    if (not isinstance(verified_platform,dict) or set(verified_platform)!={"format","edition","files"}
            or verified_platform["format"]!="weplay-platform-independent-v1"
            or verified_platform["edition"]not in LAYOUTS or not isinstance(verified_platform["files"],dict)):
        raise ValueError("Verified recipient platform layout is required")
    edition=verified_platform["edition"]
    return edition,LAYOUTS[edition]


def prepare(recipient_root: Path,selection: dict,display: dict,*,available_features,available_scales,
            native_report: Path,verified_platform=None,private_case=False) -> dict | None:
    settings=_settings();selected=settings.validate(selection)
    settings.require_features(selected,available_features)
    if not settings.requested_features(selected):return None
    if type(private_case)is not bool:raise ValueError("Invalid private case mode")
    if (not isinstance(available_scales,(tuple,list,set,frozenset))
            or any(type(item)is not int or item not in settings.SCALES for item in available_scales)
            or selected["render_scale"] not in available_scales):
        raise ValueError("native_graphics_unavailable")
    dimensions=settings.render_size(selected,display)
    flags=sum(bit for bit,key in ((1,"improved_water"),(2,"improved_terrain"),(4,"improved_vegetation"))if selected[key])
    if not flags:return None
    edition,archive_name=_layout(verified_platform)
    root=plain(recipient_root)
    source=plain(root/archive_name)
    if not source.is_relative_to(root) or not source.is_file():
        raise ValueError("Recipient original graphics archive is absent")
    report=plain(native_report)
    if not report.is_absolute() or report.suffix.lower()!=".json":
        raise ValueError("Invalid trusted native report location")
    local=plain(root/"LocalData")
    if private_case:
        work=plain(inventory.ROOT/"work")
        if report.parent==work or not report.parent.is_relative_to(work) or not report.parent.is_dir():
            raise ValueError("Private graphics case must remain under work/")
        if report.parent.is_relative_to(root/Path(archive_name).parts[0]):
            raise ValueError("Private material outputs must not enter source game data")
        cache_root=report.parent
    else:
        if report.parent!=local:
            raise ValueError("Production native report must be directly under recipient LocalData/")
        cache_root=root
    effects_report=report.parent/"material-effects.private.json"
    if effects_report==report:
        raise ValueError("Native/effects report collision")
    generated=[];source_receipts=[]
    # Borrow only the explicitly recipient-owned archive. All resource/pair
    # identities and recipe transforms complete before any output is written.
    with source.open("rb") as stream,mmap.mmap(stream.fileno(),0,access=mmap.ACCESS_READ) as raw:
        if len(raw)<32 or raw[:4]!=b"BAF\x06":raise ValueError("Unsupported recipient archive")
        header_size=struct.unpack_from("<I",raw,8)[0]
        if header_size not in (16,64) or len(raw)<16+header_size:raise ValueError("Invalid recipient archive header")
        header=inventory.bba.crypt(raw[16:16+header_size],inventory.bba.HEAD_KEYS,1231853475,7)
        if struct.unpack_from("<I",header,8)[0]>32*1024*1024:raise ValueError("Recipient directory exceeds bound")
        archive=inventory.bba.Archive(raw,copy=False)
        directory_hash=hashlib.sha256(archive.header+archive.directory).hexdigest()
        if flags&1:source_receipts.append(_river(inventory.read_payload(archive,WATER_RESOURCE)))
        if flags&2:
            name="Graphics/Shaders/Terrain_ATI_High.ShaderLib";data=inventory.read_payload(archive,name)
            for record in ("BumpLayerShader","ParallaxLayerShader"):
                item=recipes.generate_terrain(name,data,record);generated.append((record,item))
        if flags&4:
            name="Graphics/Shaders/SpeedTree_ATI_High.ShaderLib"
            generated.append(("leaf",recipes.generate_leaf(name,inventory.read_payload(archive,name))))
        reread=inventory.bba.Archive(raw,copy=False)
        if hashlib.sha256(reread.header+reread.directory).hexdigest()!=directory_hash:
            raise ValueError("Recipient source directory changed during generation")
    # No source game file is ever written. Cache failures leave only immutable,
    # unreferenced LocalData files and return no configuration/activation.
    report.parent.mkdir(parents=True,exist_ok=True)
    artifacts={}
    for role,item in generated:
        artifacts[role]=recipes.write_local_cache(cache_root,item)
        source_receipts.append({key:item.manifest[key]for key in ("source_resource","source_resource_sha256","original_vertex_sha256","original_pixel_sha256")})
    absolute={role:plain(cache_root/item["program_relative_path"]) for role,item in artifacts.items()}
    if any(not path.is_relative_to(report.parent) for path in absolute.values()):
        raise ValueError("Generated artifact violates trusted effects host root")
    config=struct.pack("<5I4f",CONFIG_BYTES,1,flags,dimensions["render_width"],dimensions["render_height"],1.15,.94,1.20,1.10)
    for role in ("leaf","BumpLayerShader","ParallaxLayerShader"):
        config+=_wide(str(absolute[role])) if role in absolute else _wide("")
    config+=_wide(str(effects_report))
    if len(config)!=CONFIG_BYTES:raise AssertionError("Native material ABI mismatch")
    return {"format":"s7-recipient-material-stage-v1","config":config,"material_flags":flags,"render_scale":selected["render_scale"],"dimensions":dimensions,
            "artifact_root":str(cache_root),"artifacts":artifacts,"source_receipts":source_receipts,
            "platform_edition":edition,"source_archive_relative_path":archive_name,
            "required_shader_quality":"high","water_scope":"river"if flags&1 else None,
            "ocean_enabled":False,"active":False,"runtime_qualified":False,
            "numeric_policy_qualified":False,
            "all_sources_generated_before_cache_writes":True}
