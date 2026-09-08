#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Field shape only: not vmap transport, version, registry or runtime proof."""
import copy
import unittest

from test_new_horizons_content import load

try:
    from jsonschema import Draft4Validator
    from referencing import Registry, Resource
except ImportError as error:
    raise unittest.SkipTest('Optional offline schema dependencies unavailable') from error


class MapMagicSchemaTest(unittest.TestCase):
    def setUp(self):
        self.schema = load('config/schemas/newHorizonsMapMagicOverride.json')
        registry = Registry().with_resources([
            ('vcmi:' + name, Resource.from_contents(load('config/schemas/' + name + '.json')))
            for name in ('newHorizonsMagic', 'newHorizonsMagicV2')
        ])
        self.validator = Draft4Validator(self.schema, registry=registry)
        self.v1 = load('config/newHorizonsMagic.json')
        self.v2 = copy.deepcopy(self.v1)
        self.v2['rulesetVersion'] = 2
        self.v2['spells']['new-horizons:magicMissile'] = {
            'schools': ['new-horizons:sorcery'], 'level': 1, 'costs': [5] * 4,
            'directDamage': {'base': 20, 'powerCoefficient': 20},
        }

    def test_schema_and_explicit_legacy_contexts(self):
        Draft4Validator.check_schema(self.schema)
        self.validator.validate(None)
        self.validator.validate({})

    def test_complete_v1_and_v2_shapes(self):
        self.validator.validate(self.v1)
        self.validator.validate(self.v2)

    def test_wrong_field_types(self):
        for value in (False, True, 0, 1.5, '', [], [self.v1]):
            with self.subTest(value=value):
                self.assertFalse(self.validator.is_valid(value))

    def test_partial_context_cannot_borrow_installed_fields(self):
        for value in ({'rulesetVersion': 1}, {'spells': {'core:magicArrow': {'level': 2}}}):
            self.assertFalse(self.validator.is_valid(value))

    def test_v1_cannot_carry_v2_formula(self):
        value = copy.deepcopy(self.v2)
        value['rulesetVersion'] = 1
        self.assertFalse(self.validator.is_valid(value))

    def test_v2_formula_remains_strict(self):
        for formula in (None, {'base': 20}, {'base': -1, 'powerCoefficient': 20}):
            value = copy.deepcopy(self.v2)
            value['spells']['new-horizons:magicMissile']['directDamage'] = formula
            self.assertFalse(self.validator.is_valid(value))


if __name__ == '__main__':
    unittest.main()
