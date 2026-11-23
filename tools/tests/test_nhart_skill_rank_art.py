"""Selected skill-rank package, preservation and corruption controls."""
import copy
import hashlib
import json
from pathlib import Path
import unittest
from .nhart_skill_rank_art import skill_rank_wave, validate_skill_rank_art
from .nhart_test_resources import later_art_waves, entries_before_wave, read_resource
from .nhart_released_history import baseline_payload_proof

ROOT = Path(__file__).resolve().parents[2]


class SkillRankArtPackageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.waves = later_art_waves()
        cls.wave = skill_rank_wave(cls.waves)
        cls.entries = json.loads((ROOT / 'assets/new-horizons/runtime-art-manifest.json').read_text())['entries']
        cls.selection = (ROOT / 'assets/new-horizons/skill-rank-art-selections.json').read_bytes()

    def test_exact_132_selected_exports_preserve_every_previous_payload_and_record(self):
        validate_skill_rank_art(self.wave, self.entries, read_resource, self.selection)
        before = entries_before_wave(self.entries, self.waves, self.waves.index(self.wave))
        canonical = json.dumps(sorted(before, key=lambda row: row['resource'].encode()),
                               sort_keys=True, separators=(',', ':')).encode()
        self.assertEqual(len(before), self.wave['baselineCount'])
        self.assertEqual(hashlib.sha256(canonical).hexdigest(), self.wave['baselineEntriesSha256'])
        self.assertEqual(self.wave['entryCount'], len(before) + 132)
        for entry in before:
            # A later selected repaint keeps only a typed proof of the replaced payload.
            self.assertIn(baseline_payload_proof(entry, read_resource(entry['resource'])),
                          ('verified-matching-payload', 'verified-selected-replacement'))

    def test_changed_wave_provenance_missing_or_duplicate_selection_is_rejected(self):
        for field in ('baselineArchiveSha256', 'newArchiveSha256', 'resources', 'approval'):
            changed = copy.deepcopy(self.wave)
            changed[field] = 'changed'
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_skill_rank_art(changed, self.entries, read_resource, self.selection)
        for waves in ([], [self.wave, self.wave]):
            with self.assertRaises(ValueError):
                skill_rank_wave(waves)
        with self.assertRaises(ValueError):
            validate_skill_rank_art(self.wave, self.entries, read_resource, self.selection + b' ')
        changed = copy.deepcopy(self.entries)
        next(row for row in changed if row['resource'] == self.wave['resources'][0])['origin'] = 'changed'
        with self.assertRaises(ValueError):
            validate_skill_rank_art(self.wave, changed, read_resource, self.selection)

    def test_each_native_size_rejects_changed_selected_image_bytes(self):
        for role in ('small', 'medium', 'large', 'scenarioBonus'):
            name = next(value for value in self.wave['resources'] if value.endswith('_' + role + '.png'))
            def corrupt(resource):
                return read_resource(resource) + (b'changed' if resource == name else b'')
            with self.subTest(role=role), self.assertRaises(ValueError):
                validate_skill_rank_art(self.wave, self.entries, corrupt, self.selection)
