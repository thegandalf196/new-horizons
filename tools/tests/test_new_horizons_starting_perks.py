#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Authored initial-perk coverage; native creation and interactive progression are separate gates."""
from pathlib import Path
import unittest

from jsonschema import Draft4Validator, ValidationError
from referencing import Registry, Resource

if __package__:
    from .test_new_horizons_content import load
else:
    from test_new_horizons_content import load

ROOT = Path(__file__).resolve().parents[2]
TIERS = {'basic': 1, 'advanced': 2, 'expert': 3}
# Campaign/portrait defaults after the authoritative legacy/faction normalization.
SPECIAL_RANKS = {
    'sirMullich': {'discipline': 2, 'divineMandate': 1},
    'adrienne': {'bulwarkOfTheMire': 1, 'havocMagic': 3},
    'catherine': {'discipline': 1, 'divineMandate': 1},
    'dracon': {'metamagic': 2},
    'gelu': {'archery': 1, 'sylvanLuck': 1},
    'kilgor': {'offense': 2, 'bloodrage': 1},
    'undeadHaart': {'necromancy': 2},
    'mutare': {'estates': 1, 'shroudOfMalassa': 1},
    'roland': {'discipline': 1, 'divineMandate': 1},
    'mutareDrake': {'estates': 1, 'shroudOfMalassa': 1},
    'boragus': {'offense': 1, 'bloodrage': 1},
    'xeron': {'discipline': 1, 'demonicGating': 1},
    'portraitGeneralKendal': {'divineMandate': 1},
    'portraitYoungCristian': {'divineMandate': 1},
    'portraitOrdwald': {'sylvanLuck': 1},
    'portraitFinneas': {'necromancy': 1},
    'portraitYoungGem': {'sylvanLuck': 1},
    'portraitYoungSandro': {'necromancy': 1},
    'portraitYoungYog': {'metamagic': 1},
}


class StartingPerkContentTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rules = load(ROOT / 'config/newHorizonsHeroes.json')
        cls.start = cls.rules['startingSkills']
        cls.profiles = cls.start['startingPerkProfiles']
        cls.skills = load(ROOT / 'config/newHorizonsPerks.json')['skills']
        cls.perks = {p['id']: (s, TIERS[p['requires']])
                     for s, row in cls.skills.items() for p in row['perks']}

    def test_registered_heroes_have_authored_defaults(self):
        roster = set()
        for filename in load(ROOT / 'config/gameConfig.json')['heroes']:
            roster.update('core:' + key for key, row in load(ROOT / filename).items()
                          if isinstance(row, dict) and 'class' in row)
        self.assertEqual(len(roster), 163)
        self.assertEqual(set(self.profiles), roster)

    def test_each_owned_tier_has_one_valid_perk(self):
        defaults = {h: {r['skill']: r['rank'] for r in p['skills']}
                    for h, p in self.start['startingDevelopmentProfiles'].items()}
        defaults.update({'core:' + h: {'new-horizons:' + s: n for s, n in ranks.items()}
                         for h, ranks in SPECIAL_RANKS.items()})
        self.assertEqual(set(defaults), set(self.profiles))
        for hero, ranks in defaults.items():
            with self.subTest(hero=hero):
                rows = self.profiles[hero]
                actual = [self.perks[r['perk']] for r in rows]
                self.assertTrue(all(self.perks[r['perk']][0] == r['skill'] for r in rows))
                expected = {(skill, tier) for skill, rank in ranks.items()
                            for tier in range(1, rank + 1)}
                self.assertEqual(len(actual), len(set(actual)))
                self.assertEqual(set(actual), expected)

    def test_existing_profile_and_prototype_choices_are_preserved(self):
        for hero, profile in self.start['startingDevelopmentProfiles'].items():
            for row in profile['startingPerks']:
                self.assertIn(row, self.profiles[hero], hero)
        module = load(ROOT / 'Mods/new-horizons/mod.json')
        for filename in module['heroes']:
            for hero, prototype in load(ROOT / 'Mods/new-horizons/Content' / filename).items():
                for row in prototype.get('startingPerks', []):
                    self.assertIn(row, self.profiles[hero], hero)

    def test_seth_and_advanced_defaults(self):
        self.assertEqual(self.profiles['core:nimbus'], [
            {'skill': 'new-horizons:learning', 'perk': 'new-horizons:learning.eagleEye'},
            {'skill': 'new-horizons:necromancy', 'perk': 'new-horizons:necromancy.graveKnowledge'},
        ])
        self.assertEqual(len(self.profiles['core:adrienne']), 4)
        self.assertIn({'skill': 'new-horizons:havocMagic',
                       'perk': 'new-horizons:havocMagic.pyromancer'}, self.profiles['core:adrienne'])
        self.assertEqual(len(self.profiles['core:thane']), 3)
        self.assertEqual(len(self.profiles['core:astral']), 3)

    def test_captured_markers_and_generated_rules_agree(self):
        self.assertEqual(self.rules['skillOfferLayoutVersion'], 1)
        self.assertEqual(self.start['startingPerksPolicyVersion'], 1)
        generated = load(ROOT / 'Mods/new-horizons/mod.json')['settings']['heroes']['newHorizons']
        self.assertEqual(generated, self.rules)

    def test_schema_validates_and_rejects_malformed_authored_rows(self):
        schema_path = ROOT / 'config/schemas/newHorizonsHeroes.json'
        registry = Registry().with_resources((
            (p.as_uri(), Resource.from_contents(load(p)))
            for p in (ROOT / 'config/schemas').glob('newHorizons*.json')))
        schema = load(schema_path)
        schema['id'] = schema_path.as_uri()
        validator = Draft4Validator(schema, registry=registry)
        validator.validate(self.rules)
        entry_schema = load(schema_path)['definitions']['startingDevelopmentProfile']['properties']['startingPerks']['items']
        row_validator = Draft4Validator(entry_schema)
        for bad in ({'skill': 'necromancy', 'perk': 'new-horizons:necromancy.graveKnowledge'},
                    {'skill': 'new-horizons:necromancy'},
                    {'skill': 'new-horizons:necromancy', 'perk': 'new-horizons:necromancy.graveKnowledge', 'rank': 1}):
            with self.assertRaises(ValidationError):
                row_validator.validate(bad)


if __name__ == '__main__':
    unittest.main()
