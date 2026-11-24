"""Academy town animation: exact wave, frames, rest poses and typed replacement proof."""
import copy
import json
from pathlib import Path
import unittest
from .nhart_academy_town_animation import (
    ADDED, REPLACED, town_animation_replacement_proof, town_animation_wave, validate_town_animation)
from .nhart_test_resources import later_art_waves, read_resource

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / 'assets/new-horizons/runtime-art-manifest.json'


def manifest_entries():
    return json.loads(MANIFEST.read_text())['entries']


class AcademyTownAnimationTest(unittest.TestCase):
    def test_exact_frames_and_rest_poses(self):
        wave = town_animation_wave(later_art_waves())
        validate_town_animation(wave, manifest_entries(), read_resource)
        self.assertEqual(wave['entryCount'], wave['baselineCount'] + ADDED)
        self.assertEqual(len(wave['replacedEntries']), REPLACED)

    def test_replaced_originals_have_only_a_typed_proof(self):
        wave = town_animation_wave(later_art_waves())
        for replacement in wave['replacedEntries']:
            payload = read_resource(replacement['current']['resource'])
            self.assertTrue(town_animation_replacement_proof(replacement['previous'], payload))
            self.assertFalse(town_animation_replacement_proof(replacement['current'], payload))
            with self.assertRaises(ValueError):
                town_animation_replacement_proof(replacement['previous'], payload + b'changed')

    def test_changed_wave_or_pixels_are_rejected(self):
        wave = town_animation_wave(later_art_waves())
        for field in ('resources', 'replacedEntries', 'baselineEntriesSha256', 'newArchiveSha256'):
            changed = copy.deepcopy(wave); changed[field] = []
            with self.subTest(field=field), self.assertRaises(ValueError):
                town_animation_wave([changed])
        for waves in ([], [wave, wave]):
            with self.assertRaises(ValueError):
                town_animation_wave(waves)
        with self.assertRaises(ValueError):
            validate_town_animation(wave, manifest_entries(), lambda _: b'changed')

        def shifted_frame(name):
            if name == 'SPRITES/NH_ACADEMY_TBTWHOLY.json':
                return read_resource(name).replace(b'frame-00.png', b'frame-02.png', 1)
            return read_resource(name)
        with self.assertRaises(ValueError):
            validate_town_animation(wave, manifest_entries(), shifted_frame)


if __name__ == '__main__':
    unittest.main()
