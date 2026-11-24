"""Synthetic fixtures: these tests do not launch S7 or qualify its graphics."""
from copy import deepcopy
import hashlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from contextlib import redirect_stderr

from tools.shadow_detail import compare


def manifest():
    return {
        "schema": 1,
        "conditions": {
            "edition": "gold", "executable_sha256": "a" * 64,
            "renderer_sha256": "b" * 64, "save_sha256": "c" * 64,
            "camera_sha256": "d" * 64, "environment_sha256": "e" * 64,
            "native_settings_sha256": "f" * 64,
            "output_size": [2, 2], "render_size": [2, 2],
            "native_lod_scale": 1.5, "simulation_pace_ms": 100,
            "reshade_active": False, "hover_disabled": True,
            "stationary_roi_asserted": True,
        },
        "variant": {"filter": "existing-distant-2048-footprint", "shadow_dimension": 4096},
        "roi": [0, 0, 2, 2],
        "frames": [{
            "image": f"frame-{i}.bmp", "sha256": "1" * 64,
            "elapsed_seconds": i * 0.1, "simulation_turn": 100 + i,
            "sun_azimuth_degrees": 240 + i * 0.03,
            "sun_elevation_degrees": 30, "exposure": 2.6,
        } for i in range(6)],
    }


def bmp(rows, bits=24, top_down=False):
    width, height = len(rows[0]), len(rows)
    stride = ((width * bits + 31) // 32) * 4
    body = bytearray()
    for row in rows if top_down else reversed(rows):
        for r, g, b in row:
            body.extend((b, g, r))
            if bits == 32:
                body.append(17)  # Alpha must not affect RGB intensity.
        body.extend(bytes(stride - width * (bits // 8)))
    return (struct.pack("<2sIHHI", b"BM", 54 + len(body), 0, 0, 54)
            + struct.pack("<IiiHHIIiiII", 40, width, -height if top_down else height,
                          1, bits, 0, len(body), 0, 0, 0, 0) + body)


class ComparisonTests(unittest.TestCase):
    def test_valid_control_allows_only_intended_variant_to_differ(self):
        a = manifest()
        b = deepcopy(a)
        b["variant"] = {"filter": "experimental-unqualified"}
        compare.assert_comparable(a, b)

    def test_rejects_every_changed_invariant(self):
        a = manifest()
        for key in compare.IDENTITY_KEYS:
            with self.subTest(key=key):
                b = deepcopy(a)
                value = b["conditions"][key]
                if isinstance(value, bool):
                    b["conditions"][key] = not value
                elif isinstance(value, list):
                    b["conditions"][key] = [4, 4]
                elif isinstance(value, (int, float)):
                    b["conditions"][key] = value * 2
                elif key == "edition":
                    b["conditions"][key] = "steam-history"
                else:
                    b["conditions"][key] = "0" * 64
                with self.assertRaises(compare.CaptureError):
                    compare.assert_comparable(a, b)

    def test_rejects_mismatched_time_sun_exposure_and_turn(self):
        for key, delta in (("elapsed_seconds", 0.06), ("sun_azimuth_degrees", 0.11),
                           ("sun_elevation_degrees", 0.11), ("exposure", 0.002),
                           ("simulation_turn", 1)):
            with self.subTest(key=key):
                a = manifest()
                b = deepcopy(a)
                b["frames"][-1][key] += delta
                with self.assertRaises(compare.CaptureError):
                    compare.assert_comparable(a, b)

    def test_wraparound_sun_phase_is_nearby(self):
        a = manifest()
        b = deepcopy(a)
        a["frames"][0]["sun_azimuth_degrees"] = 359.99
        b["frames"][0]["sun_azimuth_degrees"] = 0.01
        compare.assert_comparable(a, b)

    def test_cannot_silently_drop_unpaired_frames(self):
        a = manifest()
        b = deepcopy(a)
        b["frames"].pop()
        with self.assertRaisesRegex(compare.CaptureError, "unequal frame"):
            compare.assert_comparable(a, b)

    def test_rejects_false_precision_and_bad_capture_bounds(self):
        cases = []
        for value in (float("nan"), float("inf"), True, "1.5"):
            b = manifest()
            b["conditions"]["native_lod_scale"] = value
            cases.append(b)
        for key in ("hover_disabled", "stationary_roi_asserted"):
            b = manifest()
            b["conditions"][key] = 1
            cases.append(b)
        b = manifest()
        b["roi"] = [1, 1, 2, 2]
        cases.append(b)
        b = manifest()
        b["conditions"]["unknown_setting"] = True
        cases.append(b)
        b = manifest()
        b["conditions"]["output_size"] = [1024, 1024]
        b["roi"] = [0, 0, 256, 256]
        b["frames"] = b["frames"] * 10
        cases.append(b)
        for i, data in enumerate(cases):
            with self.subTest(case=i), self.assertRaises(compare.CaptureError):
                compare.validate_manifest(data)

    def test_metrics_separate_constant_drift_and_adjacent_reversals(self):
        values = [[20, 0, 0], [20, 10, 10], [20, 20, 0],
                  [20, 30, 10], [20, 40, 0], [20, 50, 10]]
        result = compare.temporal_metrics(values, [0, 1, 2, 3, 4, 5])
        self.assertEqual(result["total_adjacent_direction_reversals"], 4)
        self.assertEqual(result["pixels_with_at_least_four_adjacent_reversals"], 1)
        self.assertAlmostEqual(result["mean_absolute_intensity_step_0_255"], 100 / 15)
        self.assertEqual(result["mean_absolute_intensity_change_per_second"], result["mean_absolute_intensity_step_0_255"])

    def test_small_steps_interrupt_reversals_and_threshold_is_strict(self):
        result = compare.temporal_metrics([[0], [10], [15], [0]], [0, 1, 2, 3])
        self.assertEqual(result["total_adjacent_direction_reversals"], 0)

    def test_invalid_metrics_and_tolerances_rejected(self):
        for values, times in (([[0], [1], [2]], [0, 1, 1]),
                              ([[0], [float("nan")], [2]], [0, 1, 2]),
                              ([[0], [1, 2], [2]], [0, 1, 2])):
            with self.assertRaises(compare.CaptureError):
                compare.temporal_metrics(values, times)
        with self.assertRaises(compare.CaptureError):
            compare.assert_comparable(manifest(), manifest(), angle_tolerance=float("nan"))

    def test_bmp_orientation_padding_alpha_and_roi(self):
        rows = [[(30, 60, 90), (0, 30, 60)], [(120, 150, 180), (210, 240, 255)]]
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "frame.bmp"
            for bits in (24, 32):
                for top_down in (False, True):
                    raw = bmp(rows, bits, top_down)
                    path.write_bytes(raw)
                    sha = hashlib.sha256(raw).hexdigest()
                    self.assertEqual(compare.read_bmp_roi(path, sha, [2, 2], [0, 0, 1, 2]), [60, 150])

    def test_bmp_rejects_wrong_hash_compression_dimensions_and_truncation(self):
        raw = bmp([[(0, 0, 0), (10, 10, 10)]] * 2)
        compressed = bytearray(raw)
        struct.pack_into("<I", compressed, 30, 1)
        cases = [(raw, "0" * 64, [2, 2]), (raw, None, [3, 2]),
                 (raw[:-1], None, [2, 2]), (bytes(compressed), None, [2, 2])]
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "frame.bmp"
            for data, digest, size in cases:
                path.write_bytes(data)
                with self.assertRaises(compare.CaptureError):
                    compare.read_bmp_roi(path, digest or hashlib.sha256(data).hexdigest(), size, [0, 0, 2, 2])

    def test_cli_produces_only_unqualified_statistics_from_synthetic_bmps(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            paths = []
            for kind in ("control", "candidate"):
                data = manifest()
                data["variant"] = {"synthetic_fixture": kind}
                for i, frame in enumerate(data["frames"]):
                    raw = bmp([[(i * 10, i * 10, i * 10)]] * 2)
                    # The integration fixture uses a 1x2 ROI/output.
                    data["conditions"]["output_size"] = [1, 2]
                    data["conditions"]["render_size"] = [1, 2]
                    data["roi"] = [0, 0, 1, 2]
                    frame["image"] = f"{kind}-{i}.bmp"
                    frame["sha256"] = hashlib.sha256(raw).hexdigest()
                    (root / frame["image"]).write_bytes(raw)
                path = root / f"{kind}.json"
                path.write_text(json.dumps(data), encoding="utf-8")
                paths.append(path)
            output = root / "report.json"
            self.assertEqual(compare.main([str(p) for p in paths] + ["--output", str(output)]), 0)
            result = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(result["control"]["metrics"], result["candidate"]["metrics"])
            self.assertNotIn("improvement_percent", result)
            self.assertIn("no automatic visual", result["qualification"])
            paths[0].write_text("{broken", encoding="utf-8")
            with redirect_stderr(io.StringIO()) as stderr:
                self.assertEqual(compare.main([str(p) for p in paths]), 2)
                self.assertIn("refused", stderr.getvalue())

    def test_manifest_size_is_bounded_before_parsing(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "oversize.json"
            path.write_bytes(b" " * (compare.MAX_MANIFEST_BYTES + 1))
            with self.assertRaisesRegex(compare.CaptureError, "1-MiB bound"):
                compare.compare_files(path, path)


if __name__ == "__main__":
    unittest.main()
