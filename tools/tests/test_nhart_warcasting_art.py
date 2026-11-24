"""Warcasting artwork: exact wave, provenance, derived perk states, native pixels and bindings."""
import copy
import json
import unittest
from .nhart_warcasting_art import (EXPECTED, PERKS, RANK_IMAGES, ROOT, bindings_present, selection_record,
                                   validate_bindings, validate_warcasting_art, warcasting_wave)
from .nhart_test_resources import later_art_waves, read_resource, verified_index

HEADER = 'client/windows/NewHorizonsPerkIcons.h'


def load_consumers():
    header = (ROOT / HEADER).read_text()
    skills = json.loads((ROOT / 'config/newHorizonsSkills.json').read_text())
    module = json.loads((ROOT / 'Mods/new-horizons/mod.json').read_text())['skills']
    return header, skills, module


class WarcastingArtTest(unittest.TestCase):
    def setUp(self):
        self.wave = warcasting_wave(later_art_waves())
        self.entries = list(verified_index().values())

    def test_exact_selected_native_resources(self):
        validate_warcasting_art(self.wave, self.entries, read_resource)
        self.assertEqual((self.wave['baselineCount'], self.wave['entryCount']), (6677, 6759))
        self.assertEqual(sorted(self.wave['resources']), sorted(EXPECTED))

    def test_incomplete_wave_or_changed_pixels_are_rejected(self):
        for field in ('resources', 'baselineEntriesSha256', 'newArchiveSha256'):
            changed = copy.deepcopy(self.wave); changed[field] = []
            with self.subTest(field=field), self.assertRaises(ValueError):
                warcasting_wave([changed])
        for waves in ([], [self.wave, self.wave]):
            with self.assertRaises(ValueError):
                warcasting_wave(waves)
        with self.assertRaises(ValueError):
            validate_warcasting_art(self.wave, self.entries, lambda _: b'changed')
        swapped = {'SPRITES/NH_perk_warcasting2_opening_pressed.png':
                   read_resource('SPRITES/NH_perk_warcasting2_opening_highlighted.png')}
        with self.assertRaises(ValueError):
            validate_warcasting_art(self.wave, self.entries, lambda name: swapped.get(name) or read_resource(name))

    def test_record_keeps_relative_private_provenance(self):
        selected = selection_record()
        text = json.dumps(selected)
        self.assertNotIn('/home/', text)
        self.assertNotIn('~/', text)
        for icon in selected['provenance']['perks']['icons']:
            self.assertIn(icon['perk'], PERKS)
        ranks = selected['provenance']['rankArt']['ranks']
        self.assertEqual(set(ranks), set(RANK_IMAGES))
        for rank, row in ranks.items():
            last = row['lineage'][-1]['sha256']
            self.assertTrue(all(entry['sourceMasterSha256'] == last for entry in selected['entries']
                                if entry.get('rank') == rank))

    @unittest.skipUnless(bindings_present(*load_consumers()[:2]), 'new-horizons:warcast is not bound yet')
    def test_rank_and_perk_icons_are_bound(self):
        header, skills, module = load_consumers()
        selected = selection_record()
        validate_bindings(header, skills, module, selected)
        for identity, (_, stem) in PERKS.items():
            with self.subTest(perk=identity), self.assertRaises(ValueError):
                validate_bindings(header.replace(stem + '"}', 'NH_perk_neutral"}', 1), skills, module, selected)
        changed = copy.deepcopy(skills); changed['warcast']['expert']['images']['small'] = 'NH_neutral.png'
        with self.assertRaises(ValueError):
            validate_bindings(header, changed, module, selected)
        changed = copy.deepcopy(selected); changed['perkBindings'] = changed['perkBindings'][1:]
        with self.assertRaises(ValueError):
            validate_bindings(header, skills, module, changed)

    def test_binding_check_rejects_missing_consumers(self):
        selected = selection_record()
        with self.assertRaises(ValueError):
            validate_bindings('', {}, {}, selected)
        bound = {'warcast': {rank: {'images': images} for rank, images in RANK_IMAGES.items()}}
        rows = ''.join('{"%s", "%s"},\n' % (identity, stem) for identity, (_, stem) in PERKS.items())
        validate_bindings(rows, bound, bound, selected)
        with self.assertRaises(ValueError):
            validate_bindings(rows + rows, bound, bound, selected)
        self.assertTrue(bindings_present(rows, {}))
        self.assertTrue(bindings_present('', bound))


if __name__ == '__main__':
    unittest.main()
