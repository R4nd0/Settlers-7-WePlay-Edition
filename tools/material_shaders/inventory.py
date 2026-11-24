"""Read-only, metadata-only original shader resource lab. No patch/deploy API.

Payload inspection requires an input fingerprint. An explicit directory-only
mode reads archive metadata without hashing gigabytes of unrelated content.
Reports remain under work/ and contain no bytecode.
"""
from __future__ import annotations

import argparse
from collections import Counter
import gzip
import hashlib
import io
import json
import mmap
from pathlib import Path, PurePosixPath
import re
import struct
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/lan"))
import bba

MAX_PAYLOAD = 16 * 1024 * 1024
MAX_CANDIDATES = 256
SHADER_SUFFIXES = {".fx", ".fxh", ".fxo", ".fxc", ".fxlib", ".hlsl", ".cso", ".shd",
                   ".shader", ".shaderlib", ".vsh", ".psh", ".vso", ".pso"}
MATERIAL_SUFFIXES = {".material", ".vmat", ".mat"}
VERSION = re.compile(rb"\x00[\x02\x03][\xfe\xff]\xff")
SHA256 = re.compile(r"[0-9a-f]{64}\Z")
KNOWN_OPCODES = frozenset(range(49)) | frozenset(range(64, 75)) | frozenset(range(76, 97))
REFERENCE_SUFFIXES = SHADER_SUFFIXES | MATERIAL_SUFFIXES | {".inc", ".dds", ".tga", ".png", ".bmp", ".xml"}


def safe_name(name: str) -> str:
    if (not name or len(name) > 512 or "\\" in name or ":" in name
            or any(ord(c) < 32 or ord(c) > 126 for c in name)
            or name.startswith("/") or any(p in ("", ".", "..") for p in name.split("/"))):
        raise ValueError("Invalid resource name")
    return name


def resource_kind(name: str) -> str | None:
    path = PurePosixPath(safe_name(name).lower())
    if path.suffix in SHADER_SUFFIXES or any("shader" in p for p in path.parts):
        return "shader_name_candidate"
    if path.suffix in MATERIAL_SUFFIXES or any("material" in p for p in path.parts):
        return "material_name_candidate"
    return None


def token_candidates(data: bytes) -> list[dict]:
    """Recognize bounded SM2/3 token envelopes, not semantic validity or bindings.

    Shader Model 1 has different instruction lengths and is deliberately omitted.
    Comment words are skipped as data, so an END or version inside a comment is
    never promoted to a separate stream. Unknown opcodes invalidate a candidate.
    """
    if len(data) > MAX_PAYLOAD:
        raise ValueError("Resource exceeds the inspection bound")
    result = []
    consumed = 0
    attempts = 0
    steps = 0
    for match in VERSION.finditer(data):
        start = match.start()
        if start < consumed:
            continue
        attempts += 1
        if attempts > MAX_CANDIDATES:
            raise ValueError("Too many token candidates")
        stage = "vertex" if data[start + 2] == 0xfe else "pixel"
        major = data[start + 1]
        cursor = start + 4
        instructions = comments = 0
        while cursor + 4 <= len(data):
            steps += 1
            if steps > 2_000_000:
                raise ValueError("Token scan exceeds the work bound")
            token = struct.unpack_from("<I", data, cursor)[0]
            opcode = token & 0xffff
            if token == 0xffff:
                end = cursor + 4
                if instructions:
                    result.append({"stage": stage, "model": f"{major}.0", "offset": start,
                                   "bytes": end - start, "instructions": instructions,
                                   "comments": comments,
                                   "sha256": hashlib.sha256(data[start:end]).hexdigest(),
                                   "validation": "token_envelope_only"})
                    consumed = end
                break
            if token & 0x80000000:
                break
            if opcode == 0xfffe:
                words = (token >> 16) & 0x7fff
                comments += 1
            elif opcode in KNOWN_OPCODES:
                words = (token >> 24) & 0xf
                instructions += 1
            else:
                break
            cursor += 4 * (words + 1)
    return result


def xml_references(data: bytes, names: set[str]) -> list[dict]:
    """Return path-like XML references only; never treat them as runtime bindings."""
    try:
        source = data.decode("utf-8-sig")
    except UnicodeDecodeError:
        return []  # No guessed legacy encoding or UTF16 entity processing.
    if "<!DOCTYPE" in source.upper() or "<!ENTITY" in source.upper():
        raise ValueError("XML declarations are not supported")
    try:
        root = ET.fromstring(source)
    except ET.ParseError:
        return []
    references = set()
    for element in root.iter():
        for value in (*element.attrib.values(), element.text or ""):
            value = value.strip().replace("\\", "/")
            if PurePosixPath(value.lower()).suffix not in REFERENCE_SUFFIXES:
                continue
            try:
                safe_name(value)
            except ValueError:
                continue
            references.add(value)
            if len(references) > 512:
                raise ValueError("Too many XML reference candidates")
    known = {name.casefold() for name in names}
    return [{"name": value, "exact_archive_name_match": value.casefold() in known}
            for value in sorted(references)]


def source_hints(name: str, data: bytes, names: set[str]) -> dict:
    includes = set()
    for match in re.finditer(rb"#[ \t]*include[ \t]*[\"<]([^\">\r\n]{1,512})[\">]", data):
        try:
            value = match[1].decode("ascii").replace("\\", "/")
            safe_name(value)
        except (UnicodeDecodeError, ValueError):
            continue
        includes.add(value)
        if len(includes) > 128:
            raise ValueError("Too many include reference hints")
    known = {key.casefold() for key in names}
    directory = PurePosixPath(name).parent
    references = []
    for include in sorted(includes):
        same_directory = str(directory / include)
        match = include if include.casefold() in known else same_directory if same_directory.casefold() in known else None
        references.append({"name": include, "resolved_archive_name_hint": match})
    registers = {"constant_float": set(), "constant_int": set(), "constant_bool": set(), "sampler": set()}
    classes = {b"c": "constant_float", b"i": "constant_int", b"b": "constant_bool", b"s": "sampler"}
    for match in re.finditer(rb"\bregister[ \t]*\([ \t]*([cibs])([0-9]{1,4})[ \t]*\)", data, re.IGNORECASE):
        registers[classes[match[1].lower()]].add(int(match[2]))
    return {"validation": "source_text_hints_only", "include_references": references,
            "register_indices": {kind: sorted(indices) for kind, indices in registers.items()}}


def describe(name: str, data: bytes, names: set[str] | None = None) -> dict:
    safe_name(name)
    if len(data) > MAX_PAYLOAD:
        raise ValueError("Resource exceeds the inspection bound")
    result = {"name": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
            "name_classification": resource_kind(name), "token_candidates": token_candidates(data),
            "material_binding_verified": False}
    # Four printable leading characters are a format identifier, not a decoded
    # container or any shader source. Unknown/nonprintable prefixes stay unknown.
    result["header_fourcc"] = data[:4].decode("ascii") if re.fullmatch(rb"[A-Za-z0-9_ ]{4}", data[:4]) else None
    result["hlsl_marker_counts"] = {
        label: len(re.findall(pattern, data, re.IGNORECASE))
        for label, pattern in {
            "float_vector_type": rb"\bfloat[234](?:x[234])?\b",
            "sampler_word": rb"\bsampler(?:1D|2D|3D|CUBE|_state)?\b",
            "technique_word": rb"\btechnique\b",
            "include_directive": rb"#[ \t]*include\b",
            "register_word": rb"\bregister\b",
        }.items()
    }
    result["container_format_verified"] = False
    result["source_hints"] = source_hints(name, data, names or set())
    if PurePosixPath(name.lower()).suffix == ".xml":
        result["xml_reference_candidates"] = xml_references(data, names or set())
    return result


def read_payload(archive: bba.Archive, name: str) -> bytes:
    """Bound decompression before reusing the archive's normal integrity checks."""
    safe_name(name)
    entry = archive.entries[name]
    values = entry.values
    if not 0 < values[2] <= MAX_PAYLOAD or not 0 < values[8] <= MAX_PAYLOAD:
        raise ValueError("Resource exceeds the inspection bound or is empty")
    if values[6] < 16 + archive.header_size or values[6] + values[8] > len(archive.raw):
        raise ValueError("Invalid BBA payload range")
    if values[4] != 17:
        return archive.read(name)
    encoded = archive.raw[values[6]:values[6] + values[8]]
    with gzip.GzipFile(fileobj=io.BytesIO(bba._gzip_xor(encoded, values[12]))) as stream:
        data = stream.read(MAX_PAYLOAD + 1)
    if len(data) != values[2] or bba.crc32(data) != values[3]:
        raise ValueError("BBA decompressed size/CRC32 mismatch")
    return data


def fingerprint_file(path: Path, expected: str) -> str:
    if not SHA256.fullmatch(expected):
        raise ValueError("Expected SHA256 must be 64 lowercase hex characters")
    with path.open("rb") as stream:
        actual = hashlib.file_digest(stream, "sha256").hexdigest()
    if actual != expected:
        raise ValueError("Input SHA256 mismatch")
    return actual


def inspect_archive(path: Path, expected: str | None, resources: list[str], *, directory_only=False,
                    expected_directory: str | None = None) -> dict:
    for name in resources:
        safe_name(name)
    if len(resources) != len(set(resources)) or len(resources) > 32:
        raise ValueError("Select at most 32 unique resources")
    if directory_only and (expected is not None or expected_directory is not None or resources):
        raise ValueError("Directory-only mode does not inspect payloads or assert full fingerprints")
    if not directory_only and (expected is None) == (expected_directory is None):
        raise ValueError("Select exactly one full-file or directory fingerprint")
    if expected_directory is not None and not SHA256.fullmatch(expected_directory):
        raise ValueError("Expected directory SHA256 must be 64 lowercase hex characters")
    identity = fingerprint_file(path, expected) if expected is not None else None
    with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as raw:
        if len(raw) < 32 or raw[:4] != b"BAF\x06":
            raise ValueError("Unsupported archive")
        header_size = struct.unpack_from("<I", raw, 8)[0]
        if header_size not in (16, 64) or len(raw) < 16 + header_size:
            raise ValueError("Unsupported archive header")
        header = bba.crypt(raw[16:16 + header_size], bba.HEAD_KEYS, 1231853475, 7)
        if struct.unpack_from("<I", header, 8)[0] > 32 * 1024 * 1024:
            raise ValueError("Archive directory exceeds the inspection bound")
        archive = bba.Archive(raw, copy=False)
        directory_identity = hashlib.sha256(archive.header + archive.directory).hexdigest()
        if expected_directory is not None and directory_identity != expected_directory:
            raise ValueError("Input directory SHA256 mismatch")
        candidates = []
        extensions = Counter()
        for name, entry in archive.entries.items():
            if entry.values[4] == 256 or name == ".":
                continue
            extensions[PurePosixPath(name.lower()).suffix or "(none)"] += 1
            kind = resource_kind(name)
            if kind:
                candidates.append({"name": name, "classification": kind, "bytes": entry.values[2],
                                   "stored_bytes": entry.values[8], "bba_type": entry.values[4]})
        inspected = [describe(name, read_payload(archive, name), set(archive.entries)) for name in resources]
        report = base_report(identity, "production_bba_v6")
        report.update(directory_only=directory_only, directory_sha256=directory_identity,
                      expected_directory_fingerprint_verified=expected_directory is not None,
                      input_bytes=len(raw), resource_count=len(archive.entries), extension_counts=dict(sorted(extensions.items())),
                      candidates=candidates, inspected=inspected)
        # Check the mapped input as well: replacing/changing the file during the
        # scan must not produce a report attributed to the initial fingerprint.
        if identity is not None and hashlib.sha256(raw).hexdigest() != identity:
            raise ValueError("Input changed during inspection")
        if identity is None:
            final = bba.Archive(raw, copy=False)
            if hashlib.sha256(final.header + final.directory).hexdigest() != directory_identity:
                raise ValueError("Input directory changed during inspection")
    return report


def base_report(identity: str | None, kind: str) -> dict:
    return {"format": "s7-material-shader-inventory-v1", "input_sha256": identity, "input_kind": kind,
            "full_input_fingerprint_verified": identity is not None,
            "runtime_verified": False, "material_binding_verified": False,
            "replacement_ready": False, "game_started": False, "writes_game_files": False}


def inspect_file(path: Path, expected: str, name: str) -> dict:
    safe_name(name)
    if not SHA256.fullmatch(expected):
        raise ValueError("Expected SHA256 must be 64 lowercase hex characters")
    with path.open("rb") as stream:
        data = stream.read(MAX_PAYLOAD + 1)
    if len(data) > MAX_PAYLOAD:
        raise ValueError("Resource exceeds the inspection bound")
    identity = hashlib.sha256(data).hexdigest()
    if identity != expected:
        raise ValueError("Input SHA256 mismatch")
    report = base_report(identity, "explicit_resource")
    report["inspected"] = [describe(name, data)]
    return report


def write_private_report(report: dict, path: Path) -> None:
    target = path.resolve()
    private_root = (ROOT / "work").resolve()
    if not target.is_relative_to(private_root):
        raise ValueError("Reports must remain under the repository work/ directory")
    # Exclusive creation preserves previous diagnostics and cannot overwrite an input.
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(report, stream, indent=2, sort_keys=True)
        stream.write("\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sources = parser.add_mutually_exclusive_group(required=True)
    sources.add_argument("--archive", type=Path, help="One explicitly chosen production BBA v6 archive")
    sources.add_argument("--file", type=Path, help="One explicitly chosen extracted resource")
    identity_args = parser.add_mutually_exclusive_group(required=True)
    identity_args.add_argument("--expect-sha256")
    identity_args.add_argument("--expect-directory-sha256", help="Explicit bounded BBA metadata/payload exploration; no full archive identity")
    identity_args.add_argument("--directory-only", action="store_true", help="BBA names/sizes only; no payload reads or full-file identity")
    parser.add_argument("--resource", action="append", default=[], help="Exact BBA payload to inspect (max 32)")
    parser.add_argument("--name", help="Logical name for --file, not an absolute local path")
    parser.add_argument("--output", type=Path, required=True, help="New JSON report under work/")
    args = parser.parse_args(argv)
    try:
        if args.archive:
            if args.name:
                raise ValueError("--name is only supported with --file")
            report = inspect_archive(args.archive, args.expect_sha256, args.resource, directory_only=args.directory_only,
                                     expected_directory=args.expect_directory_sha256)
        else:
            if not args.name or args.resource or args.directory_only or args.expect_directory_sha256:
                raise ValueError("--file requires --name and does not accept --resource")
            report = inspect_file(args.file, args.expect_sha256, args.name)
        write_private_report(report, args.output)
    except (OSError, ValueError, KeyError, struct.error, EOFError) as error:
        print(f"Material shader inspection refused: {type(error).__name__}", file=sys.stderr)
        return 2
    print("Private metadata report written; no shader replacement or runtime qualification.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
