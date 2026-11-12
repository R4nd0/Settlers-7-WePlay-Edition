"""Controller handshake regressions; native execution is tested separately."""
import json
from contextlib import nullcontext
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch
import debugger
from profiles import STEAM

ROOT = Path(__file__).resolve().parents[2]


class Script:
    def __init__(self, events, failure=None):
        self.events, self.failure = events, failure

    def on(self, kind, callback):
        self.message = callback

    def load(self):
        if self.failure:
            raise self.failure
        for event in self.events:
            self.message(dict(type='send', payload=event), None)


class Session:
    def __init__(self, events, failure=None):
        self.script = Script(events, failure)
        self.was_detached = False

    def on(self, kind, callback):
        self.detached = callback

    def create_script(self, source):
        self.source = source
        return self.script

    def detach(self):
        self.was_detached = True
        self.detached()


class Startup(unittest.TestCase):
    def run_adapter(self, events, *, failure=None, clock=None):
        session = Session(events, failure)
        selected = (ROOT/'work/mock.exe', [], dict(version='history-steam', slot='D'), STEAM)
        with tempfile.TemporaryDirectory(dir=ROOT/'work') as directory, \
                patch.dict(sys.modules, frida=types.SimpleNamespace(attach=lambda pid: session)), \
                patch.object(debugger, 'validate_target', return_value=selected):
            with patch.object(debugger.time, 'monotonic', side_effect=clock) if clock else nullcontext():
                try:
                    adapter = debugger.Adapter(selected[0], 123, Path(directory)/'trace.jsonl')
                except BaseException:
                    self.assertTrue(session.was_detached)
                    raise
                session.detach()
                adapter.close_after_restored()
                self.assertTrue(adapter.closed)
                return [json.loads(line) for line in (Path(directory)/'trace.jsonl').read_text().splitlines()]

    def test_ready_alone(self):
        self.assertEqual(self.run_adapter([dict(event='ready')]), [dict(event='ready')])

    def test_local_boundary_precedes_ready(self):
        events = [dict(event='local-steam-boundary-observed'), dict(event='ready')]
        self.assertEqual(self.run_adapter(events), events)

    def test_unavailable_diagnostic_precedes_ready(self):
        events = [dict(event='steam-observer-unavailable', api='SteamAPI_Init'), dict(event='ready')]
        self.assertEqual(self.run_adapter(events), events)

    def test_sdk_result_precedes_ready(self):
        events = [dict(event='steam-startup-result', result=False), dict(event='ready')]
        self.assertEqual(self.run_adapter(events), events)

    def test_fatal_agent_error_still_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'initialization rejected'):
            self.run_adapter([dict(event='agent-script-error'), dict(event='ready')])

    def test_unknown_event_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'initialization rejected'):
            self.run_adapter([dict(event='unexpected'), dict(event='ready')])

    def test_script_load_failure_detaches(self):
        with self.assertRaisesRegex(ValueError, 'native guard'):
            self.run_adapter([], failure=ValueError('native guard'))

    def test_diagnostics_cannot_extend_deadline(self):
        with self.assertRaisesRegex(RuntimeError, 'initialization rejected'):
            self.run_adapter([dict(event='local-steam-boundary-observed')], clock=[0, 11, 11])


if __name__ == '__main__':
    unittest.main()
