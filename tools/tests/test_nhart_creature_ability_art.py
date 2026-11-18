"""Runtime ability-art completeness, native pixels and strict wave mutation controls."""
import copy
import unittest
import json
from pathlib import Path
import sys
from .nhart_creature_ability_art import ability_wave, validate_ability_art, validate_named_spell_subtypes, RECORD
from .nhart_test_resources import later_art_waves, read_resource, verified_index


class CreatureAbilityArtTest(unittest.TestCase):
    def test_exact_selected_native_resources_match_current_consumers(self):
        wave = ability_wave(later_art_waves())
        validate_ability_art(wave, list(verified_index().values()), read_resource)
        self.assertEqual((wave['baselineCount'], wave['entryCount']), (6563, 6596))
        self.assertEqual(len(wave['resources']), 33)
        self.assertTrue(all(not name.endswith('_50.png') for name in wave['resources']))
        self.assertNotIn('SPRITES/NH_ability_mind_spell_immunity.png', wave['resources'])
        self.assertNotIn('SPRITES/NH_ability_fearful.png', wave['resources'])
        self.assertIn('SPRITES/NH_ability_acid_breath.png', wave['resources'])
        self.assertIn('SPRITES/NH_ability_raise_demons.png', wave['resources'])

    def test_incomplete_wave_changed_provenance_or_pixels_are_rejected(self):
        wave = ability_wave(later_art_waves())
        for field in ('resources', 'baselineEntriesSha256', 'newArchiveSha256'):
            changed = copy.deepcopy(wave); changed[field] = []
            with self.subTest(field=field), self.assertRaises(ValueError):
                ability_wave([changed])
        for waves in ([], [wave, wave]):
            with self.assertRaises(ValueError):
                ability_wave(waves)
        with self.assertRaises(ValueError):
            validate_ability_art(wave, list(verified_index().values()), lambda _: b'changed')

    def test_named_spell_subtypes_preserve_both_acid_forms_and_external_icons(self):
        root = Path(__file__).resolve().parents[2]
        sys.path.insert(0, str(root / 'tools'))
        from import_new_horizons_spell_art import parse_jsonc
        config = parse_jsonc((root / 'config/bonuses.json').read_text())
        recorded = json.loads((root / RECORD).read_text())['namedSpellSubtypeBindings']
        validate_named_spell_subtypes(config, recorded)
        for mutation in ('missing-acid-share', 'numeric-key', 'wrong-retained-icon'):
            changed = copy.deepcopy(config)
            icons = changed['SPELL_AFTER_ATTACK']['graphics']['subtypeIcons']
            if mutation == 'missing-acid-share':
                del icons['spell.acidBreathDamage']
            elif mutation == 'numeric-key':
                icons['0'] = icons.pop('spell.acidBreath')
            else:
                icons['spell.dispelHelpful'] = 'SPELLINT.def:0:35'
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                validate_named_spell_subtypes(changed, recorded)
