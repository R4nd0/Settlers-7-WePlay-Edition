"""Compare controlled native S7 shadow captures without launching or patching S7.

Inputs are private JSON manifests and uncompressed 24/32-bit BMP screenshots.
The statistics describe temporal intensity variation, not its physical cause.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys


MAX_IMAGE_BYTES = 256 * 1024 * 1024
MAX_MANIFEST_BYTES = 1024 * 1024
MAX_FRAMES = 300
MAX_ROI_PIXELS = 65536
MAX_TOTAL_SAMPLES = 2_000_000
IDENTITY_KEYS = (
    "edition", "executable_sha256", "renderer_sha256", "save_sha256",
    "camera_sha256", "environment_sha256", "output_size", "render_size",
    "native_lod_scale", "native_settings_sha256", "simulation_pace_ms",
    "reshade_active", "hover_disabled", "stationary_roi_asserted",
)


class CaptureError(ValueError):
    """The input cannot establish a controlled comparison."""


def _number(value: object, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise CaptureError(f"{label} must be a finite number")
    if not math.isfinite(value):
        raise CaptureError(f"{label} must be a finite number")
    return float(value)


def _integer(value: object, label: str, low: int, high: int) -> int:
    if type(value) is not int or not low <= value <= high:
        raise CaptureError(f"{label} must be an integer in [{low}, {high}]")
    return value


def _digest(value: object, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64 or any(
        c not in "0123456789abcdef" for c in value
    ):
        raise CaptureError(f"{label} must be a lowercase SHA256")
    return value


def _size(value: object, label: str) -> tuple[int, int]:
    if not isinstance(value, list) or len(value) != 2:
        raise CaptureError(f"{label} must be [width, height]")
    return tuple(_integer(v, label, 1, 16384) for v in value)


def validate_manifest(data: object) -> dict:
    if not isinstance(data, dict) or type(data.get("schema")) is not int or data["schema"] != 1:
        raise CaptureError("manifest schema must be 1")
    conditions = data.get("conditions")
    if not isinstance(conditions, dict) or set(conditions) != set(IDENTITY_KEYS):
        raise CaptureError("conditions must contain exactly the documented identity keys")
    if conditions["edition"] not in ("gold", "ubisoft-history", "steam-history"):
        raise CaptureError("unsupported edition")
    for key in IDENTITY_KEYS:
        if key.endswith("_sha256"):
            _digest(conditions[key], key)
    _size(conditions["output_size"], "output_size")
    _size(conditions["render_size"], "render_size")
    if _number(conditions["native_lod_scale"], "native_lod_scale") <= 0:
        raise CaptureError("native_lod_scale must be a measured positive scale")
    if type(conditions["simulation_pace_ms"]) is not int or conditions["simulation_pace_ms"] != 100:
        raise CaptureError("capture requires the original 100-ms simulation pace")
    for key, expected in (("reshade_active", False), ("hover_disabled", True),
                          ("stationary_roi_asserted", True)):
        if conditions[key] is not expected:
            raise CaptureError(f"{key} must be {expected}")
    variant = data.get("variant")
    if not isinstance(variant, dict) or not variant:
        raise CaptureError("variant must describe the single intended shadow/detail change")
    try:
        json.dumps(variant, allow_nan=False)
    except (TypeError, ValueError) as exc:
        raise CaptureError("variant must contain finite JSON values") from exc
    roi = data.get("roi")
    if not isinstance(roi, list) or len(roi) != 4:
        raise CaptureError("roi must be [x, y, width, height]")
    x, y = (_integer(v, "roi position", 0, 16383) for v in roi[:2])
    w, h = (_integer(v, "roi size", 1, 16384) for v in roi[2:])
    ow, oh = conditions["output_size"]
    if x + w > ow or y + h > oh or w * h > MAX_ROI_PIXELS:
        raise CaptureError("roi exceeds the image or the 65536-pixel analysis bound")
    frames = data.get("frames")
    if not isinstance(frames, list) or not 3 <= len(frames) <= MAX_FRAMES:
        raise CaptureError(f"frames must contain between 3 and {MAX_FRAMES} captures")
    if w * h * len(frames) > MAX_TOTAL_SAMPLES:
        raise CaptureError("capture exceeds the 2000000-pixel temporal analysis bound")
    last_time = -math.inf
    last_turn = -1
    for frame in frames:
        if not isinstance(frame, dict):
            raise CaptureError("each frame must be an object")
        time = _number(frame.get("elapsed_seconds"), "elapsed_seconds")
        turn = _integer(frame.get("simulation_turn"), "simulation_turn", 0, 2**63 - 1)
        if time < 0 or time <= last_time or turn <= last_turn:
            raise CaptureError("frame elapsed times and native turns must strictly increase")
        last_time, last_turn = time, turn
        for key, lo, hi in (("sun_azimuth_degrees", 0, 360),
                            ("sun_elevation_degrees", -90, 90), ("exposure", 0, 100)):
            n = _number(frame.get(key), key)
            if not lo <= n <= hi:
                raise CaptureError(f"{key} outside its range")
        if not isinstance(frame.get("image"), str) or not frame["image"]:
            raise CaptureError("each frame requires an image path")
        _digest(frame.get("sha256"), "frame.sha256")
    return data


def assert_comparable(a: dict, b: dict, *, time_tolerance: float = 0.05,
                      angle_tolerance: float = 0.1, exposure_tolerance: float = 0.001) -> None:
    """Reject confounded comparisons; no interpolation or silent frame dropping."""
    validate_manifest(a)
    validate_manifest(b)
    for key in IDENTITY_KEYS:
        if a["conditions"][key] != b["conditions"][key]:
            raise CaptureError(f"mismatched condition: {key}")
    if a["roi"] != b["roi"]:
        raise CaptureError("mismatched roi")
    if len(a["frames"]) != len(b["frames"]):
        raise CaptureError("unequal frame counts; capture paired native turns")
    for label, value in (("time_tolerance", time_tolerance), ("angle_tolerance", angle_tolerance),
                         ("exposure_tolerance", exposure_tolerance)):
        if _number(value, label) < 0:
            raise CaptureError(f"{label} must be nonnegative")
    for i, (fa, fb) in enumerate(zip(a["frames"], b["frames"])):
        if fa["simulation_turn"] != fb["simulation_turn"]:
            raise CaptureError(f"frame {i}: unmatched native simulation turn")
        azimuth = abs(fa["sun_azimuth_degrees"] - fb["sun_azimuth_degrees"]) % 360
        azimuth = min(azimuth, 360 - azimuth)
        if azimuth > angle_tolerance or abs(fa["sun_elevation_degrees"] - fb["sun_elevation_degrees"]) > angle_tolerance:
            raise CaptureError(f"frame {i}: unmatched sun phase")
        if abs(fa["exposure"] - fb["exposure"]) > exposure_tolerance:
            raise CaptureError(f"frame {i}: unmatched exposure")
        if abs(fa["elapsed_seconds"] - fb["elapsed_seconds"]) > time_tolerance:
            raise CaptureError(f"frame {i}: unmatched capture cadence")


def read_bmp_roi(path: Path, expected_sha256: str, size: list[int], roi: list[int]) -> list[float]:
    """Read RGB-channel averages from uncompressed Windows 24/32-bit BMPs."""
    try:
        with path.open("rb") as handle:
            if not 54 <= path.stat().st_size <= MAX_IMAGE_BYTES:
                raise CaptureError("BMP outside bounded size")
            raw = handle.read(MAX_IMAGE_BYTES + 1)
    except OSError as exc:
        raise CaptureError(f"cannot read BMP: {exc.strerror}") from exc
    if len(raw) > MAX_IMAGE_BYTES or hashlib.sha256(raw).hexdigest() != expected_sha256:
        raise CaptureError("BMP SHA256 mismatch or size changed while reading")
    if raw[:2] != b"BM":
        raise CaptureError("capture must be an uncompressed Windows BMP")
    offset, dib = struct.unpack_from("<II", raw, 10)
    if dib < 40 or 14 + dib > len(raw) or offset < 14 + dib:
        raise CaptureError("unsupported or truncated BMP header")
    width, height, planes, bits, compression = struct.unpack_from("<iiHHI", raw, 18)
    if [width, abs(height)] != size or height == 0 or planes != 1 or bits not in (24, 32) or compression != 0:
        raise CaptureError("BMP dimensions, format or compression do not match the capture")
    stride = ((width * bits + 31) // 32) * 4
    if offset + stride * abs(height) > len(raw):
        raise CaptureError("truncated BMP pixels")
    x, y, w, h = roi
    if x < 0 or y < 0 or w < 1 or h < 1 or x + w > width or y + h > abs(height):
        raise CaptureError("roi outside BMP")
    result = []
    for row in range(y, y + h):
        stored_row = row if height < 0 else height - row - 1
        start = offset + stored_row * stride + x * (bits // 8)
        for col in range(w):
            p = start + col * (bits // 8)
            result.append((raw[p] + raw[p + 1] + raw[p + 2]) / 3.0)
    return result


def temporal_metrics(samples: list[list[float]], times: list[float], *, threshold: float = 5.0) -> dict:
    """Do not label observed image changes as shadows or convert them to FPS."""
    if len(samples) < 3 or len(samples) != len(times) or not samples[0]:
        raise CaptureError("metrics require three or more equally sized nonempty frames")
    threshold = _number(threshold, "reversal threshold")
    if threshold < 0:
        raise CaptureError("reversal threshold must be nonnegative")
    pixels = len(samples[0])
    for frame in samples:
        if len(frame) != pixels:
            raise CaptureError("different ROI sample sizes")
        for value in frame:
            if not 0 <= _number(value, "pixel") <= 255:
                raise CaptureError("pixel outside [0, 255]")
    for i, value in enumerate(times):
        if _number(value, "time") < 0 or (i and value <= times[i - 1]):
            raise CaptureError("metrics times must strictly increase")
    previous_signs = [0] * pixels
    reversals = [0] * pixels
    absolute_steps = 0.0
    rates = 0.0
    for i in range(1, len(samples)):
        dt = times[i] - times[i - 1]
        for p, (before, after) in enumerate(zip(samples[i - 1], samples[i])):
            delta = after - before
            absolute_steps += abs(delta)
            rates += abs(delta) / dt
            sign = (1 if delta > 0 else -1) if abs(delta) > threshold else 0
            # Small steps interrupt a reversal sequence instead of bridging it.
            if sign and previous_signs[p] and sign != previous_signs[p]:
                reversals[p] += 1
            previous_signs[p] = sign
    comparisons = (len(samples) - 1) * pixels
    return {
        "frames": len(samples), "roi_pixels": pixels,
        "duration_seconds": times[-1] - times[0],
        "mean_absolute_intensity_step_0_255": absolute_steps / comparisons,
        "mean_absolute_intensity_change_per_second": rates / comparisons,
        "reversal_step_threshold_0_255": threshold,
        "total_adjacent_direction_reversals": sum(reversals),
        "pixels_with_at_least_four_adjacent_reversals": sum(n >= 4 for n in reversals),
        "mean_temporal_pixel_range_0_255": sum(max(v) - min(v) for v in zip(*samples)) / pixels,
    }


def compare_files(control: Path, candidate: Path, threshold: float = 5.0) -> dict:
    def load(path: Path) -> dict:
        try:
            with path.open("rb") as handle:
                raw = handle.read(MAX_MANIFEST_BYTES + 1)
            if len(raw) > MAX_MANIFEST_BYTES:
                raise CaptureError("capture manifest exceeds the 1-MiB bound")
            return validate_manifest(json.loads(raw.decode("utf-8")))
        except (OSError, UnicodeError, json.JSONDecodeError) as exc:
            raise CaptureError("cannot read valid capture manifest") from exc
    a, b = load(control), load(candidate)
    assert_comparable(a, b)
    reports = {}
    for name, manifest, path in (("control", a, control), ("candidate", b, candidate)):
        samples = []
        for frame in manifest["frames"]:
            image = path.parent / frame["image"]
            samples.append(read_bmp_roi(image, frame["sha256"], manifest["conditions"]["output_size"], manifest["roi"]))
        reports[name] = {
            "variant": manifest["variant"],
            "metrics": temporal_metrics(samples, [f["elapsed_seconds"] for f in manifest["frames"]], threshold=threshold),
        }
    return {
        "schema": 1, "status": "paired-capture-statistics",
        "qualification": "no automatic visual, performance or shadow-cause qualification",
        "limits": [
            "Identity and stationary-region conditions are operator assertions, not independent runtime checks.",
            "Changing intensity can include native SSAO, animation, lighting and capture artifacts.",
            "GPU readback cadence cannot establish game FPS or frame pacing.",
        ],
        "roi": a["roi"], **reports,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("control", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--reversal-threshold", type=float, default=5.0)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        report = compare_files(args.control, args.candidate, args.reversal_threshold)
        rendered = json.dumps(report, indent=2, allow_nan=False) + "\n"
        if args.output:
            args.output.write_text(rendered, encoding="utf-8")
        else:
            print(rendered, end="")
    except (CaptureError, OSError) as exc:
        print(f"Capture comparison refused: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
