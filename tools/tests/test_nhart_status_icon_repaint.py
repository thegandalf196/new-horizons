"""Repainted 50px status icons: exact wave, typed replacement proof and native pixels."""
import copy
import json
from pathlib import Path
import unittest
from .nhart_status_icon_repaint import RESOURCES, repaint_replacement_proof, repaint_wave, validate_repaint
from .nhart_test_resources import later_art_waves, read_resource


MANIFEST = Path(__file__).resolve().parents[2] / 'assets/new-horizons/runtime-art-manifest.json'


def manifest_entries():
    return json.loads(MANIFEST.read_text())['entries']


class StatusIconRepaintTest(unittest.TestCase):
    def test_exact_replacements_and_native_pixels(self):
        wave = repaint_wave(later_art_waves())
        validate_repaint(wave, manifest_entries(), read_resource)
        self.assertEqual((wave['baselineCount'], wave['entryCount'], wave['resources']), (6602, 6602, []))
        self.assertEqual(sorted(row['current']['resource'] for row in wave['replacedEntries']), sorted(RESOURCES))

    def test_flat_originals_have_only_a_typed_proof(self):
        wave = repaint_wave(later_art_waves())
        for replacement in wave['replacedEntries']:
            payload = read_resource(replacement['current']['resource'])
            self.assertTrue(repaint_replacement_proof(replacement['previous'], payload))
            self.assertFalse(repaint_replacement_proof(replacement['current'], payload))
            with self.assertRaises(ValueError):
                repaint_replacement_proof(replacement['previous'], payload + b'changed')

    def test_changed_wave_or_pixels_are_rejected(self):
        wave = repaint_wave(later_art_waves())
        for field in ('replacedEntries', 'baselineEntriesSha256', 'newArchiveSha256'):
            changed = copy.deepcopy(wave); changed[field] = []
            with self.subTest(field=field), self.assertRaises(ValueError):
                repaint_wave([changed])
        for waves in ([], [wave, wave]):
            with self.assertRaises(ValueError):
                repaint_wave(waves)
        with self.assertRaises(ValueError):
            validate_repaint(wave, manifest_entries(), lambda _: b'changed')
