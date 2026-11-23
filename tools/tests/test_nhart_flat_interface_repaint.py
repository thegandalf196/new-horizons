"""Repainted flat interface icons: exact wave, typed replacement proof and native pixels."""
import copy
import json
from pathlib import Path
import unittest
from .nhart_flat_interface_repaint import COUNT, flat_repaint_wave, flat_replacement_proof, validate_flat_repaint
from .nhart_test_resources import later_art_waves, read_resource

MANIFEST = Path(__file__).resolve().parents[2] / 'assets/new-horizons/runtime-art-manifest.json'


def manifest_entries():
    return json.loads(MANIFEST.read_text())['entries']


class FlatInterfaceRepaintTest(unittest.TestCase):
    def test_exact_replacements_and_native_pixels(self):
        wave = flat_repaint_wave(later_art_waves())
        validate_flat_repaint(wave, manifest_entries(), read_resource)
        self.assertEqual(wave['entryCount'], wave['baselineCount'])
        self.assertEqual(len(wave['replacedEntries']), COUNT)

    def test_flat_originals_have_only_a_typed_proof(self):
        wave = flat_repaint_wave(later_art_waves())
        for replacement in wave['replacedEntries']:
            payload = read_resource(replacement['current']['resource'])
            self.assertTrue(flat_replacement_proof(replacement['previous'], payload))
            self.assertFalse(flat_replacement_proof(replacement['current'], payload))
            with self.assertRaises(ValueError):
                flat_replacement_proof(replacement['previous'], payload + b'changed')

    def test_changed_wave_or_pixels_are_rejected(self):
        wave = flat_repaint_wave(later_art_waves())
        for field in ('replacedEntries', 'baselineEntriesSha256', 'newArchiveSha256'):
            changed = copy.deepcopy(wave); changed[field] = []
            with self.subTest(field=field), self.assertRaises(ValueError):
                flat_repaint_wave([changed])
        with self.assertRaises(ValueError):
            validate_flat_repaint(wave, manifest_entries(), lambda _: b'changed')
