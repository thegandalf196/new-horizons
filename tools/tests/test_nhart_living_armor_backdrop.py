"""Package-only checks and mutation controls for the new creature backdrop."""
import copy
import unittest
from .nhart_living_armor_backdrop import backdrop_wave, validate_backdrop
from .nhart_test_resources import later_art_waves, read_resource, verified_index


class LivingArmorBackdropTest(unittest.TestCase):
    def test_selected_package_contains_both_native_opaque_canvases(self):
        wave = backdrop_wave(later_art_waves())
        validate_backdrop(wave, list(verified_index().values()), read_resource)
        self.assertEqual(wave['baselineCount'], 6560)
        self.assertEqual(wave['entryCount'], 6563)

    def test_changed_wave_missing_resource_or_pixels_are_rejected(self):
        wave = backdrop_wave(later_art_waves())
        for field in ('resources', 'baselineEntriesSha256', 'newArchiveSha256'):
            changed = copy.deepcopy(wave); changed[field] = []
            with self.subTest(field=field), self.assertRaises(ValueError):
                backdrop_wave([changed])
        for waves in ([], [wave, wave]):
            with self.assertRaises(ValueError):
                backdrop_wave(waves)
        with self.assertRaises(ValueError):
            validate_backdrop(wave, list(verified_index().values()), lambda _: b'changed')
