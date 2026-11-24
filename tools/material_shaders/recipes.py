"""Recipient-generated material recipes. No original/derived game code bundled.

The installer supplies the recipient's independently verified original resource
bytes. Only this authored transform and compatibility hashes belong in a public
package. Generated programs stay in that recipient's LocalData content cache.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path

from . import foliage,shaderlib,terrain

RECIPE_ID="leaf-angular-backlight-v1"
ORIGINAL_VERTEX="15701e004a296b3137122207b4557c71700c20aa23eda1b01482c45223ece0df"
ORIGINAL_PIXEL="2e633d1cd30a2db140fc338938864d909141f6cb9fd702e40ab6b89b141d90be"
RESOURCES={
    "Graphics/Shaders/SpeedTree_ATI_High.ShaderLib":"068e99267c04a3bb42f010fa6ead00a8e36c77270b5a93d18c16cfdec12e74c2",
    "Graphics/Shaders/SpeedTree_ATIx_High.ShaderLib":"108dd48d5b4b8de72873ac4d24c32332e79e306a022b1d001025f06095fd072e",
    "Graphics/Shaders/SpeedTree_NV_High.ShaderLib":"4a6c26629a3365de40cbf0b7c7a1e78578ec3bc918c652396b2f55d68475a3e1",
}
TERRAIN_RESOURCES={
    "Graphics/Shaders/Terrain_ATI_High.ShaderLib":"786b2eccd23315563436af9bab83032e5a414057314aebc0b7ea525b0487900c",
    "Graphics/Shaders/Terrain_ATIx_High.ShaderLib":"ad4c71a88d99138c1a16f7fbd12e522235b9b3d53b88ff175f0e49ecf574f703",
    "Graphics/Shaders/Terrain_NV_High.ShaderLib":"b2bb042476623cb4c6a80b9ca367af4e85b3be3381414217ac3bac405efa2ceb",
}
TERRAIN_PAIRS={
    "BumpLayerShader":("6daa00fadd5f62f1fff27c3221154af9b83ed36225496ab981e9b624bf73a0cb",
                       "811ea82dae81585cd9f795efd2c10918cd78cbf53d89fe62be0475b6bacf5f18"),
    "ParallaxLayerShader":("41abe0979ae889b7f4e8cc51eea3fea5e7355649e1c2aca5fafb1e0e89a20daa",
                           "e776a4441563c4a3fe073811d694246c7245262244b77c614fddfe4f43144191"),
}


@dataclass(frozen=True)
class Generated:
    program: bytes
    manifest: dict


def generate_leaf(resource_name: str,original_resource: bytes,*,factor=1.25) -> Generated:
    expected=RESOURCES.get(resource_name)
    if expected is None or shaderlib.digest(original_resource)!=expected:
        raise ValueError("Unsupported original leaf shader resource identity")
    library=shaderlib.parse(original_resource)
    blocks={block.stage:block for block in library.blocks if block.shader=="LeafCardShader"}
    if set(blocks)!={"vertex","pixel"} or shaderlib.digest(blocks["vertex"].code)!=ORIGINAL_VERTEX or shaderlib.digest(blocks["pixel"].code)!=ORIGINAL_PIXEL:
        raise ValueError("Original LeafCard VS/PS pair identity mismatch")
    program,details=foliage.extend_leaf_backlight(blocks["vertex"].code,factor=factor)
    manifest={"format":"s7-recipient-material-program-v1","recipe":RECIPE_ID,"maintainer":"WePlay Community",
              "source_resource":resource_name,"source_resource_sha256":expected,
              "original_vertex_sha256":ORIGINAL_VERTEX,"original_pixel_sha256":ORIGINAL_PIXEL,
              "generated_vertex_sha256":shaderlib.digest(program),"generated_bytes":len(program),
              "generated_from_recipient_owned_resource":True,"public_binary_distribution_permitted":False,
              "classify_original_pair_before_temporary_bind":True,"restore_original_shader_after_draw":True,
              "active":False,"runtime_binding_verified":False,**details}
    return Generated(program,manifest)


def generate_terrain(resource_name: str,original_resource: bytes,shader_record: str,*,strength=1.10) -> Generated:
    expected=TERRAIN_RESOURCES.get(resource_name);pair=TERRAIN_PAIRS.get(shader_record)
    if expected is None or pair is None or shaderlib.digest(original_resource)!=expected:
        raise ValueError("Unsupported original terrain shader resource identity")
    library=shaderlib.parse(original_resource)
    blocks={block.stage:block for block in library.blocks if block.shader==shader_record}
    if set(blocks)!={"vertex","pixel"} or shaderlib.digest(blocks["vertex"].code)!=pair[0] or shaderlib.digest(blocks["pixel"].code)!=pair[1]:
        raise ValueError("Original terrain VS/PS pair identity mismatch")
    program,details=terrain.extend_normal_detail(blocks["pixel"].code,strength=strength)
    manifest={"format":"s7-recipient-material-program-v1","recipe":"terrain-normal-"+shader_record+"-v1",
              "maintainer":"WePlay Community","source_resource":resource_name,"source_resource_sha256":expected,
              "original_vertex_sha256":pair[0],"original_pixel_sha256":pair[1],
              "generated_pixel_sha256":shaderlib.digest(program),"generated_bytes":len(program),
              "generated_from_recipient_owned_resource":True,"public_binary_distribution_permitted":False,
              "classify_original_pair_before_temporary_bind":True,"restore_original_shader_after_draw":True,
              "active":False,"runtime_binding_verified":False,**details}
    return Generated(program,manifest)


def write_local_cache(recipient_root: Path,generated: Generated) -> dict:
    """Content-addressed recipient output only; never edit source game resources.

    The caller owns package/installer authorization and original content checks.
    Existing program files are verified, never overwritten (including files a
    running process might hold). A new effect/factor yields another filename.
    """
    root=recipient_root.resolve()
    if not root.is_dir():raise ValueError("Recipient root must already exist")
    local=(root/"LocalData").resolve()
    if not local.is_relative_to(root):raise ValueError("LocalData escapes recipient root")
    manifest=generated.manifest;recipe=manifest.get("recipe")
    terrain_ids={"terrain-normal-"+name+"-v1" for name in TERRAIN_PAIRS}
    if recipe==RECIPE_ID:resources=RESOURCES;key="generated_vertex_sha256";suffix=".vs.bin"
    elif recipe in terrain_ids:resources=TERRAIN_RESOURCES;key="generated_pixel_sha256";suffix=".ps.bin"
    else:raise ValueError("Unknown authored recipe")
    target=(local/"MaterialShaders"/recipe).resolve()
    if not target.is_relative_to(local):raise ValueError("Material cache escapes LocalData")
    digest=hashlib.sha256(generated.program).hexdigest()
    source_digest=shaderlib.checked_digest(manifest.get("source_resource_sha256"))
    if (manifest.get(key)!=digest
            or manifest.get("generated_bytes")!=len(generated.program)
            or resources.get(manifest.get("source_resource"))!=source_digest
            or manifest.get("active") is not False):
        raise ValueError("Generated program/manifest identity mismatch")
    target.mkdir(parents=True,exist_ok=True)
    path=target/(digest+suffix)
    if path.exists():
        if not path.is_file() or path.is_symlink() or path.stat().st_size!=len(generated.program) or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:
            raise ValueError("Existing generated program identity mismatch")
    else:
        with path.open("xb") as stream:stream.write(generated.program)
    # Receipt identity is per source resource even when vendor aliases generate
    # the same program. This avoids replacing a receipt for another input.
    encoded=(json.dumps(manifest,indent=2,sort_keys=True)+"\n").encode("utf8")
    receipt_name=digest+"-"+source_digest+".json"
    receipt=target/receipt_name
    if receipt.exists():
        if not receipt.is_file() or receipt.is_symlink() or receipt.read_bytes()!=encoded:
            raise ValueError("Existing generated receipt identity mismatch")
    else:
        with receipt.open("xb") as stream:stream.write(encoded)
    return {"program_relative_path":path.relative_to(root).as_posix(),
            "receipt_relative_path":receipt.relative_to(root).as_posix(),
            key:digest,"generated_program_sha256":digest,"active":False,"runtime_binding_verified":False}
