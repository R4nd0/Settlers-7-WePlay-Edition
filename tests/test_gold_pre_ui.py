"""Graphics isolation and fail-closed source guards, without game data."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools/gold' / (name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


hooks = module('reshade_hooks')
candidate = module('pre_ui_runtime')


class PreUiTests(unittest.TestCase):
    def test_candidate_cannot_mutate_gold_or_history_runtime_pins(self):
        gold = module('visual_runtime')
        import reshade_runtime as history
        original = (gold.runtime.CLOCK_SHA256, history.CLOCK_SHA256,
                    gold.runtime.ADDON_DLL_SHA256, history.ADDON_DLL_SHA256)
        a, b = candidate.visual(), candidate.visual()
        self.assertIsNot(a.runtime, b.runtime)
        a.runtime.CLOCK_SHA256 = 'changed'
        self.assertEqual(b.runtime.CLOCK_SHA256, candidate.ADDON_SHA256)
        self.assertEqual(original, (gold.runtime.CLOCK_SHA256, history.CLOCK_SHA256,
                                    gold.runtime.ADDON_DLL_SHA256, history.ADDON_DLL_SHA256))

    def test_renderer_configuration_is_child_local_and_rejects_unknown_overrides(self):
        inherited = {'KEEP': 'yes'}
        visual = mock.Mock()
        visual.launch_environment.side_effect = lambda root, env, options: dict(env)
        with mock.patch.object(candidate, 'visual', return_value=visual):
            env = candidate.launch_environment('unused', inherited)
            self.assertEqual(inherited, {'KEEP': 'yes'})
            self.assertEqual(env['KEEP'], 'yes')
            self.assertEqual(env['DXVK_CONFIG'], candidate.DXVK_CONFIG)
            with self.assertRaisesRegex(ValueError, 'Unqualified'):
                candidate.launch_environment('unused', {'DXVK_CONFIG': 'unknown'})
            self.assertEqual(visual.launch_environment.call_count, 1)

    def test_patch_requires_reviewed_bytes_and_exact_base_before_modification(self):
        self.assertEqual(hooks.digest(hooks.PATCH.read_bytes()), hooks.PATCH_SHA256)
        (ROOT / 'work').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT / 'work', prefix='gold-pre-ui-fixture-') as temporary:
            source = Path(temporary)
            with mock.patch.object(hooks, 'git', return_value='wrong') as git:
                with self.assertRaisesRegex(ValueError, 'exact ReShade'):
                    hooks.verify(source, patched=False)
                self.assertEqual(git.call_count, 1)
            for name in hooks.FILES:
                path = source / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'unknown source')
            before = {name: (source / name).read_bytes() for name in hooks.FILES}
            with mock.patch.object(hooks, 'git', return_value=hooks.BASE) as git:
                with self.assertRaisesRegex(ValueError, 'Unexpected original'):
                    hooks.apply(source)
                self.assertEqual(git.call_count, 1)  # No git apply reached.
            self.assertEqual(before, {name: (source / name).read_bytes() for name in hooks.FILES})

    def test_source_hashes_tolerate_only_line_endings(self):
        self.assertEqual(hooks.digest(b'a\r\nb\r\n'), hooks.digest(b'a\nb\n'))
        self.assertNotEqual(hooks.digest(b'a\nb\n'), hooks.digest(b'a\nb \n'))


if __name__ == '__main__':
    unittest.main()
