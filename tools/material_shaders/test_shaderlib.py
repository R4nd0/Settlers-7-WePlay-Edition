import hashlib
import struct
import unittest

from tools.material_shaders import shaderlib


def shader(stage="pixel", value=0x3f000000):
    if stage == "vertex":
        return struct.pack("<5I", 0xfffe0300, 0x02000001, 0xe00f0000, 0x90e40000, 0xffff)
    value_words = list(struct.unpack("<11I", shaderlib.marker_pixel_shader()))
    value_words[3] = value
    return struct.pack("<11I", *value_words)


def encoded(program):
    return (struct.pack("<I", len(program)) + program).hex().encode("ascii")


def fixture():
    return (b'FILEVERSION = 2.3.0.0;\r\nPLATFORM = DX9;\r\n'
            b'SOURCECODE=$TESTSOURCE{\r\n// authored fixture only\r\n}$TESTSOURCE\r\n'
            b'SHADER FixtureWater {\r\n'
            b'  ps_function = "fixture";\r\n'
            b'  vs_bytecode[vs_3_0]={' + encoded(shader("vertex")) + b'}\r\n'
            b'  ps_bytecode[ps_3_0]={' + encoded(shader()) + b'}\r\n}\r\n'
            b'SHADER FixtureOther {\r\n'
            b'  ps_bytecode[ps_3_0]={' + encoded(shader(value=0x3e800000)) + b'}\r\n}\r\n'
            b'EFFECT Fixture { TECHNIQUE "fixture" { passes = FixtureWater; } }\r\n')


class ShaderLibraryTests(unittest.TestCase):
    def test_parse_actual_text_format_shape_and_metadata_only(self):
        raw = fixture()
        library = shaderlib.parse(raw)
        self.assertEqual(library.records, ("FixtureWater", "FixtureOther"))
        self.assertEqual(len(library.blocks), 3)
        self.assertEqual(library.blocks[0].stage, "vertex")
        self.assertEqual(library.blocks[1].code, shader())
        metadata = library.metadata()
        self.assertEqual(metadata["resource_sha256"], hashlib.sha256(raw).hexdigest())
        self.assertFalse(metadata["runtime_verified"])
        self.assertNotIn("code", metadata["compiled_blocks"][0])

    def test_exact_original_identities_and_unrelated_bytes_retained(self):
        raw = fixture()
        library = shaderlib.parse(raw)
        block = library.blocks[1]
        changed, report = shaderlib.replace_with_marker(
            raw, expected_resource_sha256=library.resource_sha256,
            shader_record="FixtureWater", expected_compiled_sha256=shaderlib.digest(block.code))
        result = shaderlib.parse(changed)
        self.assertEqual(result.blocks[1].code, shaderlib.marker_pixel_shader())
        self.assertEqual(result.blocks[0].code, library.blocks[0].code)
        self.assertEqual(result.blocks[2].code, library.blocks[2].code)
        self.assertTrue(changed.startswith(raw[:block.hex_start]))
        self.assertTrue(changed.endswith(raw[block.hex_end:]))
        self.assertTrue(report["diagnostic_only"])
        self.assertFalse(report["runtime_binding_observed"])
        self.assertFalse(report["deployment_permitted"])

    def test_wrong_full_resource_or_compiled_identity_refuses(self):
        raw = fixture()
        library = shaderlib.parse(raw)
        for resource, code in (("0" * 64, shaderlib.digest(shader())),
                               (library.resource_sha256, "0" * 64)):
            with self.assertRaises(ValueError):
                shaderlib.replace_with_marker(raw, expected_resource_sha256=resource,
                                              shader_record="FixtureWater", expected_compiled_sha256=code)

    def test_header_version_platform_and_fence_must_match(self):
        for raw in (fixture().replace(b"2.3.0.0", b"2.4.0.0"), fixture().replace(b"DX9", b"DX11"),
                    fixture().replace(b"}$TESTSOURCE", b"}$OTHER"), fixture().replace(b"FILEVERSION", b"UNKNOWN")):
            with self.subTest(raw=raw[:40]), self.assertRaises(ValueError):
                shaderlib.parse(raw)

    def test_length_prefix_target_and_hex_integrity(self):
        source = fixture()
        body = encoded(shader())
        for raw in (source.replace(body, b"00000000" + body[8:]),
                    source.replace(b"ps_bytecode[ps_3_0]", b"ps_bytecode[vs_3_0]", 1),
                    source.replace(b"ps_bytecode[ps_3_0]", b"ps_bytecode[ps_4_0]", 1),
                    source.replace(body, b"z" + body[1:])):
            with self.subTest(), self.assertRaises(ValueError):
                shaderlib.parse(raw)

    def test_comments_source_fences_and_strings_do_not_create_records(self):
        source = fixture().replace(b"// authored fixture only", b"SHADER Hidden { ps_bytecode[ps_3_0]={00} }")
        source += b'// SHADER Commented {\r\n/* SHADER AlsoHidden { */\r\n'
        self.assertEqual(shaderlib.parse(source).records, ("FixtureWater", "FixtureOther"))

    def test_duplicate_records_and_stages_refused(self):
        for raw in (fixture().replace(b"FixtureOther {", b"FixtureWater {"),
                    fixture().replace(b'  ps_function = "fixture";', b"ps_bytecode[ps_3_0]={" + encoded(shader()) + b"}")):
            with self.assertRaises(ValueError):
                shaderlib.parse(raw)

    def test_extra_binary_bytes_and_incomplete_program_refused(self):
        body = encoded(shader())
        for candidate in (encoded(shader() + b"\0\0\0\0"), encoded(shader()[:-4])):
            with self.assertRaises(ValueError):
                shaderlib.parse(fixture().replace(body, candidate))


if __name__ == "__main__":
    unittest.main()
