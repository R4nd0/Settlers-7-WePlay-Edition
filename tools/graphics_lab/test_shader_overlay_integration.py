"""Explicit overlay intent and frozen build/command boundaries, no game access."""
from contextlib import nullcontext
from copy import deepcopy
from pathlib import Path
import struct
import unittest
from unittest.mock import patch
from tools.graphics_lab import game, load_hook, qualification
from tools.supersampling import hook_probe


class ShaderOverlayIntegrationTests(unittest.TestCase):
    def test_overlay_requires_boolean_and_water_palette_before_io(self):
        for kwargs in ({'shader_overlay': 1}, {'shader_overlay': True},
                       {'shader_overlay': True, 'leaf_palette': True}):
            with self.subTest(kwargs=kwargs), patch.object(game.platform, 'load') as load:
                with self.assertRaisesRegex(ValueError, 'Shader overlay requires'):
                    game.run(Path('not-read'), Path('not-created'), **kwargs)
                load.assert_not_called()

    def test_overlay_has_fixed_typed_diagnostic_export(self):
        dll=Path('not-opened.dll')
        with patch.object(load_hook, 'verify_build', return_value=(dll, {'diagnostic_abi': {'overlay': 12}})), \
             patch.object(load_hook, 'file_lease', return_value=nullcontext()), \
             patch.object(load_hook, '_invoke_pinned', return_value={'accepted': True}) as invoke:
            for enabled in (0,1):
                payload=struct.pack('<3I', 12, 1, enabled)
                self.assertEqual(load_hook.invoke(None,dll,'overlay',payload), {'accepted': True})
                invoke.assert_called_with(None,dll,payload,b'WePlayNativeGraphicsConfigureOverlay\0')
        with patch.object(load_hook, 'verify_build', return_value=(dll, {'diagnostic_abi': {}})):
            with self.assertRaisesRegex(ValueError,'shader overlay'):
                load_hook.invoke(None,dll,'overlay',struct.pack('<3I',12,1,1))
        for payload in (struct.pack('<3I',12,1,2),struct.pack('<3I',12,2,1),bytes(8)):
            with self.subTest(payload=payload),patch.object(load_hook,'verify_build') as verify:
                with self.assertRaises(ValueError):load_hook.invoke(None,dll,'overlay',payload)
                verify.assert_not_called()

    def test_source45_preserves_main_abi_and_old43_contract(self):
        build=dict(format='s7-native-graphics-build-v2',source_set='effects-v12',compiled_capabilities=15,
                   abi=deepcopy(hook_probe.BUILD_ABI_WATER_PALETTE),
                   diagnostic_abi={'comparison':12,'shadow':12,'ambient':12,'ambient_v2':12,'overlay':12},
                   build_sha256='a'*64,files={})
        sources,_,valid=qualification._build_contract(build)
        self.assertTrue(valid);self.assertEqual(len(sources),45)
        self.assertEqual(sources,set(hook_probe.OVERLAY_SOURCES))
        broken=deepcopy(build);del broken['diagnostic_abi']['overlay']
        self.assertFalse(qualification._build_contract(broken)[2])
        old=deepcopy(broken);old['source_set']='effects-v11'
        self.assertTrue(qualification._build_contract(old)[2])
        self.assertEqual(len(qualification._build_contract(old)[0]),43)


if __name__=='__main__':unittest.main()
