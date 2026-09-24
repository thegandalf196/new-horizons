#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline v2 shape checks; not VCMI loader, spell, AI or activation proof."""
import copy
import unittest

from test_new_horizons_content import legacy_rules, load

try:
    from jsonschema import Draft4Validator
    from referencing import Registry, Resource
except ImportError as error:
    raise unittest.SkipTest('Optional offline jsonschema dependencies unavailable; native schema gates remain required') from error


class MagicV2DataTest(unittest.TestCase):
    def test_hat_description_patch_is_registered_without_changing_core_artifact(self):
        metadata = load('Mods/new-horizons/mod.json')
        path = 'config/artifacts/spellbindersHat.json'
        self.assertIn(path, metadata['artifacts'])
        patch = load('Mods/new-horizons/Content/' + path)
        description = patch['core:spellbindersHat']['text']['description']
        self.assertIn('While equipped', description)
        self.assertIn('Level 5 combat spells', description)
        self.assertIn('not spells you have learned', description)
        self.assertNotIn('bonuses', patch['core:spellbindersHat'])

    def setUp(self):
        self.v1 = load('config/schemas/newHorizonsMagic.json')
        self.v2 = load('config/schemas/newHorizonsMagicV2.json')
        registry = Registry().with_resources([
            ('vcmi:newHorizonsMagic', Resource.from_contents(self.v1)),
            ('vcmi:newHorizonsMagicV2', Resource.from_contents(self.v2)),
        ])
        self.old_validator = Draft4Validator(self.v1, registry=registry)
        self.validator = Draft4Validator(self.v2, registry=registry)
        self.rules = load('config/newHorizonsMagic.json')
        self.old_rules = legacy_rules(self.rules)
        self.formula_spell = 'core:magicArrow'

    def test_named_schemas_and_valid_v2_formula(self):
        Draft4Validator.check_schema(self.v1)
        Draft4Validator.check_schema(self.v2)
        self.validator.validate(self.rules)
        self.old_validator.validate(self.old_rules)
        self.assertFalse(self.old_validator.is_valid(self.rules))
        self.assertFalse(self.validator.is_valid(self.old_rules))

    def test_v1_stays_strict_and_v2_is_not_an_empty_context(self):
        self.old_validator.validate({})
        self.assertFalse(self.validator.is_valid({}))
        changed = copy.deepcopy(self.old_rules)
        changed['spells'][self.formula_spell]['directDamage'] = {'base': 20, 'powerCoefficient': 20}
        self.assertFalse(self.old_validator.is_valid(changed))

    def test_formula_fields_types_and_bounds(self):
        for key in ('base', 'powerCoefficient'):
            for invalid in (-1, 1000001, 0.5, 20.0, '20', None, True):
                with self.subTest(key=key, invalid=repr(invalid)):
                    changed = copy.deepcopy(self.rules)
                    changed['spells'][self.formula_spell]['directDamage'][key] = invalid
                    self.assertFalse(self.validator.is_valid(changed))
            for valid in (0, 1000000):
                changed = copy.deepcopy(self.rules)
                changed['spells'][self.formula_spell]['directDamage'][key] = valid
                self.validator.validate(changed)

    def test_missing_extra_and_null_formula(self):
        for invalid in (None, [], {}, {'base': 20}, {'base': 20, 'powerCoefficient': 20, 'divisor': 10}):
            with self.subTest(invalid=invalid):
                changed = copy.deepcopy(self.rules)
                changed['spells'][self.formula_spell]['directDamage'] = invalid
                self.assertFalse(self.validator.is_valid(changed))
        # V2 may retain legacy-effect rows without a direct-damage override.
        del self.rules['spells'][self.formula_spell]['directDamage']
        self.validator.validate(self.rules)

    def test_active_marker_is_optional_for_old_rows_and_boolean_for_new_rows(self):
        self.validator.validate(self.rules)
        changed = copy.deepcopy(self.rules)
        del changed['spells']['core:clone']['active']
        self.validator.validate(changed)
        changed['spells']['core:clone']['active'] = False
        self.validator.validate(changed)
        for invalid in (None, 0, 1, 'false'):
            changed = copy.deepcopy(self.rules)
            changed['spells']['core:clone']['active'] = invalid
            with self.subTest(invalid=repr(invalid)):
                self.assertFalse(self.validator.is_valid(changed))

    def test_existing_school_rank_and_cost_constraints_remain(self):
        for key, value in (('schools', ['core:air']), ('level', 0), ('costs', [5] * 3), ('costs', [5, None, 5, 5])):
            with self.subTest(key=key, value=value):
                changed = copy.deepcopy(self.rules)
                changed['spells'][self.formula_spell][key] = value
                self.assertFalse(self.validator.is_valid(changed))

    def test_warcasting_is_optional_but_strictly_boolean_and_v2_only(self):
        changed = copy.deepcopy(self.rules)
        changed.pop('warcasting')
        self.validator.validate(changed)
        for value in (False, True):
            changed['warcasting'] = value
            self.validator.validate(changed)
            old = copy.deepcopy(self.old_rules)
            old['warcasting'] = value
            self.assertFalse(self.old_validator.is_valid(old))
        for value in (None, 0, 1, 'true', {}, []):
            with self.subTest(value=value):
                changed['warcasting'] = value
                self.assertFalse(self.validator.is_valid(changed))

    def test_spell_point_pools_are_explicit_optional_and_strict(self):
        changed = copy.deepcopy(self.rules)
        changed.pop('spellPoints')
        self.validator.validate(changed)
        for invalid in (None, {}, [], True,
                        {'rulesetVersion': 2, 'intelligenceMaximumPercent': 130},
                        {'rulesetVersion': 1, 'intelligenceMaximumPercent': 99},
                        {'rulesetVersion': 1, 'intelligenceMaximumPercent': 1001},
                        {'rulesetVersion': 1, 'intelligenceMaximumPercent': None},
                        {'rulesetVersion': 1, 'intelligenceMaximumPercent': 130, 'unknown': 1}):
            changed['spellPoints'] = invalid
            with self.subTest(invalid=invalid):
                self.assertFalse(self.validator.is_valid(changed))
        self.assertEqual(self.rules['spellPoints'],
                         {'rulesetVersion': 1, 'intelligenceMaximumPercent': 130})

    def test_buffer_sources_do_not_multiply_normal_capacity(self):
        reservoir = load('Mods/new-horizons/Content/config/factions/uniqueBuildings.json')[
            'core:tower']['town']['buildings']['special4']['configuration']
        self.assertEqual(reservoir['visitMode'], 'once')
        self.assertEqual(reservoir['resetParameters']['weeks'], 1)
        self.assertEqual(reservoir['rewards'][0]['manaBuffer'], 50)
        self.assertNotIn('manaPercentage', reservoir['rewards'][0])
        spring = load('Mods/new-horizons/Content/config/objects/magicSpring.json')[
            'core:magicSpring']['types']['magicSpring']['rewards'][0]
        self.assertEqual(spring['manaPercentage'], 100)
        self.assertEqual(spring['manaBuffer'], 25)
        self.assertNotIn('limiter', spring)
        base_spring = load('config/objects/magicSpring.json')['magicSpring']['types']['magicSpring']
        self.assertEqual(base_spring['visitMode'], 'once')
        self.assertEqual(base_spring['resetParameters'], {'weeks': 1, 'visitors': True})
        # Preserve the base game's definition; only the curated module changes it.
        self.assertEqual(base_spring['rewards'][0]['manaPercentage'], 200)
        module = load('Mods/new-horizons/mod.json')
        self.assertIn('config/objects/magicSpring.json', module['objects'])

    def test_buffer_reward_requires_nonnegative_bounded_integer(self):
        schema = load('config/schemas/rewardable.json')
        validator = Draft4Validator(schema)
        for amount in (0, 25, 50, 2147483647):
            with self.subTest(amount=amount):
                validator.validate({'rewards': [{'manaBuffer': amount}]})
        for amount in (-1, 2147483648, 0.5, '50', None, True, [], {}):
            with self.subTest(invalid=repr(amount)):
                self.assertFalse(validator.is_valid({'rewards': [{'manaBuffer': amount}]}))
        # Buffer is an explicit reward, not a new ordinary-Mana requirement.
        validator.validate({'rewards': [{'manaPoints': 25}]})
        self.assertNotIn('manaBuffer', schema['definitions']['limiter']['properties'])


if __name__ == '__main__':
    unittest.main()
