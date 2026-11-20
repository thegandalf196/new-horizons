#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline Sorcery declarations; native execution and rendered assets are separate gates."""
import json
from pathlib import Path
import unittest

from jsonschema import Draft4Validator
from .test_new_horizons_content import load, validate_rules

ROOT = Path(__file__).resolve().parents[2]
PERKS = ('arcaneWard', 'manaSiphon', 'deepReserves', 'formulaRecovery',
         'arcaneCycle', 'afterglow', 'crescendo', 'cleansingWeave',
         'grandWeaving', 'spellstorm')


class SorceryRevisionDataTest(unittest.TestCase):
    def test_ordinary_roster_is_twelve_with_distinct_new_identities(self):
        rules = load('config/newHorizonsMagic.json')
        validate_rules(rules)
        ordinary = {key: row['level'] for key, row in rules['spells'].items()
                    if row.get('active', True) and row.get('heroAccess', True)
                    and row.get('ordinaryAcquisition', True)
                    and 'new-horizons:sorcery' in row['schools']}
        self.assertEqual(ordinary, {
            'core:magicArrow': 1, 'core:dispel': 1, 'new-horizons:focusMagic': 1,
            'new-horizons:summonLivingArmor': 1, 'new-horizons:stormOfDaggers': 2,
            'new-horizons:transfigureMatter': 2, 'core:slow': 3, 'core:teleport': 3,
            'core:implosion': 4, 'new-horizons:phantomArmy': 4,
            'new-horizons:forcedRecall': 5, 'new-horizons:forceLance': 5})
        self.assertFalse(rules['spells']['new-horizons:massSlow']['ordinaryAcquisition'])
        self.assertEqual(rules['spells']['new-horizons:forceLance']['directDamage'],
                         {'base': 100, 'powerCoefficient': 20})
        self.assertEqual(rules['spells']['new-horizons:disintegrate']['directDamage'],
                         {'base': 180, 'powerCoefficient': 25})

    def test_new_recipes_have_native_factory_registrations(self):
        content = load('Mods/new-horizons/Content/config/spells/newHorizons.json')
        scripts = load('config/scriptsSpells.json')
        schema = load('config/schemas/spell.json')
        for name, mana, effect in (('focusMagic', 6, 'focusMagicEnchantment'),
                                   ('forcedRecall', 25, 'forcedRecall'),
                                   ('forceLance', 30, 'forceLance')):
            spell = content[name]
            Draft4Validator(schema).validate(spell)
            self.assertEqual(spell['targetType'], 'CREATURE')
            for row in spell['levels'].values():
                self.assertEqual(row['cost'], mana)
                self.assertEqual(list(row['battleEffects'].values()), [{'type': 'core:' + effect}])
            self.assertEqual(scripts[effect]['implements'], 'spellEffect')
            self.assertEqual(scripts[effect]['script'], 'spells/' + effect)
        self.assertEqual(load('config/newHorizonsMagic.json')['spells']
                         ['new-horizons:focusMagic']['focusMagic'], {'version': 2})
        precision = load('config/spells/timed.json')['precision']
        self.assertEqual(content['focusMagic']['animation'], precision['animation'])
        self.assertEqual(content['focusMagic']['sounds'], precision['sounds'])

    def test_retired_identities_have_no_executable_content(self):
        content = load('Mods/new-horizons/Content/config/spells/newHorizons.json')
        rules = load('config/newHorizonsMagic.json')['spells']
        scripts = load('config/scriptsSpells.json')
        for name in ('spellLock', 'timeStop', 'counterspell'):
            self.assertNotIn('new-horizons:' + name, rules)
            self.assertNotIn(name, content)
            self.assertNotIn(name, scripts)

    def test_metamagic_registry_and_all_starting_references_are_current(self):
        rules = load('config/newHorizonsPerks.json')
        skill = rules['skills']['new-horizons:metamagic']
        pool = skill['perks']
        self.assertEqual([p['id'] for p in pool], ['new-horizons:metamagic.' + p for p in PERKS])
        self.assertEqual([p['requires'] for p in pool], ['basic'] * 4 + ['advanced'] * 4 + ['expert'] * 2)
        self.assertNotIn('Metamagic point', json.dumps(skill))
        self.assertEqual(rules['metamagicPolicy']['version'], 7)
        self.assertEqual(rules['metamagicPolicy']['usesPerCombatByRank'], [0, 1, 2, 3])
        self.assertEqual(rules['metamagicPolicy']['halonAdditionalUses'], 1)
        Draft4Validator(load('config/schemas/newHorizonsPerks.json')).validate(rules)
    def test_current_data_requires_ordered_one_per_tier_policy(self):
        rules = load('config/newHorizonsPerks.json')
        self.assertEqual(rules['perkProgressionPolicyVersion'], 2)
        self.assertEqual(rules['fourOfferProgressionVersion'], 1)
        self.assertEqual(rules['maxPerksPerSkill'], 3)
        validator = Draft4Validator(load('config/schemas/newHorizonsPerks.json'))
        for invalid in (None, 0, 3, True, '2'):
            self.assertTrue(list(validator.iter_errors(
                {**rules, 'perkProgressionPolicyVersion': invalid})))

    def test_focus_specialties_are_fixed_and_retired_bonus_is_cleared(self):
        patches = load('Mods/new-horizons/Content/config/heroes/halon.json')
        for hero in ('cyra', 'brissa', 'terek'):
            patch = patches['core:' + hero]
            self.assertEqual(patch['spellbook'], ['new-horizons:focusMagic'])
            self.assertIsNone(patch['specialty']['spellFixedAdditive'])
            self.assertEqual(patch['specialty']['bonuses']['focusMagic'], {
                'type': 'SPECIAL_SPELL_LEV', 'subtype': 'new-horizons:focusMagic',
                'val': 10, 'valueType': 'BASE_NUMBER'})

    def test_halon_fresh_start_matches_his_spellweaver_specialty(self):
        starts = load('config/newHorizonsHeroes.json')['startingSkills']
        self.assertEqual(starts['startingDevelopmentProfiles']['core:halon']['skills'], [
            {'skill': 'new-horizons:metamagic', 'rank': 1},
            {'skill': 'new-horizons:warcasting', 'rank': 1}])
        self.assertEqual(starts['startingPerkProfiles']['core:halon'], [{
            'skill': 'new-horizons:metamagic',
            'perk': 'new-horizons:metamagic.formulaRecovery'}])
        halon = load('Mods/new-horizons/Content/config/heroes/halon.json')['core:halon']
        self.assertEqual(halon['spellbook'], ['core:dispel'])
        self.assertEqual(halon['texts']['specialty']['name'], 'Spellweaver')


if __name__ == '__main__':
    unittest.main()
