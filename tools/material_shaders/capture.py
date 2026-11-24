"""Build an explicit read-only material input capture configuration (native ABI1).

This consumes private catalog/CTAB metadata only. It contains no shader programs
and enables no replacements or constant writes. Source interpretations are kept
separate from the compiler-verified stage/register binding.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

from . import inventory

HEADER=struct.Struct("<IIIIQ")
TARGET=struct.Struct("<5I")
REQUEST=struct.Struct("<3I")
MAX_TARGETS=8
MAX_REQUESTS=8
TARGET_BYTES=TARGET.size+MAX_REQUESTS*REQUEST.size
CONFIG_BYTES=HEADER.size+MAX_TARGETS*TARGET_BYTES
PRESETS=(
    (1,"RiverColor",(("vertex","g_RiverFogDepth",15,"depth-class fog distances"),
                       ("vertex","g_RiverReflectiveness",15,"depth-class reflection/sparkle strengths"),
                       ("pixel","g_PSSunSpecularColor",7,"sun specular color"))),
    (1,"OceanColor",(("pixel","g_OceanFogParam",15,"x: fog density; y: depth density; z: specular intensity; w: specular exponent"),
                       ("pixel","g_WaterFogColor",7,"water fog RGB"),
                       ("pixel","g_WaveChoppiness",3,"wave choppiness pair"))),
    (1,"OceanWater",(("pixel","g_OceanCamera",7,"camera position for phase correlation"),
                       ("pixel","g_OceanFog",15,"ocean fog vector; individual components not yet qualified"))),
    (2,"ParallaxLayerShader",(("vertex","g_Detail",7,"detail map size, mip count, tiling"),
                               ("pixel","PSConstant1",15,"specular exponent, high-pass factor, displacement, softening"),
                               ("pixel","PSConstant2",7,"ambient, diffuse, specular multipliers"))),
    (2,"BumpLayerShader",(("vertex","g_Detail",7,"detail map size, mip count, tiling"),
                           ("pixel","PSConstant1",15,"specular exponent, high-pass factor, displacement, softening"),
                           ("pixel","PSConstant2",7,"ambient, diffuse, specular multipliers"))),
    (3,"LeafCardShader",(("pixel","g_fDiffuseScale",1,"diffuse scale, not a backlighting control"),
                          ("pixel","g_PSSunDiffuseColor",7,"sun diffuse RGB"),
                          ("pixel","g_PSSkyColor",7,"sky RGB"))),
    (3,"FrondShader",(("pixel","g_fDiffuseScale",1,"diffuse scale, not a backlighting control"),
                       ("pixel","g_PSSunDiffuseColor",7,"sun diffuse RGB"),
                       ("pixel","g_PSSkyColor",7,"sky RGB"))),
    (3,"BranchShader",(("pixel","g_fDiffuseScale",1,"diffuse scale, not a backlighting control"),
                        ("pixel","g_PSSunDiffuseColor",7,"sun diffuse RGB"),
                        ("pixel","g_PSAmbientCube",15,"first ambient cube face only; remaining faces not captured"))),
)


def build(catalog: dict,bindings: dict,*,frame_stride=120,first_frame=0,sample_limit=8) -> tuple[bytes,dict]:
    if (isinstance(frame_stride,bool) or not isinstance(frame_stride,int) or not 1<=frame_stride<=36000
            or isinstance(first_frame,bool) or not isinstance(first_frame,int) or not 0<=first_frame<2**64
            or isinstance(sample_limit,bool) or not isinstance(sample_limit,int) or not 1<=sample_limit<=8):
        raise ValueError("Unsupported capture bounds")
    entries=catalog.get("entries",[])
    if not entries or len(entries)>512:
        raise ValueError("Invalid catalog metadata")
    by_id={row["id"]:row for row in entries}
    if set(by_id)!=set(range(1,len(entries)+1)):
        raise ValueError("Catalog IDs are not contiguous")
    reflected={}
    for row in bindings.get("entries",[]):
        key=(row["resource"],row["shader_record"],row["stage"])
        if key in reflected:raise ValueError("Duplicate reflection entry")
        reflected[key]=row
    targets=[]
    for family,record,requests in PRESETS:
        candidates={}
        for row in entries:
            if row["family"]==family and row["shader_record"]==record:
                stages=candidates.setdefault(row["resource_name"],{})
                if row["stage"] in stages:raise ValueError("Duplicate catalog stage")
                stages[row["stage"]]=row
        pairs={}
        for name,stages in sorted(candidates.items()):
            if set(stages)!={"vertex","pixel"}:
                raise ValueError("Missing exact shader pair")
            key=tuple(stages[stage]["compiled_sha256"] for stage in ("vertex","pixel"))
            pairs.setdefault(key,(name,stages))
        if len(pairs)!=1:
            raise ValueError("Preset requires one alias-equivalent VS/PS pair")
        name,stages=next(iter(pairs.values()))
        metadata={}
        for stage in ("vertex","pixel"):
            key=(name,record,stage)
            row=reflected.get(key)
            if not row or row["compiled_sha256"]!=stages[stage]["compiled_sha256"]:
                raise ValueError("CTAB reflection does not match the original compiled program")
            metadata[stage]={item["name"]:item for item in row["bindings"]}
        values=[]
        for stage,variable,mask,interpretation in requests:
            row=metadata[stage].get(variable)
            if not row or row["register_set"]!="float4" or row["register_count"]<1:
                raise ValueError("Preset constant is absent from this shader stage")
            index=row["register_index"]
            if not 0<=index<=(255 if stage=="vertex" else 223):raise ValueError("Constant register out of range")
            values.append({"stage":1 if stage=="vertex" else 2,"index":index,"component_mask":mask,
                           "name":variable,"interpretation":interpretation,"binding_source":"compiled_ctab",
                           "interpretation_source":"private_original_include_inspection"})
        samplers=[item for item in metadata["pixel"].values() if item["register_set"]=="sampler"]
        if any(not 0<=item["register_index"]<16 or item["register_count"]!=1 for item in samplers):
            raise ValueError("Unsupported sampler array or index")
        targets.append({"family":family,"shader_record":record,"vertex_id":stages["vertex"]["id"],
                        "pixel_id":stages["pixel"]["id"],"vertex_sha256":stages["vertex"]["compiled_sha256"],
                        "pixel_sha256":stages["pixel"]["compiled_sha256"],"constants":values,
                        "samplers":[{"index":item["register_index"],"name":item["name"]} for item in samplers],
                        "sample_limit":sample_limit})
    binary=HEADER.pack(CONFIG_BYTES,1,len(targets),frame_stride,first_frame)
    for target in targets:
        mask=0
        for item in target["samplers"]:mask|=1<<item["index"]
        binary+=TARGET.pack(target["vertex_id"],target["pixel_id"],sample_limit,len(target["constants"]),mask)
        for item in target["constants"]:binary+=REQUEST.pack(item["stage"],item["index"],item["component_mask"])
        binary+=bytes((MAX_REQUESTS-len(target["constants"]))*REQUEST.size)
    binary+=bytes((MAX_TARGETS-len(targets))*TARGET_BYTES)
    if len(binary)!=CONFIG_BYTES:raise AssertionError("Capture config ABI size mismatch")
    return binary,{"format":"s7-material-input-capture-config-v1","config_sha256":hashlib.sha256(binary).hexdigest(),
                   "config_bytes":len(binary),"catalog_sha256":catalog["catalog_sha256"],"targets":targets,
                   "frame_stride":frame_stride,"first_frame":first_frame,"read_only":True,
                   "replacement_enabled":False,"live_values_observed":False,
                   "foliage_backlighting_uniform_identified":False}


def write(binary: bytes,manifest: dict,directory: Path) -> None:
    target=directory.resolve()
    if not target.is_relative_to((inventory.ROOT/"work").resolve()):raise ValueError("Capture output must remain under work/")
    target.mkdir(parents=True,exist_ok=False)
    with (target/"capture.bin").open("xb") as stream:stream.write(binary)
    with (target/"capture.private.json").open("x",encoding="utf8",newline="\n") as stream:
        json.dump(manifest,stream,indent=2,sort_keys=True);stream.write("\n")


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog",type=Path,required=True)
    parser.add_argument("--bindings",type=Path,required=True)
    parser.add_argument("--output-directory",type=Path,required=True)
    parser.add_argument("--first-frame",type=int,default=0)
    parser.add_argument("--frame-stride",type=int,default=120)
    parser.add_argument("--sample-limit",type=int,default=8)
    args=parser.parse_args(argv)
    for path in (args.catalog,args.bindings):
        if path.stat().st_size>4*1024*1024:raise ValueError("Metadata input exceeds bound")
    binary,manifest=build(json.loads(args.catalog.read_text(encoding="utf8")),json.loads(args.bindings.read_text(encoding="utf8")),
                          first_frame=args.first_frame,frame_stride=args.frame_stride,sample_limit=args.sample_limit)
    write(binary,manifest,args.output_directory)
    print(json.dumps({"targets":len(manifest["targets"]),"config_sha256":manifest["config_sha256"],"read_only":True}))
    return 0


if __name__=="__main__":raise SystemExit(main())
