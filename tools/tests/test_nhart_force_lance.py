"""Selected Force Lance payload, provenance and corruption controls."""
import copy
import hashlib
import json
from pathlib import Path
import unittest

from .nhart_force_lance import force_lance_wave, validate_force_lance
from .nhart_test_resources import later_art_waves, read_resource, entries_before_wave
from .nhart_released_history import baseline_payload_proof

ROOT = Path(__file__).resolve().parents[2]


class ForceLanceArtTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.waves = later_art_waves()
        cls.wave = force_lance_wave(cls.waves)
        cls.entries = json.loads((ROOT / 'assets/new-horizons/runtime-art-manifest.json').read_text())['entries']
        cls.selection = (ROOT / 'assets/new-horizons/force-lance-art-selections.json').read_bytes()

    def test_exact_native_assets_and_preserved_baseline(self):
        validate_force_lance(self.wave, self.entries, read_resource, self.selection)
        before = entries_before_wave(self.entries, self.waves, self.waves.index(self.wave))
        canonical = json.dumps(sorted(before, key=lambda row: row['resource'].encode()),
                               sort_keys=True, separators=(',', ':')).encode()
        self.assertEqual(len(before), 6252)
        self.assertEqual(hashlib.sha256(canonical).hexdigest(), self.wave['baselineEntriesSha256'])
        for entry in before:
            # A later selected repaint keeps only a typed proof of the replaced payload.
            self.assertIn(baseline_payload_proof(entry, read_resource(entry['resource'])),
                          ('verified-matching-payload', 'verified-selected-replacement'))
        selected = json.loads(self.selection)
        self.assertEqual(selected['nativeTipAnchor'], [110, 138])
        self.assertEqual(selected['mirroredTipAnchor'], [49, 138])
        self.assertIn('not Final', selected['approval'])
        self.assertEqual(len(self.wave['resources']), 7)

    def test_unknown_missing_duplicate_or_relabelled_selection_is_rejected(self):
        for field in ('baselineArchiveSha256', 'newArchiveSha256', 'resources', 'approval'):
            changed = copy.deepcopy(self.wave)
            changed[field] = 'unreviewed'
            with self.assertRaises(ValueError):
                validate_force_lance(changed, self.entries, read_resource, self.selection)
        for waves in ([], [self.wave, self.wave]):
            with self.assertRaises(ValueError):
                force_lance_wave(waves)
        for field, value in (('approval', 'Final'), ('nativeTipAnchor', [80, 80])):
            changed = json.loads(self.selection)
            changed[field] = value
            with self.assertRaises(ValueError):
                validate_force_lance(self.wave, self.entries, read_resource, json.dumps(changed).encode())

    def test_changed_provenance_and_each_payload_are_rejected(self):
        for name in self.wave['resources']:
            changed = copy.deepcopy(self.entries)
            next(row for row in changed if row['resource'] == name)['origin'] = 'unreviewed'
            with self.assertRaises(ValueError):
                validate_force_lance(self.wave, changed, read_resource, self.selection)
            def corrupt(resource):
                return read_resource(resource) + (b'changed' if resource == name else b'')
            with self.assertRaises(ValueError):
                validate_force_lance(self.wave, self.entries, corrupt, self.selection)
