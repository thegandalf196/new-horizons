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
        self.v3 = load('config/schemas/newHorizonsMagicV3.json')
        registry = Registry().with_resources([
            ('vcmi:newHorizonsMagic', Resource.from_contents(self.v1)),
            ('vcmi:newHorizonsMagicV2', Resource.from_contents(self.v2)),
            ('vcmi:newHorizonsMagicV3', Resource.from_contents(self.v3)),
        ])
        self.registry = registry
        self.old_validator = Draft4Validator(self.v1, registry=registry)
        self.validator = Draft4Validator(self.v2, registry=registry)
        self.v3_rules = load('config/newHorizonsMagic.json')
        self.rules = copy.deepcopy(self.v3_rules)
        self.rules['rulesetVersion'] = 2
        self.rules.pop('schoolRankPowerCoefficientPercent')
        self.rules.pop('spellcraftEfficiencyPercent')
        self.rules['spells']['core:quicksand'].pop('selectedPlacement')
        self.rules['spells']['core:earthquake'].pop('earthquake')
        self.rules['spells'] = {key: row for key, row in self.rules['spells'].items()
                                if 'variant' not in row}
        self.old_rules = legacy_rules(self.rules)
        self.formula_spell = 'core:magicArrow'

    def test_named_schemas_and_valid_v2_formula(self):
        Draft4Validator.check_schema(self.v1)
        Draft4Validator.check_schema(self.v2)
        Draft4Validator.check_schema(self.v3)
        self.validator.validate(self.rules)
        Draft4Validator(self.v3, registry=self.registry).validate(self.v3_rules)
        self.old_validator.validate(self.old_rules)
        self.assertFalse(self.old_validator.is_valid(self.rules))
        self.assertFalse(self.validator.is_valid(self.old_rules))

    def test_earthquake_v3_parameters_are_bounded_and_absent_from_legacy(self):
        validator = Draft4Validator(self.v3, registry=self.registry)
        row = self.v3_rules['spells']['core:earthquake']
        self.assertEqual(row['level'], 3)
        self.assertEqual(row['costs'], [12] * 4)
        self.assertEqual(row['earthquake'], {
            'radius': 2, 'duration': 3, 'movementCost': 1,
            'structuralDamage': 100, 'baseSections': 2, 'maxSections': 4,
            'powerPerSection': 80, 'baseDamage': 30,
            'powerNumerator': 4, 'powerDivisor': 5,
        })
        for field, invalid in (('radius', 3), ('duration', 0),
                               ('structuralDamage', 0), ('movementCost', 2)):
            changed = copy.deepcopy(self.v3_rules)
            changed['spells']['core:earthquake']['earthquake'][field] = invalid
            self.assertFalse(validator.is_valid(changed))
        markerless = copy.deepcopy(self.v3_rules)
        markerless['spells']['core:earthquake'].pop('earthquake')
        self.assertTrue(validator.is_valid(markerless))
        legacy = copy.deepcopy(self.rules)
        legacy['spells']['core:earthquake']['earthquake'] = row['earthquake']
        self.assertFalse(self.validator.is_valid(legacy))

    def test_mass_variants_preserve_base_school_level_and_triple_cost(self):
        validator = Draft4Validator(self.v3, registry=self.registry)
        for variant_id, base_id, skill_id, perk_id, active, power_percent in (
                ('new-horizons:massBless', 'core:bless',
                 'new-horizons:lightMagic', 'new-horizons:lightMagic.litany', True, 100),
                ('new-horizons:massCurse', 'core:curse',
                 'new-horizons:shadowMagic', 'new-horizons:shadowMagic.grandMalediction', True, 100),
                ('new-horizons:massSorrow', 'core:sorrow',
                 'new-horizons:shadowMagic', 'new-horizons:shadowMagic.grandMalediction', True, 100),
                ('new-horizons:massRegeneration', 'new-horizons:regeneration',
                 'new-horizons:natureMagic', 'new-horizons:natureMagic.verdantCommunion', True, 100),
                ('new-horizons:massSlow', 'core:slow',
                 'new-horizons:sorceryMagic', 'new-horizons:sorceryMagic.temporalField', True, 60)):
            with self.subTest(variant=variant_id):
                row = self.v3_rules['spells'][variant_id]
                base = self.v3_rules['spells'][base_id]
                self.assertEqual(row['schools'], base['schools'])
                self.assertEqual(row['level'], base['level'])
                self.assertEqual(row['costs'], [3 * cost for cost in base['costs']])
                self.assertIs(row.get('active', True), active)
                self.assertFalse(row['ordinaryAcquisition'])
                self.assertEqual(row['variant'], {
                    'base': base_id, 'skill': skill_id,
                    'perk': perk_id, 'powerPercent': power_percent,
                })
                for invalid in (None, 101, '100', True):
                    changed = copy.deepcopy(self.v3_rules)
                    changed['spells'][variant_id]['variant']['powerPercent'] = invalid
                    self.assertFalse(validator.is_valid(changed))
                changed = copy.deepcopy(self.v3_rules)
                if variant_id == 'new-horizons:massSlow':
                    changed['spells'][variant_id]['variant']['base'] = 'core:bless'
                else:
                    changed['spells'][variant_id]['variant']['powerPercent'] = 60
                self.assertFalse(validator.is_valid(changed))

    def test_mass_slow_reuses_base_slow_effects_and_art(self):
        variants = load('Mods/new-horizons/Content/config/spells/massVariants.json')
        timed_spells = load('config/spells/timed.json')
        mass_slow = variants['massSlow']
        slow = timed_spells['slow']

        self.assertEqual(mass_slow['animation'], slow['animation'])
        self.assertEqual(mass_slow['sounds'], slow['sounds'])
        self.assertEqual(mass_slow['graphics'], {
            'iconBook': 'SPELLS.def:0:54',
            'iconScroll': 'SPELLSCR.def:0:54',
            'iconEffect': 'SPELLINT.def:0:55',
            'iconImmune': 'SPELLINT.def:0:55',
            'iconScenarioBonus': 'SPELLBON.def:0:54',
        })
        self.assertEqual(mass_slow['counters'], slow['counters'])
        self.assertEqual(mass_slow['flags'], slow['flags'])
        self.assertEqual(mass_slow['targetCondition'], slow['targetCondition'])

        for rank, source_rank, cost in (
                ('none', 'base', 12), ('basic', 'base', 12),
                ('advanced', 'advanced', 9), ('expert', 'expert', 9)):
            with self.subTest(rank=rank):
                level = mass_slow['levels'][rank]
                expected_effects = copy.deepcopy(slow['levels']['base']['effects'])
                for effect_name, patch in slow['levels'][source_rank].get('effects', {}).items():
                    expected_effects[effect_name].update(patch)
                self.assertEqual(level['range'], 'X')
                self.assertEqual(level['cost'], cost)
                self.assertEqual(level['effects'], expected_effects)
                self.assertEqual(level['targetModifier'], {'smart': True})
                self.assertIn('60% of the final Initiative reduction ordinary Slow produces',
                              level['description'])
                self.assertIn('after rank, caps, and specialties', level['description'])
                self.assertIn('duration is unchanged', level['description'])
                self.assertIn('Granted only by Temporal Field', level['description'])
                self.assertIn('three times Slow\'s listed Mana before Wisdom',
                              level['description'])

    def test_mass_regeneration_reuses_base_effect_and_resources(self):
        variants = load('Mods/new-horizons/Content/config/spells/massVariants.json')
        spells = load('Mods/new-horizons/Content/config/spells/newHorizons.json')
        mass_regeneration = variants['massRegeneration']
        regeneration = spells['regeneration']

        self.assertEqual(mass_regeneration['school'], regeneration['school'])
        self.assertEqual(mass_regeneration['level'], regeneration['level'])
        self.assertEqual(mass_regeneration['graphics'], regeneration['graphics'])
        self.assertEqual(mass_regeneration['animation'], regeneration['animation'])
        self.assertEqual(mass_regeneration['sounds'], regeneration['sounds'])
        self.assertTrue(mass_regeneration['flags']['positive'])
        self.assertEqual(mass_regeneration['targetCondition']['noneOf'], {
            'bonus.NON_LIVING': 'absolute',
            'bonus.MECHANICAL': 'absolute',
            'bonus.SIEGE_WEAPON': 'absolute',
            'bonus.UNDEAD': 'absolute',
        })

        for rank in ('none', 'basic', 'advanced', 'expert'):
            with self.subTest(rank=rank):
                level = mass_regeneration['levels'][rank]
                base_level = regeneration['levels'][rank]
                self.assertEqual(level['range'], 'X')
                self.assertEqual(level['cost'], 3 * base_level['cost'])
                self.assertEqual(level['battleEffects'], base_level['battleEffects'])
                self.assertEqual(level['targetModifier'], {'smart': True})
                self.assertIn('Nature Magic rank scales only the Spell Power term',
                              level['description'])
                self.assertIn('Herbalist adds 10 percentage points before the cap',
                              level['description'])
                self.assertIn('Granted only by Verdant Communion', level['description'])

    def test_v3_requires_exact_school_rank_coefficients_and_v2_rejects_them(self):
        v3_validator = Draft4Validator(self.v3, registry=self.registry)
        v3_validator.validate(self.v3_rules)
        self.assertFalse(self.validator.is_valid(self.v3_rules))
        missing = copy.deepcopy(self.v3_rules)
        missing.pop('schoolRankPowerCoefficientPercent')
        self.assertFalse(v3_validator.is_valid(missing))
        for rank, expected in enumerate((100, 115, 130, 145)):
            with self.subTest(rank=rank):
                self.assertEqual(self.v3_rules['schoolRankPowerCoefficientPercent'][rank], expected)
                changed = copy.deepcopy(self.v3_rules)
                changed['schoolRankPowerCoefficientPercent'][rank] = expected + 1
                self.assertFalse(v3_validator.is_valid(changed))
        for invalid in (None, [], [100, 115, 130], [100, 115, 130, 145, 160]):
            with self.subTest(invalid=invalid):
                changed = copy.deepcopy(self.v3_rules)
                changed['schoolRankPowerCoefficientPercent'] = invalid
                self.assertFalse(v3_validator.is_valid(changed))

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
