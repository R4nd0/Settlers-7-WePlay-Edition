"""Synthetic filesystem checks; no game or real Documents profile is opened."""
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.shadow_detail import profile_guard as guard


class ProfileGuardTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.profile = self.base / "profile"
        (self.profile / "Savegames").mkdir(parents=True)
        (self.profile / "Options.ini").write_bytes(b"[Quality]\nFullScreen=0\n")
        (self.profile / "Profiles.xml").write_bytes(b"<profiles/>")
        self.save = self.profile / "Savegames/s1.s7s"
        self.save.write_bytes(b"synthetic older save")

    def test_capture_preserves_hashes_and_timestamps_and_reads_only_allowlisted_files(self):
        cache = self.profile / "AccountCache.dat"
        cache.write_bytes(b"must not be opened")
        item = guard.ProfileGuard({"shared": self.profile}, self.base / "backup")
        original = Path.open
        def restricted(path, *args, **kwargs):
            if path == cache:
                raise AssertionError("unrelated cache was read")
            return original(path, *args, **kwargs)
        with patch.object(Path, "open", restricted):
            before = item.capture()
            self.assertEqual(item.verify_unchanged(), {"protected_profiles_unchanged": True, "profile_roots": 1})
        self.assertEqual(len(before["shared"]["files"]), 3)
        for relative, expected in before["shared"]["files"].items():
            self.assertEqual(guard._record(self.base / "backup/shared" / relative), expected)
        self.assertEqual(json.loads((self.base / "backup/snapshot.private.json").read_text()), before)

    def test_changed_save_is_detected_and_never_replaced_by_backup(self):
        item = guard.ProfileGuard({"shared": self.profile}, self.base / "backup")
        item.capture()
        self.save.write_bytes(b"synthetic newer save; preserve me")
        with self.assertRaises(guard.ProfileChanged):
            item.verify_unchanged()
        self.assertEqual(item.changes()["shared"]["changed"], ["Savegames/s1.s7s"])
        self.assertEqual(self.save.read_bytes(), b"synthetic newer save; preserve me")

    def test_added_removed_and_timestamp_changes_are_all_reported(self):
        item = guard.ProfileGuard({"shared": self.profile}, self.base / "backup")
        item.capture()
        (self.profile / "Profiles.xml").unlink()
        (self.profile / "Savegames/s2.s7local").write_bytes(b"new metadata")
        options = self.profile / "Options.ini"
        stat = options.stat()
        os.utime(options, ns=(stat.st_atime_ns, stat.st_mtime_ns + 1_000_000_000))
        changes = item.changes()["shared"]
        self.assertEqual(changes["added"], ["Savegames/s2.s7local"])
        self.assertEqual(changes["removed"], ["Profiles.xml"])
        self.assertEqual(changes["changed"], ["Options.ini"])

    def test_missing_profile_becoming_created_is_detected(self):
        missing = self.base / "missing"
        item = guard.ProfileGuard({"shared": missing}, self.base / "backup")
        item.capture()
        missing.mkdir()
        self.assertTrue(item.changes()["shared"]["root_existence_changed"])

    def test_refuses_unsafe_backup_reuse_paths_labels_and_read_bounds(self):
        with self.assertRaises(ValueError):
            guard.ProfileGuard({"../escape": self.profile}, self.base / "backup")
        with self.assertRaises(ValueError):
            guard.ProfileGuard({"shared": self.profile}, self.profile / "backup")
        item = guard.ProfileGuard({"shared": self.profile}, self.base / "backup")
        with self.assertRaises(ValueError):
            item.changes()
        item.capture()
        with self.assertRaises(ValueError):
            item.capture()
        with patch.object(guard, "MAX_FILE_BYTES", 1), self.assertRaises(ValueError):
            guard.snapshot_profile(self.profile)

    def test_changed_source_during_copy_cannot_create_a_valid_checkpoint(self):
        item = guard.ProfileGuard({"shared": self.profile}, self.base / "backup")
        original = guard.shutil.copy2
        def changing(source, target):
            original(source, target)
            source.write_bytes(b"changed during copy")
        with patch.object(guard.shutil, "copy2", changing), self.assertRaises(guard.ProfileChanged):
            item.capture()
        self.assertIsNone(item.before)
        self.assertFalse((self.base / "backup/snapshot.private.json").exists())

    def test_unreadable_subdirectory_is_not_silently_omitted(self):
        def denied(root, *, followlinks, onerror):
            onerror(PermissionError("synthetic access denied"))
            return iter(())
        with patch.object(guard.os, "walk", denied), self.assertRaises(PermissionError):
            guard.snapshot_profile(self.profile)


if __name__ == "__main__":
    unittest.main()
