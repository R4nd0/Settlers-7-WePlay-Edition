"""Renderer publication and recovery use only owned or absent targets."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/installer'), str(ROOT / 'tools/lan')]
import graphics_profile as graphics
import test_graphics_profile as fixtures


class GraphicsSwapTests(unittest.TestCase):
    snapshot = fixtures.GraphicsProfileTests.snapshot

    def setUp(self):
        fixtures.GraphicsProfileTests.setUp(self)

    def test_interrupted_graphics_file_write_does_not_poison_staging_directory(self):
        target = self.parent / 'new-preset.ini'
        original = graphics.tempfile.NamedTemporaryFile
        attempted = []
        def open_file(*args, **kwargs):
            stream = original(*args, **kwargs)
            attempted.append(Path(stream.name))
            flush = stream.flush
            def fail():
                flush()
                raise OSError('fixture write failure')
            stream.flush = fail
            return stream
        with patch.object(graphics.tempfile, 'NamedTemporaryFile', side_effect=open_file):
            with self.assertRaisesRegex(OSError, 'fixture write failure'):
                graphics.atomic_create(target, b'profile bytes', self.parent)
        self.assertFalse(target.exists())
        self.assertEqual(len(attempted), 1)
        self.assertFalse(attempted[0].exists())

    def test_native_renderer_recovery_preserves_concurrently_restored_foreign_dll(self):
        graphics.install(self.root, self.source)
        executable = self.root / graphics.EXE
        local = executable.with_name('d3d9.dll')
        package = self.parent / 'package'
        backup = package / 'Payload/Graphics/d3d9.dll'
        backup.parent.mkdir(parents=True)
        backup.write_bytes(self.dll)
        graphics.apply_launch_renderer(self.root, executable, package, 'native')
        self.assertFalse(local.exists())
        original = graphics._read
        foreign = b'independently installed wrapper'
        def read(path, limit):
            data = original(path, limit)
            if path == backup:
                local.write_bytes(foreign)
            return data
        with patch.object(graphics, '_read', side_effect=read):
            with self.assertRaises(FileExistsError):
                graphics.recover_launch_renderer(self.root, executable, package)
        self.assertEqual(local.read_bytes(), foreign)
        self.assertTrue((self.root / graphics.RENDERER_JOURNAL).is_file())


if __name__ == '__main__':
    unittest.main()
