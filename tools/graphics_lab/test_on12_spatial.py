"""Admission and lifecycle checks; no remote worker or real game is started."""
import json
from pathlib import Path
import struct
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from . import on12_spatial as spatial


class Tests(unittest.TestCase):
    def test_unrelated_backend_rejected_before_launch(self):
        from . import game, dxwrapper
        with self.assertRaises(ValueError):
            game.run(Path('unused'), Path('unused'), on12_spatial_build=Path('unused'))
        with self.assertRaises(ValueError):
            dxwrapper.Session(Path('unused'), Path('unused'), 'native', on12_spatial_build=Path('unused'))

    def test_command_bounds_and_explicit_boolean(self):
        for sequence, enabled in ((0, True), (True, True), (0x100000000, True), (1, 1)):
            with self.subTest(sequence=sequence, enabled=enabled), self.assertRaises(ValueError):
                spatial.command(Path('unused'), sequence, enabled)
        with TemporaryDirectory(dir=spatial.ROOT / 'work') as temporary:
            case = Path(temporary)
            spatial.command(case, 3, False)
            self.assertEqual((case / 'on12-spatial.private.command').read_bytes(), struct.pack('<4I', 16, 1, 3, 0))

    def test_report_freshness_and_fixed_abi(self):
        with TemporaryDirectory(dir=spatial.ROOT / 'work') as temporary:
            report = Path(temporary) / 'on12-spatial-live.private.json'
            value = spatial.configuration(report)
            self.assertEqual(len(value), 1036)
            self.assertEqual(struct.unpack_from('<3I', value), (1036, 1, 1))
            self.assertEqual(value[12:].decode('utf-16le').rstrip('\0'), str(report))
            report.write_bytes(b'{}')
            with self.assertRaises(ValueError):
                spatial.configuration(report)

    def test_wait_for_detach_and_initialize_exactly_once(self):
        with TemporaryDirectory(dir=spatial.ROOT / 'work') as temporary:
            case = Path(temporary)
            session = object.__new__(spatial.Session)
            session.prepared = True
            session.initialized = False
            session.case = case
            session.executable = case / 'Settlers7R.exe'
            session.directory = case / 'build'
            session.config = b'fixed'
            session.build = {'files': {spatial.DLL: 'source-bound'}}
            owned = SimpleNamespace(executable=session.executable)
            report = dict(snapshot_done=False)
            path = case / 'water-probe-live.private.json'
            with patch.object(spatial.loader, '_invoke_fixed', return_value={'succeeded': True}) as invoke:
                session.observe(owned)
                path.write_text(json.dumps(report))
                session.observe(owned)
                invoke.assert_not_called()
                report.update(snapshot_done=True, failure=0, attached=True, on12_query_hr=0, snapshot_once=True,
                              hooks_detached=False, pow_cap_requested=False, caps_applied=0)
                path.write_text(json.dumps(report))
                with self.assertRaises(RuntimeError):
                    session.observe(owned)
                invoke.assert_not_called()
                report['hooks_detached'] = True
                path.write_text(json.dumps(report))
                session.observe(owned)
                session.observe(owned)
                invoke.assert_called_once_with(owned, session.directory / spatial.DLL, b'fixed', 'on12_spatial')
                self.assertTrue(session.initialized)


if __name__ == '__main__':
    unittest.main()
