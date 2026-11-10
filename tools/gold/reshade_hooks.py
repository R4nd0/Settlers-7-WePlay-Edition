"""Reproduce the reviewed Vulkan state-event patch on exact ReShade 6.8.0.

This modifies developer source only. It never downloads, installs or launches a
runtime, and does not change the History Edition runtime pins.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess

BASE = "18deaa52de0c425a78b329e9cb3c497281cd00ec"
PATCH = Path(__file__).with_name("reshade_state.patch")
PATCH_SHA256 = "66612956c1960f9aa9f50589fadd77f8988674373cfbf62c0eaa89e24706f435"
FILES = {
    "source/vulkan/vulkan.cpp": (
        "43ab1352863a8b20d575915c2a83d232a1d94ac6351a5805a3654479e61297f9",
        "e66e99fe8ed3f7cd90e5f53d07facfddb4c677215306ed750a1ee528e09d2b91"),
    "source/vulkan/vulkan_hooks.hpp": (
        "f2649da9545835da6de0c631efeb741212c119c85eb956bf882842f3b2b96b4e",
        "cdddccdae1b822a735d4e6274589639184a9985cca84112784b1b6af92e11cb2"),
    "source/vulkan/vulkan_hooks_command_list.cpp": (
        "641398163a328331b917cc909d759737df361076ef5ecfd2318cf32fd94a29e7",
        "879e6c6654e2c263520200c2b0634851fa30999c59c9f733747cd2c12f589a3c"),
}
MARKER = "// WePlay experimental Vulkan 1.3/1.4 state-event coverage."


def digest(data: bytes) -> str:
    return hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest()


def git(source: Path, *args: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(source), *args], text=True, encoding="utf-8").strip()


def verify(source: Path, *, patched: bool) -> None:
    if git(source, "rev-parse", "HEAD") != BASE:
        raise ValueError("Expected the exact ReShade 6.8.0 base commit")
    if digest(PATCH.read_bytes()) != PATCH_SHA256:
        raise ValueError("Reviewed patch hash changed")
    for name, hashes in FILES.items():
        if digest((source / name).read_bytes()) != hashes[int(patched)]:
            raise ValueError(f"Unexpected {'patched' if patched else 'original'} source: {name}")


def apply(source: Path) -> None:
    # No partially patched or locally edited source is admitted.
    if all(digest((source / name).read_bytes()) == hashes[1]
           for name, hashes in FILES.items()):
        verify(source, patched=True)
        return
    verify(source, patched=False)
    git(source, "apply", "--check", "--ignore-space-change", str(PATCH.resolve()))
    git(source, "apply", "--ignore-space-change", str(PATCH.resolve()))
    verify(source, patched=True)


def fixture(source: Path, output: Path) -> None:
    verify(source, patched=True)
    body = (source / "source/vulkan/vulkan_hooks_command_list.cpp").read_text()
    if body.count(MARKER) != 1:
        raise ValueError("Expected exactly one reviewed wrapper section")
    prefix = PATCH.with_name("reshade_hooks_fixture_prefix.cpp").read_text()
    suffix = PATCH.with_name("reshade_hooks_fixture_suffix.cpp").read_text()
    with output.open("x", encoding="utf-8", newline="\n") as target:
        target.write(prefix + body[body.index(MARKER):] + suffix)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--fixture", type=Path)
    args = parser.parse_args()
    if args.apply:
        apply(args.source.resolve())
    verify(args.source.resolve(), patched=True)
    if args.fixture:
        fixture(args.source.resolve(), args.fixture.resolve())
    print("Exact ReShade Vulkan state hooks verified; no runtime installed or launched.")


if __name__ == "__main__":
    main()
