"""Generate a private scalar-only original-program catalog for the D3D9 observer."""
from __future__ import annotations

import argparse
import hashlib
import json
import mmap
from pathlib import Path
import struct

from . import inventory, shaderlib

MAGIC=b"S7MSCAT1"
HEADER=struct.Struct("<8sII")
ENTRY=struct.Struct("<IIII32s")
FAMILIES={"water":1,"terrain":2,"vegetation":3}
MAX_ENTRIES=512
DEFAULT_HIGH=tuple((family, f"Graphics/Shaders/{base}_{vendor}_High.ShaderLib")
                   for family,base in (("water","Water"),("terrain","Terrain"),("vegetation","SpeedTree"))
                   for vendor in ("ATI","ATIx","NV"))


def build(resources: list[tuple[str,str,bytes]]) -> tuple[bytes,dict]:
    if not resources or len(resources)>32:
        raise ValueError("Select one to 32 explicit shader libraries")
    rows=[]
    identities=[]
    names=set()
    for family,name,data in sorted(resources,key=lambda item:(item[0],item[1])):
        if family not in FAMILIES or name in names:
            raise ValueError("Unknown family or duplicate resource")
        inventory.safe_name(name)
        names.add(name)
        library=shaderlib.parse(data)
        identities.append({"resource_name":name,"resource_sha256":library.resource_sha256,
                           "family":family,"shader_records":len(library.records)})
        for block in library.blocks:
            if len(rows)>=MAX_ENTRIES:
                raise ValueError("Observer catalog limit exceeded")
            rows.append({"id":len(rows)+1,"family":FAMILIES[family],"resource_name":name,
                         **block.metadata()})
    binary=HEADER.pack(MAGIC,1,len(rows))
    for row in rows:
        binary+=ENTRY.pack(row["id"],row["family"],1 if row["stage"]=="vertex" else 2,
                           row["compiled_bytes"],bytes.fromhex(row["compiled_sha256"]))
    aliases={}
    for row in rows:
        key=(row["stage"],row["compiled_bytes"],row["compiled_sha256"])
        aliases.setdefault(key,[]).append(row["id"])
    return binary,{"format":"s7-material-observer-catalog-v1","catalog_sha256":hashlib.sha256(binary).hexdigest(),
                   "catalog_bytes":len(binary),"entry_count":len(rows),"resources":identities,"entries":rows,
                   "alias_groups":[values for values in aliases.values() if len(values)>1],
                   "contains_program_bytes":False,"runtime_binding_verified":False,"replacement_enabled":False}


def from_archive(path: Path, expected_directory: str, resources=DEFAULT_HIGH) -> tuple[bytes,dict]:
    if not inventory.SHA256.fullmatch(expected_directory):
        raise ValueError("Expected directory SHA256")
    # Reuse the bounded checked directory/payload reader, then re-open only the
    # explicitly selected resources for decoding. No game directory traversal.
    baseline=inventory.inspect_archive(path,None,[name for _,name in resources],expected_directory=expected_directory)
    values=[]
    with path.open("rb") as stream, mmap.mmap(stream.fileno(),0,access=mmap.ACCESS_READ) as raw:
        archive=inventory.bba.Archive(raw,copy=False)
        identity=hashlib.sha256(archive.header+archive.directory).hexdigest()
        if identity!=expected_directory:
            raise ValueError("Archive directory changed")
        expected={item["name"]:item["sha256"] for item in baseline["inspected"]}
        for family,name in resources:
            data=inventory.read_payload(archive,name)
            if hashlib.sha256(data).hexdigest()!=expected[name]:
                raise ValueError("Selected resource changed")
            values.append((family,name,data))
    binary,manifest=build(values)
    manifest.update(input_directory_sha256=expected_directory,full_archive_fingerprint_verified=False)
    return binary,manifest


def write(binary: bytes,manifest: dict,directory: Path) -> None:
    target=directory.resolve()
    if not target.is_relative_to((inventory.ROOT/"work").resolve()):
        raise ValueError("Catalog output must remain under work/")
    target.mkdir(parents=True,exist_ok=False)
    with (target/"catalog.bin").open("xb") as stream:
        stream.write(binary)
    with (target/"catalog.private.json").open("x",encoding="utf8",newline="\n") as stream:
        json.dump(manifest,stream,indent=2,sort_keys=True)
        stream.write("\n")


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive",type=Path,required=True)
    parser.add_argument("--expect-directory-sha256",required=True)
    parser.add_argument("--output-directory",type=Path,required=True)
    args=parser.parse_args(argv)
    binary,manifest=from_archive(args.archive,args.expect_directory_sha256)
    write(binary,manifest,args.output_directory)
    print(json.dumps({"entries":manifest["entry_count"],"catalog_sha256":manifest["catalog_sha256"],
                      "runtime_binding_verified":False,"replacement_enabled":False}))
    return 0


if __name__=="__main__":
    raise SystemExit(main())
