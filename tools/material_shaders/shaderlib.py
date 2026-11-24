"""Checked S7 text ShaderLib 2.3.0.0/DX9 metadata and private in-memory edits.

No game launch, archive write, file export or deployment is implemented here.
Original source and decoded bytecode remain in caller-owned memory. Metadata
identifies exact compiled shaders for a future read-only runtime binding probe.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import re
import struct

from .inventory import MAX_PAYLOAD, token_candidates

HEADER = re.compile(rb"\AFILEVERSION[ \t]*=[ \t]*2\.3\.0\.0[ \t]*;[ \t]*\r?\nPLATFORM[ \t]*=[ \t]*DX9[ \t]*;[ \t]*\r?\n")
SOURCE = re.compile(rb"(?m)^[ \t]*SOURCECODE[ \t]*=[ \t]*\$([A-Za-z_][A-Za-z0-9_]{0,63})\{")
RECORD = re.compile(rb"(?m)^[ \t]*SHADER[ \t]+([A-Za-z_][A-Za-z0-9_]{0,127})[ \t]*\{")
BYTECODE = re.compile(rb"(?m)^[ \t]*(vs|ps)_bytecode\[(vs|ps)_([23])_0\][ \t]*=[ \t]*\{([0-9A-Fa-f \t\r\n]+)\}")
DIGEST = re.compile(r"[0-9a-f]{64}\Z")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def checked_digest(value: str) -> str:
    if not isinstance(value, str) or not DIGEST.fullmatch(value):
        raise ValueError("Expected lowercase SHA256")
    return value


def _masked(data: bytes) -> bytearray:
    """Mask source fences, comments and strings while retaining byte offsets."""
    value = bytearray(data)
    fences = list(SOURCE.finditer(data))
    if len(fences) != 1:
        raise ValueError("Expected one supported source fence")
    start = fences[0]
    closer = b"}$" + start[1]
    end = data.find(closer, start.end())
    if end < 0 or data.find(closer, end + len(closer)) >= 0:
        raise ValueError("Malformed or ambiguous source fence")
    for position in range(start.start(), end + len(closer)):
        if value[position] not in (10, 13):
            value[position] = 32
    position = 0
    while position < len(value):
        if value[position:position + 2] == b"//":
            end = value.find(b"\n", position + 2)
            end = len(value) if end < 0 else end
        elif value[position:position + 2] == b"/*":
            end = value.find(b"*/", position + 2)
            if end < 0:
                raise ValueError("Unterminated comment")
            end += 2
        elif value[position] == 34:
            end = position + 1
            while end < len(value):
                if value[end] == 92:
                    end += 2
                elif value[end] == 34:
                    end += 1
                    break
                else:
                    end += 1
            else:
                raise ValueError("Unterminated string")
        else:
            position += 1
            continue
        for index in range(position, end):
            if value[index] not in (10, 13):
                value[index] = 32
        position = end
    return value


@dataclass(frozen=True)
class CompiledBlock:
    shader: str
    stage: str
    model: str
    hex_start: int
    hex_end: int
    code: bytes

    def metadata(self) -> dict:
        return {"shader_record": self.shader, "stage": self.stage, "model": self.model,
                "compiled_bytes": len(self.code), "compiled_sha256": digest(self.code),
                "material_binding_verified": False}


@dataclass(frozen=True)
class Library:
    resource_sha256: str
    records: tuple[str, ...]
    blocks: tuple[CompiledBlock, ...]

    def metadata(self) -> dict:
        return {"format": "s7-text-shaderlib-dx9-v1", "file_version": "2.3.0.0", "platform": "DX9",
                "resource_sha256": self.resource_sha256, "shader_records": list(self.records),
                "compiled_blocks": [block.metadata() for block in self.blocks],
                "runtime_verified": False, "replacement_ready": False,
                "material_binding_verified": False}


def parse(data: bytes) -> Library:
    if not isinstance(data, bytes) or not 0 < len(data) <= MAX_PAYLOAD or not HEADER.match(data):
        raise ValueError("Unsupported ShaderLib header or size")
    masked = _masked(data)
    records = list(RECORD.finditer(masked))
    if not 0 < len(records) <= 128:
        raise ValueError("Unsupported shader record count")
    names = [match[1].decode("ascii") for match in records]
    if len(names) != len(set(names)):
        raise ValueError("Duplicate shader record")
    blocks = []
    for match, name in zip(records, names):
        depth = 1
        end = match.end()
        while end < len(masked) and depth:
            depth += (masked[end] == 123) - (masked[end] == 125)
            end += 1
        if depth:
            raise ValueError("Unterminated shader record")
        body = data[match.end():end - 1]
        body_masked = masked[match.end():end - 1]
        declarations = list(re.finditer(rb"\b(?:vs|ps)_bytecode\b", body_masked))
        matches = list(BYTECODE.finditer(body))
        # Never silently skip malformed/unknown-profile encoded programs.
        if len(matches) != len(declarations):
            raise ValueError("Unsupported bytecode declaration")
        stages = set()
        for code in matches:
            stage = "vertex" if code[1] == b"vs" else "pixel"
            if code[1] != code[2] or stage in stages:
                raise ValueError("Bytecode stage mismatch or duplicate")
            stages.add(stage)
            encoded = bytes.fromhex(code[4].decode("ascii"))
            if len(encoded) < 12 or struct.unpack_from("<I", encoded)[0] != len(encoded) - 4:
                raise ValueError("Encoded shader byte length mismatch")
            program = encoded[4:]
            candidates = token_candidates(program)
            model = code[3].decode("ascii") + ".0"
            if (len(candidates) != 1 or candidates[0]["offset"] != 0
                    or candidates[0]["bytes"] != len(program)
                    or candidates[0]["stage"] != stage or candidates[0]["model"] != model):
                raise ValueError("Unsupported compiled shader envelope")
            blocks.append(CompiledBlock(name, stage, model,
                                        match.end() + code.start(4), match.end() + code.end(4), program))
    if not blocks:
        raise ValueError("No compiled shaders")
    return Library(digest(data), tuple(names), tuple(blocks))


def marker_pixel_shader() -> bytes:
    """Authored SM3 diagnostic: output opaque magenta, no textures/game constants.

    ps_3_0; def c0, 1,0,1,1; mov oC0,c0; end. This is a binding marker,
    not a visual-quality improvement. The original program is not included.
    """
    return struct.pack("<11I", 0xffff0300, 0x05000051, 0xa00f0000,
                       0x3f800000, 0, 0x3f800000, 0x3f800000,
                       0x02000001, 0x800f0800, 0xa0e40000, 0x0000ffff)


def replace_with_marker(data: bytes, *, expected_resource_sha256: str, shader_record: str,
                        expected_compiled_sha256: str) -> tuple[bytes, dict]:
    """Create a private in-memory candidate only; no files or runtime are changed.

    Both full resource and exact original compiled-program identities are
    mandatory. A runtime binding still needs to be observed before deployment.
    Every unrelated character, shader and render-state declaration is retained.
    """
    checked_digest(expected_resource_sha256)
    checked_digest(expected_compiled_sha256)
    library = parse(data)
    if library.resource_sha256 != expected_resource_sha256:
        raise ValueError("ShaderLib resource identity mismatch")
    selected = [block for block in library.blocks if block.shader == shader_record and block.stage == "pixel"]
    if len(selected) != 1:
        raise ValueError("No unique pixel shader record")
    block = selected[0]
    if block.model != "3.0" or digest(block.code) != expected_compiled_sha256:
        raise ValueError("Original pixel shader identity or profile mismatch")
    program = marker_pixel_shader()
    payload = (struct.pack("<I", len(program)) + program).hex().encode("ascii")
    newline = b"\r\n" if b"\r\n" in data else b"\n"
    replacement = newline.join(payload[start:start + 160] for start in range(0, len(payload), 160))
    candidate = data[:block.hex_start] + replacement + data[block.hex_end:]
    verified = parse(candidate)
    if len(verified.blocks) != len(library.blocks) or verified.records != library.records:
        raise ValueError("Candidate changed the library structure")
    for original, changed in zip(library.blocks, verified.blocks):
        if original.shader == shader_record and original.stage == "pixel":
            if changed.code != program:
                raise ValueError("Candidate marker readback mismatch")
        elif original.code != changed.code:
            raise ValueError("Candidate changed an unrelated shader")
    return candidate, {"format": "s7-material-marker-candidate-v1", "diagnostic_only": True,
                       "input_resource_sha256": library.resource_sha256,
                       "output_resource_sha256": verified.resource_sha256,
                       "original_compiled_sha256": digest(block.code), "marker_compiled_sha256": digest(program),
                       "shader_record": shader_record, "runtime_binding_observed": False,
                       "runtime_verified": False, "replacement_ready": False, "deployment_permitted": False}
