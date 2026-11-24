"""Bounded backups of explicit game-profile files; never restores user saves.

This helper has no launcher, debugger, account-cache or optional dependencies.
Keep its backup directory under the caller's private repository work/ area.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil

MAX_FILE_BYTES = 256 * 1024 * 1024
MAX_FILES = 2500
MAX_DIRECTORIES = 2500


class ProfileChanged(RuntimeError):
    """A protected profile changed; retain newer files for manual assessment."""


def _plain(path: Path) -> Path:
    path = Path(path).absolute()
    for item in (path, *path.parents):
        if item.is_symlink() or (hasattr(item, "is_junction") and item.is_junction()):
            raise ValueError("Profile guards do not follow symbolic links or junctions")
    return path.resolve()


def _protected(path: Path) -> bool:
    return path.name.casefold() in ("options.ini", "profiles.xml") or path.suffix.casefold() in (".s7s", ".s7local")


def _record(path: Path) -> dict:
    path = _plain(path)
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        before = os.fstat(stream.fileno())
        if not 0 <= before.st_size <= MAX_FILE_BYTES:
            raise ValueError("Protected profile file exceeds the 256-MiB bound")
        size = 0
        while block := stream.read(1024 * 1024):
            size += len(block)
            if size > MAX_FILE_BYTES:
                raise ProfileChanged("Protected file grew beyond the read bound")
            digest.update(block)
        after = os.fstat(stream.fileno())
    current = path.stat()
    identity = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns)
    if identity(before) != identity(after) or identity(after) != identity(current) or size != after.st_size:
        raise ProfileChanged("Protected profile file changed while being read")
    return {"sha256": digest.hexdigest(), "size": size, "mtime_ns": after.st_mtime_ns}


def snapshot_profile(root: Path) -> dict:
    """Snapshot only options/profile metadata and save files under this root."""
    root = _plain(root)
    if not root.exists():
        return {"root_exists": False, "files": {}}
    if not root.is_dir():
        raise ValueError("A profile root must be a directory")
    files = {}
    directories = 0
    def fail(error):
        raise error
    for folder, children, names in os.walk(root, followlinks=False, onerror=fail):
        directories += 1
        if directories > MAX_DIRECTORIES:
            raise ValueError("Profile directory enumeration exceeds its bound")
        folder = _plain(Path(folder))
        # Reject linked directories rather than silently omit possible saves.
        for child in children:
            _plain(folder / child)
        for name in names:
            path = folder / name
            if not _protected(path):
                continue
            if len(files) >= MAX_FILES:
                raise ValueError("Protected profile file count exceeds its bound")
            files[path.relative_to(root).as_posix()] = _record(path)
    return {"root_exists": True, "files": files}


class ProfileGuard:
    """Capture once, verify shared profiles unchanged; do not roll back progress."""

    def __init__(self, named_roots: dict[str, Path], backup_dir: Path):
        if not isinstance(named_roots, dict) or not named_roots or any(
            not isinstance(label, str) or not re.fullmatch(r"[a-z][a-z0-9_-]{0,31}", label)
            for label in named_roots
        ):
            raise ValueError("Use unique short ASCII profile labels")
        self.roots = {label: _plain(root) for label, root in named_roots.items()}
        self.backup_dir = _plain(backup_dir)
        for root in self.roots.values():
            if self.backup_dir == root or self.backup_dir.is_relative_to(root) or root.is_relative_to(self.backup_dir):
                raise ValueError("Backups must be separate from all protected profile roots")
        self.before: dict | None = None

    def capture(self) -> dict:
        if self.before is not None or self.backup_dir.exists():
            raise ValueError("Use a fresh backup directory and capture only once")
        before = {name: snapshot_profile(root) for name, root in self.roots.items()}
        self.backup_dir.mkdir()
        for name, state in before.items():
            for relative, expected in state["files"].items():
                source = _plain(self.roots[name] / relative)
                if _record(source) != expected:
                    raise ProfileChanged("Protected profile changed before backup")
                target = self.backup_dir / name / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
                if _record(source) != expected or _record(target) != expected:
                    raise ProfileChanged("Protected profile changed during backup")
        if before != {name: snapshot_profile(root) for name, root in self.roots.items()}:
            raise ProfileChanged("Protected file set changed during backup")
        (self.backup_dir / "snapshot.private.json").write_text(json.dumps(before, indent=2) + "\n", encoding="utf-8")
        self.before = before
        return before

    def changes(self) -> dict:
        if self.before is None:
            raise ValueError("Capture profiles before checking for changes")
        result = {}
        for name, root in self.roots.items():
            before = self.before[name]
            after = snapshot_profile(root)
            if before != after:
                old, new = before["files"], after["files"]
                result[name] = {
                    "root_existence_changed": before["root_exists"] != after["root_exists"],
                    "added": sorted(new.keys() - old.keys()),
                    "removed": sorted(old.keys() - new.keys()),
                    "changed": sorted(key for key in old.keys() & new.keys() if old[key] != new[key]),
                }
        return result

    def verify_unchanged(self) -> dict:
        changes = self.changes()
        if changes:
            raise ProfileChanged("Protected profiles changed; keep current files and the private backup for assessment")
        return {"protected_profiles_unchanged": True, "profile_roots": len(self.roots)}
