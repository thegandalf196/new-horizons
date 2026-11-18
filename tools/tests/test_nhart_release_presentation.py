"""Native role completeness and narrow presentation-history mutation controls."""
import copy
import json
from pathlib import Path
import unittest
import sys
from .nhart_release_presentation import (pinned_record, SELECTION, SELECTION_SHA256,
    before_presentation, presentation_wave, validate_presentation_art)
from .nhart_test_resources import later_art_waves, read_resource, verified_index

ROOT = Path(__file__).resolve().parents[2]


class ReleasePresentationTest(unittest.TestCase):
    def test_complete_selected_native_family_and_descriptors(self):
        wave = presentation_wave(later_art_waves())
        validate_presentation_art(wave, list(verified_index().values()), read_resource)
        selected = pinned_record(SELECTION, SELECTION_SHA256)
        for spell in selected['spells']:
            self.assertEqual(len(spell['bindings']), 5)
            self.assertEqual(len(set(spell['bindings'].values())), 3)
        for name, frames in [('NH_focus_magic_orb', ['NH_focus_magic_orb.png']),
                             ('NH_prayers_bookmark', ['NH_prayers_bookmark_selected.png',
                                                      'NH_prayers_bookmark_inactive.png'])]:
            descriptor = json.loads(read_resource('SPRITES/' + name + '.json'))
            self.assertEqual(descriptor, {'images': [{'group': 0, 'frame': i, 'file': image}
                                                     for i, image in enumerate(frames)]})

    def test_transition_reverses_only_art_and_cast_metadata(self):
        sys.path.insert(0, str(ROOT / 'tools'))
        from import_new_horizons_spell_art import parse_jsonc
        current = parse_jsonc((ROOT / 'Mods/new-horizons/Content/config/spells/newHorizons.json').read_text())
        old = before_presentation(current)
        self.assertNotIn('animation', old['focusMagic'])
        self.assertNotIn('sounds', old['focusMagic'])
        self.assertEqual(before_presentation(old), old)
        self.assertEqual(current['focusMagic']['animation'], {'affect': ['C12SPA0']})
        for key in ('forceLance', 'forcedRecall'):
            left, right = copy.deepcopy(current[key]), copy.deepcopy(old[key])
            left.pop('graphics'); right.pop('graphics')
            self.assertEqual(left, right)
        changed = copy.deepcopy(current)
        changed['focusMagic']['levels']['basic']['cost'] += 7
        self.assertEqual(before_presentation(changed)['focusMagic']['levels']['basic']['cost'],
                         old['focusMagic']['levels']['basic']['cost'] + 7)
        for spell, field in [('focusMagic', 'animation'), ('focusMagic', 'sounds')]:
            changed = copy.deepcopy(current)
            changed[spell][field] = {}
            with self.assertRaises(ValueError):
                before_presentation(changed)
        changed = copy.deepcopy(current)
        changed['forceLance']['graphics'] = old['forceLance']['graphics']
        with self.assertRaises(ValueError):
            before_presentation(changed)

    def test_exact_wave_mutation_or_incomplete_family_is_rejected(self):
        wave = presentation_wave(later_art_waves())
        for field in ('resources', 'baselineEntriesSha256', 'newArchiveSha256'):
            changed = copy.deepcopy(wave); changed[field] = []
            with self.subTest(field=field), self.assertRaises(ValueError):
                presentation_wave([changed])
        for waves in ([], [wave, wave]):
            with self.assertRaises(ValueError):
                presentation_wave(waves)
