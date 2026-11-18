"""Distinct current faction perk artwork in the shipping resource package."""
import copy
import hashlib
import json
from pathlib import Path
import re
import unittest

from .nhart_current_faction_perk_art import BINDINGS, current_faction_perk_wave_from
from .nhart_test_resources import later_art_waves, read_resource, open_image

ROOT = Path(__file__).resolve().parents[2]


class CurrentFactionPerkArtTest(unittest.TestCase):
    def test_exact_twenty_current_bindings_are_unique_and_use_distinct_paintings(self):
        header = (ROOT / 'client/windows/NewHorizonsPerkIcons.h').read_text()
        rows = re.findall(r'\{"(new-horizons:[^"]+)", "([^"]+)"\}', header)
        for identity, expected in BINDINGS.items():
            self.assertEqual([icon for key, icon in rows if key == identity], [expected])
        self.assertEqual(len(set(BINDINGS.values())), 20)
        for size in (44, 32):
            suffix = '.png' if size == 44 else '_32.png'
            paintings = []
            for stem in BINDINGS.values():
                resource = 'SPRITES/' + stem + suffix
                image = open_image(resource)
                self.assertEqual((image.mode, image.size), ('RGBA', (size, size)))
                paintings.append(hashlib.sha256(image.tobytes()).hexdigest())
            self.assertEqual(len(set(paintings)), 20)

    def test_all_twenty_have_real_four_state_descriptors_and_native_aliases(self):
        wave = current_faction_perk_wave_from(later_art_waves())
        expected = set()
        for stem in BINDINGS.values():
            descriptor = json.loads(read_resource('SPRITES/' + stem + '.json'))
            frames = [{'group': 0, 'frame': index, 'file': stem + '_' + state + '.png'}
                      for index, state in enumerate(('normal', 'pressed', 'disabled', 'highlighted'))]
            self.assertEqual(descriptor, {'images': frames})
            self.assertEqual(open_image('SPRITES/' + stem + '.png').tobytes(),
                             open_image('SPRITES/' + stem + '_normal.png').tobytes())
            expected.update('SPRITES/' + stem + suffix for suffix in
                            ('.json', '.png', '_32.png', '_normal.png', '_pressed.png',
                             '_disabled.png', '_highlighted.png'))
        self.assertEqual(len(expected), 140)
        self.assertEqual(set(wave['resources']), expected)

    def test_wave_rejects_partial_duplicate_and_wrong_identity_families(self):
        wave = current_faction_perk_wave_from(later_art_waves())
        for mutate in ('missing', 'duplicate', 'wrong-icon'):
            changed = copy.deepcopy(wave)
            if mutate == 'missing':
                changed['perks'].pop()
            elif mutate == 'duplicate':
                changed['perks'][-1] = copy.deepcopy(changed['perks'][0])
            else:
                changed['perks'][0]['icon44'] = 'NH_perk_neutral'
            with self.subTest(mutate=mutate), self.assertRaises(ValueError):
                current_faction_perk_wave_from([changed])
        with self.assertRaises(ValueError):
            current_faction_perk_wave_from([])
        with self.assertRaises(ValueError):
            current_faction_perk_wave_from([wave, wave])
