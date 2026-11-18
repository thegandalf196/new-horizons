#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Fresh hero declarations; native creation and later earned perks are separate gates."""
import copy
from pathlib import Path
import unittest

from jsonschema import Draft4Validator
from referencing import Registry, Resource

if __package__:
    from .test_new_horizons_content import load
else:
    from test_new_horizons_content import load

ROOT = Path(__file__).resolve().parents[2]


class StartingPerkContentTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rules = load(ROOT / 'config/newHorizonsHeroes.json')
        cls.start = cls.rules['startingSkills']
        cls.profiles = cls.start['startingDevelopmentProfiles']

    def test_registered_ordinary_campaign_and_placeholder_heroes_have_defaults(self):
        roster = set()
        for filename in load(ROOT / 'config/gameConfig.json')['heroes']:
            roster.update('core:' + key for key, row in load(ROOT / filename).items()
                          if isinstance(row, dict) and 'class' in row)
        self.assertEqual(len(roster), 163)
        self.assertEqual(set(self.profiles), roster)

    def test_every_default_has_two_distinct_basic_skills_and_one_authored_perk(self):
        factions = set(self.start['factionSkills'].values())
        self.assertEqual(set(self.start['startingPerkProfiles']), set(self.profiles))
        perk_rules = load(ROOT / 'config/newHorizonsPerks.json')['skills']
        for hero, profile in self.profiles.items():
            with self.subTest(hero=hero):
                self.assertEqual(set(profile), {'skills'})
                ranks = profile['skills']
                self.assertEqual(len(ranks), 2)
                owned = {row['skill'] for row in ranks}
                self.assertEqual(len(owned), 2)
                self.assertEqual({row['rank'] for row in ranks}, {1})
                self.assertEqual(len(owned & factions), 1)
                choices = self.start['startingPerkProfiles'][hero]
                self.assertEqual(len(choices), 1)
                choice, = choices
                self.assertIn(choice['skill'], owned)
                matching = [p for p in perk_rules[choice['skill']]['perks']
                            if p['id'] == choice['perk']]
                self.assertEqual(len(matching), 1)
                self.assertEqual(matching[0]['requires'], 'basic')
                self.assertEqual(matching[0]['effect']['status'], 'active')

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
                skills = {row['skill'] for row in self.profiles['core:' + hero]['skills']}
                self.assertIn(expected, skills, hero)
                generic, = skills - {expected}
                self.assertGreater(self.rules['skillOfferWeights'][hero_class][generic], 0, hero)

    def test_module_prototypes_have_no_authored_starting_perks(self):
        module = load(ROOT / 'Mods/new-horizons/mod.json')
        for filename in module['heroes']:
            for hero, prototype in load(ROOT / 'Mods/new-horizons/Content' / filename).items():
                self.assertNotIn('startingPerks', prototype, hero)

    def test_captured_markers_and_generated_rules_agree(self):
        self.assertEqual(self.rules['skillOfferLayoutVersion'], 1)
        self.assertEqual(self.start['startingPerksPolicyVersion'], 4)
        generated = load(ROOT / 'Mods/new-horizons/mod.json')['settings']['heroes']['newHorizons']
        self.assertEqual(generated, self.rules)

    def test_schema_rejects_perks_nonbasic_and_malformed_fresh_profiles(self):
        schema_path = ROOT / 'config/schemas/newHorizonsHeroes.json'
        registry = Registry().with_resources((
            (p.as_uri(), Resource.from_contents(load(p)))
            for p in (ROOT / 'config/schemas').glob('newHorizons*.json')))
        schema = load(schema_path)
        schema['id'] = schema_path.as_uri()
        validator = Draft4Validator(schema, registry=registry)
        validator.validate(self.rules)
        bad_cases = []
        for value in ({}, {'core:nimbus': []}):
            bad = copy.deepcopy(self.rules)
            bad['startingSkills']['startingPerkProfiles'] = value
            bad_cases.append(bad)
        for mutation in ('perk', 'advanced', 'single', 'missing', 'two-perks', 'no-perks'):
            bad = copy.deepcopy(self.rules)
            profile = bad['startingSkills']['startingDevelopmentProfiles']['core:nimbus']
            if mutation == 'perk':
                profile['startingPerks'] = [{'skill': 'new-horizons:learning',
                                             'perk': 'new-horizons:learning.eagleEye'}]
            elif mutation == 'advanced':
                profile['skills'][0]['rank'] = 2
            elif mutation == 'single':
                profile['skills'].pop()
            elif mutation == 'two-perks':
                choices = bad['startingSkills']['startingPerkProfiles']['core:nimbus']
                choices.append(copy.deepcopy(choices[0]))
            elif mutation == 'no-perks':
                del bad['startingSkills']['startingPerkProfiles']['core:nimbus']
            else:
                del bad['startingSkills']['startingDevelopmentProfiles']['core:nimbus']
            bad_cases.append(bad)
        for bad in bad_cases:
            self.assertTrue(list(validator.iter_errors(bad)))


if __name__ == '__main__':
    unittest.main()
