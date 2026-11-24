"""Owner-selected spell school badge medallions: exact wave, provenance and native pixels."""
import copy
import unittest
from .nhart_spell_school_badges import EXPECTED, school_badge_wave, validate_school_badges
from .nhart_test_resources import later_art_waves, read_resource, verified_index


class SpellSchoolBadgeArtTest(unittest.TestCase):
    def test_exact_selected_native_resources(self):
        wave = school_badge_wave(later_art_waves())
        validate_school_badges(wave, list(verified_index().values()), read_resource)
        self.assertEqual((wave['baselineCount'], wave['entryCount']), (6671, 6677))
        self.assertEqual(sorted(wave['resources']), sorted(EXPECTED))

    def test_incomplete_wave_or_changed_pixels_are_rejected(self):
        wave = school_badge_wave(later_art_waves())
        for field in ('resources', 'baselineEntriesSha256', 'newArchiveSha256'):
            changed = copy.deepcopy(wave); changed[field] = []
            with self.subTest(field=field), self.assertRaises(ValueError):
                school_badge_wave([changed])
        for waves in ([], [wave, wave]):
            with self.assertRaises(ValueError):
                school_badge_wave(waves)
        with self.assertRaises(ValueError):
            validate_school_badges(wave, list(verified_index().values()), lambda _: b'changed')
