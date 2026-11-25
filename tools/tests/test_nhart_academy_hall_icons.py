"""Academy Town Hall icons: exact replacement wave, opaque 150x70 icons and typed proof."""
import copy
import json
from pathlib import Path
import unittest
from .nhart_academy_hall_icons import (
    ICONS, hall_icon_replacement_proof, hall_icon_wave, validate_hall_icons)
from .nhart_test_resources import later_art_waves, read_resource

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / 'assets/new-horizons/runtime-art-manifest.json'


def manifest_entries():
    return json.loads(MANIFEST.read_text())['entries']


class AcademyHallIconTest(unittest.TestCase):
    def test_exact_replaced_icons(self):
        wave = hall_icon_wave(later_art_waves())
        validate_hall_icons(wave, manifest_entries(), read_resource)
        self.assertEqual((wave['entryCount'], len(wave['replacedEntries'])), (wave['baselineCount'], len(ICONS)))

    def test_replaced_icons_have_only_a_typed_proof(self):
        wave = hall_icon_wave(later_art_waves())
        for replacement in wave['replacedEntries']:
            payload = read_resource(replacement['current']['resource'])
            self.assertTrue(hall_icon_replacement_proof(replacement['previous'], payload))
            self.assertFalse(hall_icon_replacement_proof(replacement['current'], payload))
            with self.assertRaises(ValueError):
                hall_icon_replacement_proof(replacement['previous'], payload + b'changed')

    def test_changed_wave_record_or_pixels_are_rejected(self):
        wave = hall_icon_wave(later_art_waves())
        for field in ('replacedEntries', 'baselineEntriesSha256', 'newArchiveSha256'):
            changed = copy.deepcopy(wave); changed[field] = []
            with self.subTest(field=field), self.assertRaises(ValueError):
                hall_icon_wave([changed])
        for waves in ([], [wave, wave]):
            with self.assertRaises(ValueError):
                hall_icon_wave(waves)
        with self.assertRaises(ValueError):
            validate_hall_icons(wave, manifest_entries(), lambda _: b'changed')
        # the icon must still show the selected building art
        entries = manifest_entries()
        for row in entries:
            if row['resource'] == 'SPRITES/NH_academy/town/buildings/tbtwdw_2.png':
                row['sha256'] = '0' * 64
        with self.assertRaises(ValueError):
            validate_hall_icons(wave, entries, read_resource)


if __name__ == '__main__':
    unittest.main()
