"""Retired custom Spells/Cancel buttons: absent from pack and code, exact in history."""
import copy
import json
from pathlib import Path
import unittest
from .nhart_retired_custom_buttons import (RESOURCES, custom_button_retirement_wave,
    validate_custom_button_retirement)
from .nhart_retired_interface_icons import retired_row_proof
from .nhart_test_resources import entries_before_wave, later_art_waves, verified_index

MANIFEST = Path(__file__).resolve().parents[2] / 'assets/new-horizons/runtime-art-manifest.json'


class RetiredCustomButtonsTest(unittest.TestCase):
    def setUp(self):
        self.waves = later_art_waves()
        self.wave = custom_button_retirement_wave(self.waves)
        self.entries = json.loads(MANIFEST.read_text())['entries']

    def test_retired_buttons_are_gone_and_restored_in_history(self):
        validate_custom_button_retirement(self.wave, self.entries, verified_index())
        before = entries_before_wave(self.entries, self.waves, self.waves.index(self.wave))
        self.assertTrue(set(RESOURCES) <= {row['resource'] for row in before})
        self.assertTrue(all(retired_row_proof(row) for row in self.wave['retiredEntries']))
        # The flat originals that the repaint replaced reach the retirement through their successor.
        flat = [r for wave in self.waves for r in wave.get('replacedEntries', [])
                if r['current']['resource'] in RESOURCES]
        self.assertEqual(len(flat), 8)
        self.assertTrue(all(retired_row_proof(r['previous']) for r in flat))

    def test_reshipped_or_changed_retirement_is_rejected(self):
        changed = copy.deepcopy(self.wave); changed['retiredEntries'] = changed['retiredEntries'][1:]
        with self.assertRaises(ValueError):
            custom_button_retirement_wave([changed])
        with self.assertRaises(ValueError):
            validate_custom_button_retirement(self.wave, self.entries + [self.wave['retiredEntries'][0]], verified_index())
