"""Academy town animation masks and config: union masks, typed proof and the six opted-in structures."""
import copy
import json
from pathlib import Path
import unittest
from .nhart_academy_town_animation import STRUCTURES
from .nhart_academy_town_masks import town_mask_replacement_proof, town_mask_wave, validate_town_masks
from .nhart_test_resources import later_art_waves, read_resource

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / 'assets/new-horizons/runtime-art-manifest.json'
ART_PATCH = ROOT / 'Mods/new-horizons/Content/config/factions/academyArt.json'


def manifest_entries():
    return json.loads(MANIFEST.read_text())['entries']


class AcademyTownMaskTest(unittest.TestCase):
    def test_masks_are_the_union_of_the_shipped_frames(self):
        wave = town_mask_wave(later_art_waves())
        validate_town_masks(wave, manifest_entries(), read_resource)
        self.assertEqual((wave['entryCount'], len(wave['replacedEntries'])), (wave['baselineCount'], 12))

    def test_replaced_masks_have_only_a_typed_proof(self):
        wave = town_mask_wave(later_art_waves())
        for replacement in wave['replacedEntries']:
            payload = read_resource(replacement['current']['resource'])
            self.assertTrue(town_mask_replacement_proof(replacement['previous'], payload))
            self.assertFalse(town_mask_replacement_proof(replacement['current'], payload))
            with self.assertRaises(ValueError):
                town_mask_replacement_proof(replacement['previous'], payload + b'changed')

    def test_changed_wave_or_stale_masks_are_rejected(self):
        wave = town_mask_wave(later_art_waves())
        for field in ('replacedEntries', 'baselineEntriesSha256', 'newArchiveSha256'):
            changed = copy.deepcopy(wave); changed[field] = []
            with self.subTest(field=field), self.assertRaises(ValueError):
                town_mask_wave([changed])
        for waves in ([], [wave, wave]):
            with self.assertRaises(ValueError):
                town_mask_wave(waves)
        # a mask that no longer follows the frames: the grail's frame 0 alone instead of the union
        def single_frame(name):
            if name == 'SPRITES/NH_ACADEMY_TBTWHOLY.json':
                descriptor = json.loads(read_resource(name))
                for row in descriptor['images']:
                    row['file'] = descriptor['images'][0]['file']
                return json.dumps(descriptor).encode()
            return read_resource(name)
        with self.assertRaises(ValueError):
            validate_town_masks(wave, manifest_entries(), single_frame)

    def test_exactly_the_animated_structures_draw_their_current_frame_only(self):
        structures = json.loads(ART_PATCH.read_text())['core:tower']['town']['structures']
        marked = {name: row['animation'] for name, row in structures.items() if 'currentFrameOnly' in row}
        self.assertEqual(marked, {value[0]: key for key, value in STRUCTURES.items()})
        self.assertTrue(all(structures[name]['currentFrameOnly'] is True for name in marked))
        for name in marked:
            self.assertEqual((structures[name]['area'], structures[name]['border']),
                             ('NH_academy/town/masks/%s-area.png' % name, 'NH_academy/town/masks/%s-border.png' % name))


if __name__ == '__main__':
    unittest.main()
