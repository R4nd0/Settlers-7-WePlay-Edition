import unittest

from tools.supersampling import plan


class SizingTests(unittest.TestCase):
    def test_1080p_2x_is_4k_and_four_times_the_pixels(self):
        result = plan.create(1920, 1080, '2')
        self.assertEqual((result.render_width, result.render_height), (3840, 2160))
        self.assertEqual(result.pixel_multiplier, 4)
        self.assertEqual(result.color_surface_bytes, 33177600)
        self.assertFalse(result.game_qualified)

    def test_1440p_1_5x_is_4k(self):
        result = plan.create(2560, 1440, '1.5')
        self.assertEqual((result.render_width, result.render_height), (3840, 2160))
        self.assertEqual(result.pixel_multiplier, 2.25)

    def test_1440p_2x_exceeds_32bit_experiment_budget(self):
        with self.assertRaisesRegex(ValueError, 'cap'):
            plan.create(2560, 1440, '2')

    def test_rejects_rounding_that_would_change_exact_aspect_ratio(self):
        with self.assertRaisesRegex(ValueError, 'integral'):
            plan.create(1921, 1080, '1.5')

    def test_rejects_coercions_and_unbounded_scales(self):
        for args in ((True, 1080, '2'), (1920.0, 1080, '2'),
                     (1920, 1080, 2), (1920, 1080, '100'),
                     (0, 0, '1'), (3840, 2161, '1')):
            with self.subTest(args=args), self.assertRaises(ValueError):
                plan.create(*args)


if __name__ == '__main__':
    unittest.main()
