"""Retired unused flat interface icons: absent from the pack, exact in history."""
import copy
import json
from pathlib import Path
import unittest
from .nhart_retired_interface_icons import COUNT, retired_row_proof, retirement_wave, validate_retirement
from .nhart_test_resources import entries_before_wave, later_art_waves, verified_index

MANIFEST = Path(__file__).resolve().parents[2] / 'assets/new-horizons/runtime-art-manifest.json'


class RetiredInterfaceIconsTest(unittest.TestCase):
    def setUp(self):
        self.waves = later_art_waves()
        self.wave = retirement_wave(self.waves)
        self.entries = json.loads(MANIFEST.read_text())['entries']

    def test_retired_rows_are_gone_but_restored_in_history(self):
        validate_retirement(self.wave, self.entries, verified_index())
        before = entries_before_wave(self.entries, self.waves, self.waves.index(self.wave))
        names = {row['resource'] for row in self.wave['retiredEntries']}
        self.assertEqual(len(names), COUNT)
        self.assertTrue(names <= {row['resource'] for row in before})
        self.assertTrue(all(retired_row_proof(row) for row in self.wave['retiredEntries']))
        changed = dict(self.wave['retiredEntries'][0], sha256='0' * 64)
        self.assertFalse(retired_row_proof(changed))

    def test_reshipped_or_changed_retirement_is_rejected(self):
        changed = copy.deepcopy(self.wave); changed['retiredEntries'] = changed['retiredEntries'][1:]
        with self.assertRaises(ValueError):
            retirement_wave([changed])
        with self.assertRaises(ValueError):
            validate_retirement(self.wave, self.entries + [self.wave['retiredEntries'][0]], verified_index())
        with self.assertRaises(ValueError):
            entries_before_wave(self.entries + [self.wave['retiredEntries'][0]], self.waves, self.waves.index(self.wave))
