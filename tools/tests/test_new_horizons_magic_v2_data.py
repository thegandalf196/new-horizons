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


if __name__ == '__main__':
    unittest.main()
