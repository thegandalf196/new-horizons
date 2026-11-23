"""Owner-approved lean glyphs and status icons: exact wave, provenance and native pixels."""
import copy
import unittest
from .nhart_interface_status_art import EXPECTED, interface_status_wave, validate_interface_status_art
from .nhart_test_resources import later_art_waves, read_resource, verified_index


class InterfaceStatusArtTest(unittest.TestCase):
    def test_exact_selected_native_resources(self):
        wave = interface_status_wave(later_art_waves())
        validate_interface_status_art(wave, list(verified_index().values()), read_resource)
        self.assertEqual((wave['baselineCount'], wave['entryCount']), (6596, 6602))
        self.assertEqual(sorted(wave['resources']), sorted(EXPECTED))

    def test_incomplete_wave_or_changed_pixels_are_rejected(self):
        wave = interface_status_wave(later_art_waves())
        for field in ('resources', 'baselineEntriesSha256', 'newArchiveSha256'):
            changed = copy.deepcopy(wave); changed[field] = []
            with self.subTest(field=field), self.assertRaises(ValueError):
                interface_status_wave([changed])
        for waves in ([], [wave, wave]):
            with self.assertRaises(ValueError):
                interface_status_wave(waves)
        with self.assertRaises(ValueError):
            validate_interface_status_art(wave, list(verified_index().values()), lambda _: b'changed')
