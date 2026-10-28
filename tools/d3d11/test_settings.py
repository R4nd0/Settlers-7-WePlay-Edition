import unittest
import settings


class SettingsTests(unittest.TestCase):
    def test_only_allowlisted_graphics_change_and_encoding_survives(self):
        text = '; original\r\n[Sound]\r\nVolume = 37\r\n[Quality]\r\nTextureQuality = 0\r\nScreenWidth = 1920\r\n[Effects]\r\nSSAO = 0\r\nFog = 1\r\n'
        for encoding in ('utf-8', 'utf-8-sig', 'utf-16'):
            prefs = dict(settings.DEFAULTS, TextureQuality=2, DistanceCulling=2, SSAO=1)
            raw = settings.replace_game_values(text.encode(encoding), prefs)
            actual = raw.decode(encoding)
            self.assertIn('; original\r\n[Sound]\r\nVolume = 37\r\n', actual)
            self.assertIn('ScreenWidth = 1920\r\n', actual)
            self.assertIn('Fog = 1\r\n', actual)
            reread = settings.read_defaults(raw)
            self.assertEqual(reread['TextureQuality'], 2)
            self.assertEqual(reread['DistanceCulling'], 2)
            self.assertEqual(reread['SSAO'], 1)
            self.assertEqual(raw, settings.replace_game_values(raw, prefs))

    def test_duplicate_sections_keys_and_invalid_values_are_rejected(self):
        for text in ('[Quality]\nTextureQuality=1\nTextureQuality=2\n',
                     '[Quality]\nTextureQuality=1\n[Quality]\n'):
            with self.assertRaises(ValueError):
                settings.replace_game_values(text.encode(), settings.DEFAULTS)
        for key, value in [('TextureQuality', 3), ('DistanceCulling', -1), ('SSAO', True),
                           ('FPS', 999), ('AA', 'FSR'), ('Filtering', 'global override')]:
            with self.assertRaises(ValueError):
                settings.validate(dict(settings.DEFAULTS, **{key: value}))

    def test_standard_aa_and_msaa_do_not_accidentally_stack(self):
        for aa, flag in [('off', 0), ('standard', 1), ('msaa2', 0), ('msaa4', 0), ('msaa8', 0)]:
            prefs = dict(settings.DEFAULTS, AA=aa)
            values = settings.game_values(prefs)
            self.assertEqual(values['Effects']['Antialiasing'], flag)
            self.assertNotIn('AA', values['Quality'])

    def test_wrapper_controls_change_directx_only(self):
        template = ('OutputAPI = bestavailable\nAdapters = all\nFPSLimit = 0\nVRAM = 256\n'
                    'dgVoodooWatermark = true\n[Glide]\nAntialiasing = off\n'
                    '[DirectX]\nAntialiasing = appdriven\nFiltering = appdriven\n')
        prefs = dict(settings.DEFAULTS, AA='msaa4', Filtering='16', FPS=120)
        output = settings.wrapper_configuration(template, 2, prefs).decode()
        self.assertIn('[Glide]\nAntialiasing = off\n', output)
        self.assertIn('[DirectX]\nAntialiasing = 4x\nFiltering = 16\n', output)
        self.assertIn('FPSLimit = 120', output)
        self.assertIn('Adapters = 2', output)


if __name__ == '__main__':
    unittest.main()
