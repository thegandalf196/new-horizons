#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Default reward units, source boundaries and shipped schema; no gameplay claim."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

from jsonschema import Draft4Validator
from referencing import Registry, Resource
from referencing.jsonschema import DRAFT4
from test_new_horizons_content import parse_jsonc

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    'primary_reward_generator', ROOT / 'tools/update-new-horizons-module.py')
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)


def load(path):
    return parse_jsonc((ROOT / path).read_text(encoding='utf-8'))


class PrimaryRewardDefaultsTest(unittest.TestCase):
    def setUp(self):
        self.rules = load('config/newHorizonsPrimaryRewards.json')
        self.generated = GENERATOR.primary_reward_patches(self.rules)
        self.objects = self.generated['config/objects/primaryRewards.json']
        self.buildings = self.generated['config/factions/primaryRewards.json']

    def test_exact_stat_site_rewards_and_registered_generated_outputs(self):
        expected = {
            'arena': [{'attack': 10}, {'defence': 10}],
            'marlettoTower': [{'defence': 5}],
            'gardenOfRevelation': [{'knowledge': 5}],
            'libraryOfEnlightenment': [dict.fromkeys(
                ('attack', 'defence', 'spellpower', 'knowledge'), 10)],
            'mercenaryCamp': [{'attack': 5}], 'starAxis': [{'spellpower': 5}],
            'schoolOfMagic': [{'spellpower': 5}, {'knowledge': 5}],
            'schoolOfWar': [{'attack': 5}, {'defence': 5}],
            'scholar': [{'@gainedStat': 5}],
        }
        self.assertEqual(set(self.objects), {'core:' + key for key in expected})
        for key, amounts in expected.items():
            rewards = self.objects['core:' + key]['types'][key]['rewards']
            self.assertEqual([r['primary'] for r in rewards if 'primary' in r], amounts)
        module = load('Mods/new-horizons/mod.json')
        for path, content in self.generated.items():
            self.assertEqual(load('Mods/new-horizons/Content/' + path), content)
            family = 'objects' if '/objects/' in path else 'factions'
            self.assertEqual(module[family].count(path), 1)
        self.assertGreater(module['factions'].index('config/factions/primaryRewards.json'),
                           module['factions'].index('config/factions/uniqueBuildings.json'))

    def test_costs_limiters_and_scholar_rng_branches_are_preserved(self):
        base = load('config/objects/rewardableOncePerHero.json')
        base.update(load('config/objects/scholar.json'))
        for scoped, obj in self.objects.items():
            key = scoped.removeprefix('core:')
            before = base[key]['types'][key]
            patch = obj['types'][key]
            self.assertLessEqual(set(patch), {'description', 'onSelectMessage', 'rewards'})
            self.assertEqual(len(patch['rewards']), len(before['rewards']))
            for old, new in zip(before['rewards'], patch['rewards']):
                if 'primary' not in old:
                    self.assertEqual(old, new)
                else:
                    inherited = copy.deepcopy({k: v for k, v in old.items() if k not in ('primary', 'message')})
                    if key == 'libraryOfEnlightenment':
                        # The inherited source is core-scoped; the generated patch
                        # is NH-scoped and must qualify those same skill predicates.
                        for branch in inherited['limiter']['anyOf'][1:]:
                            self.assertEqual(set(branch['secondary']), {'diplomacy'})
                            branch['secondary'] = {'core:diplomacy': branch['secondary']['diplomacy']}
                    self.assertEqual(inherited,
                                     {k: v for k, v in new.items() if k not in ('primary', 'message')})
                    self.assertEqual(new['primary'], {k: v * 5 for k, v in old['primary'].items()})
            effective = {**before, **patch}
            self.assertEqual(effective['visitMode'], before['visitMode'])
            if key.startswith('schoolOf'):
                self.assertTrue(effective['canRefuse'])
                for reward in effective['rewards']:
                    self.assertEqual(reward['resources'], {'gold': -1000})
                    self.assertEqual(reward['limiter']['resources'], {'gold': 1000})

    def test_library_diplomacy_predicates_keep_scope_ranks_and_saved_rule_migration(self):
        template = self.rules['objects']['core:libraryOfEnlightenment']['types']['libraryOfEnlightenment']
        generated = self.objects['core:libraryOfEnlightenment']['types']['libraryOfEnlightenment']
        expected = [{'heroLevel': 10},
                    {'heroLevel': 8, 'secondary': {'core:diplomacy': 1}},
                    {'heroLevel': 6, 'secondary': {'core:diplomacy': 2}},
                    {'heroLevel': 4, 'secondary': {'core:diplomacy': 3}}]
        self.assertEqual(template['rewards'][0]['limiter']['anyOf'], expected)
        self.assertEqual(generated['rewards'][0]['limiter']['anyOf'], expected)
        migration = load('config/newHorizonsHeroes.json')['startingSkills']['legacySkillMigrations']['core:diplomacy']
        self.assertEqual(migration, {'kind': 'skill', 'target': 'new-horizons:diplomacy'})
        # Qualifying the copied core reference preserves legacy interpretation;
        # choosing NH directly would bypass the saved-rule migration boundary.
        self.assertEqual(load('config/objects/rewardableOncePerHero.json')['libraryOfEnlightenment']
                         ['types']['libraryOfEnlightenment']['rewards'][0]['limiter']['anyOf'],
                         [{'heroLevel': 10}, {'heroLevel': 8, 'secondary': {'diplomacy': 1}},
                          {'heroLevel': 6, 'secondary': {'diplomacy': 2}},
                          {'heroLevel': 4, 'secondary': {'diplomacy': 3}}])

    def test_only_remaining_town_primary_trainer_is_patched(self):
        self.assertEqual(set(self.buildings), {'core:fortress'})
        buildings = self.buildings['core:fortress']['town']['buildings']
        self.assertEqual(set(buildings), {'special1'})
        config = buildings['special1']['configuration']
        self.assertEqual(config['visitMode'], 'hero')
        self.assertEqual(config['rewards'][0]['primary'], {'defence': 5})
        self.assertNotIn('resetParameters', config)
        existing = load('Mods/new-horizons/Content/config/factions/uniqueBuildings.json')
        for town, stat in (('inferno', 'spellpower'), ('stronghold', 'attack')):
            self.assertEqual(existing['core:' + town]['town']['buildings']['special4']
                             ['configuration']['rewards'][0]['primary'], {stat: 5})
        reservoir = existing['core:tower']['town']['buildings']['special4']['configuration']
        self.assertEqual(reservoir['rewards'][0]['manaBuffer'], 50)
        self.assertNotIn('primary', reservoir['rewards'][0])

    def test_scale_is_repeatable_and_text_tracks_configured_units(self):
        before = copy.deepcopy(self.rules)
        self.assertEqual(GENERATOR.primary_reward_patches(self.rules), self.generated)
        self.assertEqual(self.rules, before)
        alternative = copy.deepcopy(self.rules)
        alternative['legacyPointValue'] = 7
        changed = GENERATOR.primary_reward_patches(alternative)
        sites = changed['config/objects/primaryRewards.json']
        war = sites['core:schoolOfWar']['types']['schoolOfWar']
        self.assertEqual(war['rewards'][0]['primary'], {'attack': 7})
        for text in (war['description'], war['onSelectMessage'], war['rewards'][0]['message']):
            self.assertIn('7', text)
        library = sites['core:libraryOfEnlightenment']['types']['libraryOfEnlightenment']
        self.assertEqual(set(library['rewards'][0]['primary'].values()), {14})
        self.assertIn('14', library['description'])
        for placeholder in ('{point}', '{doublePoint}'):
            self.assertNotIn(placeholder, json.dumps(changed))
        for bad in (0, -1, 1.5, True, '5', 1001):
            alternative['legacyPointValue'] = bad
            with self.subTest(value=bad), self.assertRaises(ValueError):
                GENERATOR.primary_reward_patches(alternative)

    def test_templates_and_generated_rewards_validate_with_shipped_schemas(self):
        schemas = ROOT / 'config/schemas'
        registry = Registry().with_resources(
            (path.as_uri(), Resource.from_contents(
                parse_jsonc(path.read_text()), default_specification=DRAFT4))
            for path in schemas.glob('*.json'))
        schema = load('config/schemas/newHorizonsPrimaryRewards.json')
        Draft4Validator.check_schema(schema)
        for name, value in [('newHorizonsPrimaryRewards', self.rules)]:
            Draft4Validator({'$ref': (schemas / (name + '.json')).as_uri()}, registry=registry).validate(value)
        reward_validator = Draft4Validator({'$ref': (schemas / 'rewardable.json').as_uri()}, registry=registry)
        for obj in self.objects.values():
            for configuration in obj['types'].values():
                reward_validator.validate(configuration)
        building_validator = Draft4Validator({'$ref': (schemas / 'townBuilding.json').as_uri()}, registry=registry)
        building_validator.validate(self.buildings['core:fortress']['town']['buildings']['special1'])


if __name__ == '__main__':
    unittest.main()
