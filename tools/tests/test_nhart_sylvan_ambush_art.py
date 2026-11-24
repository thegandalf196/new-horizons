"""Sylvan Ambush artwork: exact wave, provenance, bindings, derived states and native pixels."""
import copy
import json
import unittest
from .nhart_sylvan_ambush_art import (EXPECTED, PERKS, ROOT, SELECTION, sylvan_ambush_wave,
                                      validate_bindings, validate_sylvan_ambush_art)
from .nhart_test_resources import later_art_waves, read_resource, verified_index

HEADER = 'client/windows/NewHorizonsPerkIcons.h'
HYBRID = 'client/windows/NewHorizonsSylvanHybridPerkIcons.h'


class SylvanAmbushArtTest(unittest.TestCase):
    def setUp(self):
        self.wave = sylvan_ambush_wave(later_art_waves())
        self.entries = list(verified_index().values())

    def test_exact_selected_native_resources(self):
        validate_sylvan_ambush_art(self.wave, self.entries, read_resource)
        self.assertEqual((self.wave['baselineCount'], self.wave['entryCount']), (6643, 6671))
        self.assertEqual(sorted(self.wave['resources']), sorted(EXPECTED))

    def test_incomplete_wave_or_changed_pixels_are_rejected(self):
        for field in ('resources', 'baselineEntriesSha256', 'newArchiveSha256'):
            changed = copy.deepcopy(self.wave); changed[field] = []
            with self.subTest(field=field), self.assertRaises(ValueError):
                sylvan_ambush_wave([changed])
        for waves in ([], [self.wave, self.wave]):
            with self.assertRaises(ValueError):
                sylvan_ambush_wave(waves)
        with self.assertRaises(ValueError):
            validate_sylvan_ambush_art(self.wave, self.entries, lambda _: b'changed')
        swapped = {'SPRITES/NH_perk_sylvan_tactics_startle_pressed.png':
                   read_resource('SPRITES/NH_perk_sylvan_tactics_startle_highlighted.png')}
        with self.assertRaises(ValueError):
            validate_sylvan_ambush_art(self.wave, self.entries, lambda name: swapped.get(name) or read_resource(name))

    def test_unbound_or_renamed_perk_icons_are_rejected(self):
        header, hybrid = ((ROOT / path).read_text() for path in (HEADER, HYBRID))
        selected = json.loads((ROOT / SELECTION).read_text())
        validate_bindings(header, hybrid, selected)
        for identity, (name, stem) in PERKS.items():
            for changed in ((header.replace(stem + '"}', 'NH_perk_neutral"}', 1), hybrid),
                            (header, hybrid.replace('"' + name + '", "' + stem, '"' + name.lower() + '", "' + stem))):
                with self.subTest(perk=identity), self.assertRaises(ValueError):
                    validate_bindings(*changed, selected)
        changed = copy.deepcopy(selected); changed['perkBindings'] = changed['perkBindings'][1:]
        with self.assertRaises(ValueError):
            validate_bindings(header, hybrid, changed)


if __name__ == '__main__':
    unittest.main()
