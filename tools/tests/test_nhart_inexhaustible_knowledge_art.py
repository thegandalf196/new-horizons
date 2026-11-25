"""Theodorus's Inexhaustible Knowledge specialty icon: exact wave, provenance and native pixels."""
import copy
import json
from pathlib import Path
import unittest
from .nhart_inexhaustible_knowledge_art import (
    EXPECTED, inexhaustible_knowledge_wave, validate_inexhaustible_knowledge)
from .nhart_test_resources import later_art_waves, read_resource

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / 'assets/new-horizons/runtime-art-manifest.json'


def manifest_entries():
    return json.loads(MANIFEST.read_text())['entries']


class InexhaustibleKnowledgeArtTest(unittest.TestCase):
    def test_exact_selected_native_resources(self):
        wave = inexhaustible_knowledge_wave(later_art_waves())
        validate_inexhaustible_knowledge(wave, manifest_entries(), read_resource)
        self.assertEqual(wave['entryCount'] - wave['baselineCount'], 4)
        self.assertEqual(set(wave['resources']), EXPECTED)

    def test_incomplete_wave_or_changed_pixels_are_rejected(self):
        wave = inexhaustible_knowledge_wave(later_art_waves())
        for field in ('resources', 'baselineEntriesSha256', 'newArchiveSha256'):
            changed = copy.deepcopy(wave); changed[field] = []
            with self.subTest(field=field), self.assertRaises(ValueError):
                inexhaustible_knowledge_wave([changed])
        for waves in ([], [wave, wave]):
            with self.assertRaises(ValueError):
                inexhaustible_knowledge_wave(waves)
        with self.assertRaises(ValueError):
            validate_inexhaustible_knowledge(wave, manifest_entries(), lambda _: b'changed')
        entries = manifest_entries()
        for row in entries:
            if row['resource'] in EXPECTED:
                row['family'] = 'builtin-skills-perks-orders-and-ui'
        with self.assertRaises(ValueError):
            validate_inexhaustible_knowledge(wave, entries, read_resource)


if __name__ == '__main__':
    unittest.main()
