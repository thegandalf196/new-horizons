"""Exact selected restoration and corruption controls, without authoring inputs."""
import copy
import json
from pathlib import Path
import unittest
from .nhart_test_resources import later_art_waves, read_resource, open_image
from .nhart_water_ice_restoration import (restoration_wave, validate_restoration,
    validate_wave, historical_payload, portrait_replacement_proof)

ROOT = Path(__file__).resolve().parents[2]


class WaterIceRestorationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.wave = restoration_wave(later_art_waves())
        cls.entries = json.loads((ROOT / 'assets/new-horizons/runtime-art-manifest.json').read_text())['entries']

    def test_exact_selected_aliases_native_groups_and_history(self):
        validate_restoration(self.wave, self.entries, read_resource)
        self.assertEqual((self.wave['baselineCount'], self.wave['entryCount']), (6250, 6252))
        self.assertEqual(len(self.wave['resources']), 2)
        self.assertEqual(len(self.wave['replacedEntries']), 10)
        for replacement in self.wave['replacedEntries']:
            name = replacement['current']['resource']
            payload = read_resource(name)
            previous = historical_payload(self.wave, replacement, payload)
            if name.endswith('.png'):
                self.assertTrue(portrait_replacement_proof(replacement['previous'], payload))
                self.assertEqual(previous, payload)  # Does not claim to recover old pixels.
                image = open_image(name)
                expected = (58, 64) if ('58x64' in name or 'twcrport' in name) else (32, 32)
                self.assertEqual(image.size, expected)
                alpha = image.convert('RGBA').getchannel('A').getextrema()
                self.assertEqual(alpha, (255, 255) if expected == (58, 64) else (0, 255))

    def test_changed_records_and_payloads_cannot_claim_approved_restoration(self):
        for field in ('baselineArchiveSha256', 'newArchiveSha256', 'approval', 'resources'):
            changed = copy.deepcopy(self.wave); changed[field] = 'unreviewed'
            with self.assertRaises(ValueError): validate_wave(changed)
        for replacement in self.wave['replacedEntries']:
            name = replacement['current']['resource']
            with self.assertRaises(ValueError):
                historical_payload(self.wave, replacement, read_resource(name) + b'bad')
            changed = dict(replacement['previous'], origin='unreviewed')
            self.assertFalse(portrait_replacement_proof(changed, read_resource(name)))
        for target in self.wave['resources'] + [self.wave['replacedEntries'][0]['current']['resource']]:
            changed = copy.deepcopy(self.entries)
            next(row for row in changed if row['resource'] == target)['origin'] = 'unreviewed'
            with self.assertRaises(ValueError): validate_restoration(self.wave, changed, read_resource)
        with self.assertRaises(ValueError): restoration_wave([self.wave, self.wave])
        with self.assertRaises(ValueError): restoration_wave([])


if __name__ == '__main__':
    unittest.main()
