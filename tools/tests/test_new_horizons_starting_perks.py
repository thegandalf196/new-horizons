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
        self.assertEqual(set(self.start['startingDevelopmentProfiles']), roster)

    def test_every_default_has_two_distinct_basic_skills_and_perks(self):
        factions = set(self.start['factionSkills'].values())
        for hero, profile in self.start['startingDevelopmentProfiles'].items():
            with self.subTest(hero=hero):
                ranks = profile['skills']
                self.assertEqual(len(ranks), 2)
                owned = {row['skill'] for row in ranks}
                self.assertEqual(len(owned), 2)
                self.assertEqual({row['rank'] for row in ranks}, {1})
                self.assertEqual(len(owned & factions), 1)
                self.assertEqual(len(profile['startingPerks']), 1)
                self.assertNotIn(profile['startingPerks'][0]['skill'], factions)
                rows = self.profiles[hero]
                self.assertEqual(len(rows), 2)
                actual = [self.perks[row['perk']] for row in rows]
                self.assertTrue(all(self.perks[row['perk']][0] == row['skill'] for row in rows))
                self.assertEqual(set(actual), {(skill, 1) for skill in owned})
        self.assertEqual(sum(map(len, self.profiles.values())), 326)

    def test_defaults_match_class_faction_and_eligible_generic_skill(self):
        classes = {}
        for filename in load(ROOT / 'config/gameConfig.json')['heroClasses']:
            classes.update({'core:' + key: row for key, row in load(ROOT / filename).items()
                            if isinstance(row, dict) and 'faction' in row})
        for filename in load(ROOT / 'config/gameConfig.json')['heroes']:
            for hero, prototype in load(ROOT / filename).items():
                if not isinstance(prototype, dict) or 'class' not in prototype:
                    continue
                hero_class = 'core:' + prototype['class']
                faction = classes[hero_class]['faction']
                faction = faction if ':' in faction else 'core:' + faction
                expected = self.start['factionSkills'][faction]
                profile = self.start['startingDevelopmentProfiles']['core:' + hero]
                self.assertIn(expected, {row['skill'] for row in profile['skills']}, hero)
                generic = profile['startingPerks'][0]['skill']
                self.assertGreater(self.rules['skillOfferWeights'][hero_class][generic], 0, hero)

    def test_existing_profile_and_prototype_choices_are_preserved(self):
        for hero, profile in self.start['startingDevelopmentProfiles'].items():
            for row in profile['startingPerks']:
                self.assertIn(row, self.profiles[hero], hero)
        module = load(ROOT / 'Mods/new-horizons/mod.json')
        for filename in module['heroes']:
            for hero, prototype in load(ROOT / 'Mods/new-horizons/Content' / filename).items():
                for row in prototype.get('startingPerks', []):
                    self.assertIn(row, self.profiles[hero], hero)

    def test_seth_and_astral_defaults(self):
        self.assertEqual(self.profiles['core:nimbus'], [
            {'skill': 'new-horizons:learning', 'perk': 'new-horizons:learning.eagleEye'},
            {'skill': 'new-horizons:necromancy', 'perk': 'new-horizons:necromancy.graveKnowledge'},
        ])
        self.assertEqual(self.profiles['core:astral'], [
            {'skill': 'new-horizons:spellcraft', 'perk': 'new-horizons:spellcraft.concentration'},
            {'skill': 'new-horizons:metamagic', 'perk': 'new-horizons:metamagic.lingeringMagic'},
        ])


    def test_captured_markers_and_generated_rules_agree(self):
        self.assertEqual(self.rules['skillOfferLayoutVersion'], 1)
        self.assertEqual(self.start['startingPerksPolicyVersion'], 2)
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
