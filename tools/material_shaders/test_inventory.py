"""Synthetic parser/integrity/privacy checks; no game data or game process."""
import gzip
import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from tools.material_shaders import inventory as lab


def words(*values):
    return struct.pack("<" + "I" * len(values), *values)


def archive_fixture(name="Shaders/Water.pso", data=b"fixture", compressed=False):
    spelling = name.replace("/", "\\").encode("ascii") + b"\0"
    padded = ((len(spelling) + 3) & ~3) + (4 if len(spelling) % 4 == 0 else 0)
    payload = lab.bba._gzip_xor(gzip.compress(data), len(spelling), True) if compressed else data
    values = [0] * 16
    values[2], values[3], values[4], values[6] = len(data), lab.bba.crc32(data), 17 if compressed else 0, 32
    values[8], values[12] = len(payload), len(spelling)
    entry = words(*values) + spelling + b"\0" * (padded - len(spelling))
    directory = bytearray(276)
    struct.pack_into("<3I", directory, 0, 0, 272, 276 + len(entry))
    struct.pack_into("<I", directory, 272, 1)
    directory.extend(entry + words(1, 0, 0))
    encoded = lab.bba.crypt(directory, lab.bba.FILE_KEYS, 1532281822, 8, True)
    header = lab.bba.crypt(words(32 + len(payload), 0, len(encoded), lab.bba.crc32(encoded)),
                           lab.bba.HEAD_KEYS, 1231853475, 7, True)
    return b"BAF\x06" + words(0, 16, 0) + header + payload + encoded


class InventoryTests(unittest.TestCase):
    def test_model3_envelope_and_hash_without_bytecode_export(self):
        data = b"prefix" + words(0xffff0300, 0x02000001, 0x800f0000, 0x90e40000, 0x0000ffff)
        report = lab.describe("Shaders/Water.pso", data)
        candidate = report["token_candidates"][0]
        self.assertEqual((candidate["offset"], candidate["stage"], candidate["model"]), (6, "pixel", "3.0"))
        self.assertEqual(candidate["validation"], "token_envelope_only")
        self.assertEqual(candidate["sha256"], hashlib.sha256(data[6:]).hexdigest())
        self.assertFalse(report["material_binding_verified"])
        self.assertNotIn("data", report)

    def test_source_markers_are_counts_not_source_exports_or_binding_claims(self):
        report = lab.describe("Shaders/Water.inc", b"float4 fixture; sampler2D s; #include \"fixture.inc\"")
        self.assertEqual(report["hlsl_marker_counts"]["float_vector_type"], 1)
        self.assertEqual(report["hlsl_marker_counts"]["sampler_word"], 1)
        self.assertEqual(report["hlsl_marker_counts"]["include_directive"], 1)
        self.assertFalse(report["container_format_verified"])
        self.assertNotIn("fixture", str(report["hlsl_marker_counts"]))

    def test_source_include_and_register_hints_do_not_export_declarations(self):
        source = b'#include "common.inc"\n#include "../unsupported.inc"\nfloat4 fixture:register(c12); sampler2D s:register(s3);'
        report = lab.describe("Shaders/Water.inc", source, {"Shaders/common.inc"})
        hints = report["source_hints"]
        self.assertEqual(hints["include_references"], [{"name": "common.inc", "resolved_archive_name_hint": "Shaders/common.inc"}])
        self.assertEqual(hints["register_indices"]["constant_float"], [12])
        self.assertEqual(hints["register_indices"]["sampler"], [3])
        self.assertEqual(hints["validation"], "source_text_hints_only")
        self.assertNotIn("fixture", str(hints))

    def test_comment_end_and_version_are_not_stream_boundaries(self):
        data = words(0xfffe0200, 0x0002fffe, 0x0000ffff, 0xffff0300,
                     0x02000001, 0x800f0000, 0x90e40000, 0xffff)
        result = lab.token_candidates(data)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0]["comments"], 1)
        self.assertEqual(result[0]["stage"], "vertex")

    def test_truncated_comments_instructions_missing_end_and_model1(self):
        for data in (words(0xffff0300, 0x7ffffffe), words(0xffff0300, 0x0f000001, 0xffff),
                     words(0xffff0300, 0x02000001, 1, 2), words(0xffff0104, 0, 0xffff),
                     words(0xffff0300, 0x0000f000, 0xffff), words(0xffff0300, 0xffff)):
            with self.subTest(data=data):
                self.assertEqual(lab.token_candidates(data), [])

    def test_reserved_opcode_is_not_a_known_instruction(self):
        for opcode in (49, 63, 75):
            with self.subTest(opcode=opcode):
                self.assertEqual(lab.token_candidates(words(0xffff0300, opcode, 0xffff)), [])

    def test_candidate_limit(self):
        with patch.object(lab, "MAX_CANDIDATES", 2):
            with self.assertRaises(ValueError):
                lab.token_candidates(words(0xffff0300, 0x0000f000) * 3)

    def test_resource_names_do_not_accept_paths_or_controls(self):
        for name in ("../a", "/a", "C:/a", "a\\b", "a//b", "a/./b", "a\nb", "a/../b"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                lab.safe_name(name)

    def test_name_candidates_do_not_establish_binding(self):
        self.assertEqual(lab.resource_kind("Materials/grass.xml"), "material_name_candidate")
        self.assertEqual(lab.resource_kind("Config/shaders.xml"), "shader_name_candidate")
        self.assertIsNone(lab.resource_kind("Textures/water.dds"))

    def test_xml_path_hints_are_bounded_and_never_runtime_bindings(self):
        data = b'<Material shader="Shaders/water.pso" strength="1"><Texture>Textures/water.dds</Texture></Material>'
        result = lab.describe("Materials/water.xml", data, {"Textures/water.dds"})
        self.assertEqual(result["xml_reference_candidates"], [
            {"name": "Shaders/water.pso", "exact_archive_name_match": False},
            {"name": "Textures/water.dds", "exact_archive_name_match": True}])
        self.assertFalse(result["material_binding_verified"])
        with self.assertRaises(ValueError):
            lab.xml_references(b'<!DOCTYPE x [<!ENTITY a "expansion">]><x/>', set())

    def test_archive_inventory_does_not_read_unselected_payloads(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "fixture.bba"
            raw = archive_fixture()
            path.write_bytes(raw)
            with patch.object(lab, "read_payload", side_effect=AssertionError("unexpected payload read")):
                report = lab.inspect_archive(path, hashlib.sha256(raw).hexdigest(), [])
            self.assertEqual(report["extension_counts"], {".pso": 1})
            self.assertEqual(report["inspected"], [])
            self.assertFalse(report["replacement_ready"])
            self.assertEqual(path.read_bytes(), raw)

    def test_directory_only_reads_no_payload_or_full_file_fingerprint(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "fixture.bba"
            path.write_bytes(archive_fixture())
            with patch.object(lab, "fingerprint_file", side_effect=AssertionError("full file read")), \
                    patch.object(lab, "read_payload", side_effect=AssertionError("payload read")):
                report = lab.inspect_archive(path, None, [], directory_only=True)
            self.assertFalse(report["full_input_fingerprint_verified"])
            self.assertIsNone(report["input_sha256"])
            self.assertEqual(len(report["directory_sha256"]), 64)
            with self.assertRaises(ValueError):
                lab.inspect_archive(path, None, ["Shaders/Water.pso"], directory_only=True)
            with self.assertRaises(ValueError):
                lab.inspect_archive(path, None, [])

    def test_explicit_directory_pin_allows_only_read_only_payload_metadata(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "fixture.bba"
            raw = archive_fixture(data=b"VSHD" + words(0xffff0300, 0, 0xffff))
            path.write_bytes(raw)
            first = lab.inspect_archive(path, None, [], directory_only=True)
            with patch.object(lab, "fingerprint_file", side_effect=AssertionError("full file read")):
                report = lab.inspect_archive(path, None, ["Shaders/Water.pso"],
                                             expected_directory=first["directory_sha256"])
            self.assertTrue(report["expected_directory_fingerprint_verified"])
            self.assertFalse(report["full_input_fingerprint_verified"])
            self.assertFalse(report["replacement_ready"])
            self.assertEqual(report["inspected"][0]["header_fourcc"], "VSHD")
            with self.assertRaises(ValueError):
                lab.inspect_archive(path, None, ["Shaders/Water.pso"], expected_directory="0" * 64)
            with self.assertRaises(ValueError):
                lab.inspect_archive(path, "0" * 64, [], expected_directory=first["directory_sha256"])

    def test_archive_selected_resource_crc_and_fingerprint(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "fixture.bba"
            data = words(0xffff0300, 0, 0xffff)
            raw = archive_fixture(data=data, compressed=True)
            path.write_bytes(raw)
            report = lab.inspect_archive(path, hashlib.sha256(raw).hexdigest(), ["Shaders/Water.pso"])
            self.assertEqual(report["inspected"][0]["sha256"], hashlib.sha256(data).hexdigest())
            with self.assertRaises(ValueError):
                lab.inspect_archive(path, "0" * 64, [])
            broken = bytearray(raw)
            broken[32] ^= 1
            path.write_bytes(broken)
            with self.assertRaises((ValueError, gzip.BadGzipFile)):
                lab.inspect_archive(path, hashlib.sha256(broken).hexdigest(), ["Shaders/Water.pso"])

    def test_bounded_decompression_rejects_forged_small_size(self):
        raw = archive_fixture(data=b"a" * 4096, compressed=True)
        archive = lab.bba.Archive(raw)
        values = list(archive.entries["Shaders/Water.pso"].values)
        values[2] = 1
        archive.entries["Shaders/Water.pso"] = lab.bba.Entry("Shaders/Water.pso", 0, tuple(values))
        with patch.object(lab, "MAX_PAYLOAD", 128), self.assertRaises(ValueError):
            lab.read_payload(archive, "Shaders/Water.pso")

    def test_file_input_mismatch_and_size_bound(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "resource"
            path.write_bytes(b"fixture")
            with self.assertRaises(ValueError):
                lab.inspect_file(path, "0" * 64, "Shaders/Water.pso")
            with patch.object(lab, "MAX_PAYLOAD", 4), self.assertRaises(ValueError):
                lab.inspect_file(path, hashlib.sha256(b"fixture").hexdigest(), "Shaders/Water.pso")

    def test_report_private_only_exclusive_and_metadata_flags(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(lab, "ROOT", Path(folder)):
            root = Path(folder)
            with self.assertRaises(ValueError):
                lab.write_private_report({}, root / "public.json")
            path = root / "work/report.json"
            lab.write_private_report(lab.base_report("0" * 64, "fixture"), path)
            with self.assertRaises(FileExistsError):
                lab.write_private_report({}, path)
            self.assertIn('"writes_game_files": false', path.read_text())


if __name__ == "__main__":
    unittest.main()
