"""Authored exact-original terrain macro-relief recipe, explicitly selected.

Both original detail UVs are halved using an already-defined local 0.5.
Bump near-normal weights become12000..18000; Parallax keeps8000..12800.
No added constants, texture reads, ray steps, global colour or mip bias.
"""
import hashlib,struct
from . import bytecode,recipes,shaderlib,terrain_distance

DISTANCES={"BumpLayerShader":(12000.0,18000.0),"ParallaxLayerShader":(8000.0,12800.0)}
UV_OUTPUTS={"BumpLayerShader":6,"ParallaxLayerShader":7}
HALF_COMPONENT={"BumpLayerShader":2,"ParallaxLayerShader":0}

def transform(program,role):
    if role not in DISTANCES or hashlib.sha256(program).hexdigest()!=recipes.TERRAIN_PAIRS[role][0]:
        raise ValueError("Exact original terrain vertex identity required")
    rows=bytecode.decode(program,stage="vertex");target=UV_OUTPUTS[role]
    definitions={row.words[1]&2047:row for row in rows if row.opcode==81}
    half=definitions.get(3);component=HALF_COMPONENT[role]
    if half is None or len(half.words)!=6 or half.words[2+component]!=struct.unpack("<I",struct.pack("<f",.5))[0]:
        raise ValueError("Original local half literal absent")
    used_temps=set();writers=[]
    for row in rows:
        if row.opcode in (0xfffe,81,47,48):continue
        tokens=row.words[2:] if row.opcode==31 else row.words[1:]
        for token in tokens:
            if token&0x80000000 and bytecode.register_type(token)==0:used_temps.add(token&2047)
        if row.opcode in {20,21,22,23,24}:
            for token in row.words[2:]:
                if bytecode.register_type(token)==0:
                    used_temps.update(range(token&2047,(token&2047)+4))
        if row.opcode not in (31,) and len(row.words)>1:
            destination=row.words[1]
            if bytecode.register_type(destination)==6 and destination&2047==target and destination&0xc0000:
                writers.append(row)
    if len(writers)!=1:raise ValueError("Nonunique detail output writer")
    writer=writers[0]
    if writer.opcode!=5 or len(writer.words)!=4 or writer.words[0]&0x10000000 or (writer.words[1]>>16)&15!=12:
        raise ValueError("Unsupported exact detail writer")
    if not any(bytecode.register_type(t)==2 and t&2047==67 and ((t>>16)&255)==0xaa and not t&0x0f002000 for t in writer.words[2:]):
        raise ValueError("Detail writer lacks direct VS67.z")
    temporary=next((i for i in range(32) if i not in used_temps),None)
    if temporary is None:raise ValueError("No unused temporary")
    changed=bytearray(program)
    full,zero=DISTANCES[role]
    expected=(3200.0,1/1200)
    replacement=(zero,1/(zero-full))
    literal_offsets=[]
    for (register,index),old,new in zip(terrain_distance.LAYOUTS[role],expected,replacement):
        offset=definitions[register].offset+8+index*4
        if changed[offset:offset+4]!=struct.pack("<f",old):raise ValueError("Original fade literal absent")
        changed[offset:offset+4]=struct.pack("<f",new);literal_offsets.append(offset)
    original_destination=writer.words[1]
    edited=list(writer.words)
    edited[1]=(original_destination&~0x70001fff)|temporary
    scaled=struct.pack("<4I",*edited)
    swizzle=component*0x55
    scaled+=struct.pack("<4I",0x03000005,original_destination,0x80e40000|temporary,0xa0000003|(swizzle<<16))
    begin=writer.offset;end=begin+len(writer.words)*4
    result=bytes(changed[:begin])+scaled+bytes(changed[end:])
    bytecode.decode(result,stage="vertex")
    return result,{"role":role,"recipe":"terrain-macro-relief-v3-private","original_vs_sha256":recipes.TERRAIN_PAIRS[role][0],
        "original_pixel_sha256":recipes.TERRAIN_PAIRS[role][1],"original_vertex_sha256":recipes.TERRAIN_PAIRS[role][0],"generated_vertex_sha256":hashlib.sha256(result).hexdigest(),
        "generated_bytes":len(result),"UV_frequency_scale":.5,"expected_runtime_VS67":[512,9,8],
        "expected_runtime_region_size":[3200,3200],"near_full":full,"near_zero":zero,
        "local_half_register":3,"local_half_component":component,"temporary":temporary,
        "changed_literal_offsets":literal_offsets,"original_UV_writer_offset":begin,
        "added_ALU_instructions":1,"added_texture_fetches":0,"added_constants":0,
        "far_alpha_unchanged":True,"position_and_other_interpolants_unchanged":True,
        "negative_mip_bias":False,"native_runtime_guard_required":True,
        "production_staging_supported":False,"visible_gain_qualified":False}

def generate(name,resource,role):
    if name not in recipes.TERRAIN_RESOURCES or shaderlib.digest(resource)!=recipes.TERRAIN_RESOURCES[name]:
        raise ValueError("Exact original recipient resource required")
    pair={block.stage:block for block in shaderlib.parse(resource).blocks if block.shader==role}
    if set(pair)!={"vertex","pixel"} or tuple(shaderlib.digest(pair[s].code) for s in ("vertex","pixel"))!=recipes.TERRAIN_PAIRS.get(role):
        raise ValueError("Original pair mismatch")
    program,manifest=transform(pair["vertex"].code,role)
    return recipes.Generated(program,{**manifest,"format":"s7-recipient-material-program-v1",
        "recipe":"terrain-macro-relief-"+role+"-v3","source_resource":name,
        "source_resource_sha256":recipes.TERRAIN_RESOURCES[name],"maintainer":"WePlay Community",
        "recipe_version":3,"material_config_version":2,"active":False,
        "generated_from_recipient_owned_resource":True,"public_binary_distribution_permitted":False,
        "classify_original_pair_before_temporary_bind":True,"restore_original_shader_after_draw":True})
