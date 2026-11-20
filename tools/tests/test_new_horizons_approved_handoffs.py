#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Accepted faction handoff contracts; gameplay/rendered acceptance is separate."""
import copy
import json
from pathlib import Path
import unittest

from jsonschema import Draft4Validator

ROOT = Path(__file__).resolve().parents[2]


def load(path):
    return json.loads((ROOT / path).read_text(encoding='utf-8'))


class ApprovedFactionHandoffsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rules = load('config/newHorizonsPerks.json')
        cls.validator = Draft4Validator(load('config/schemas/newHorizonsPerks.json'))

    def test_exact_replacement_rosters_and_strict_tiers(self):
        expected = {
            'metamagic': ('arcaneWard', 'manaSiphon', 'deepReserves', 'formulaRecovery',
                          'arcaneCycle', 'afterglow', 'crescendo', 'cleansingWeave',
                          'grandWeaving', 'spellstorm'),
            'divineMandate': ('enduringPrayer', 'joyfulDevotion', 'fearlessAdvance',
                             'unshakableFaith', 'communionOfSaints',
                             'righteousSentence', 'ferventInvocation', 'sacredAccord',
                             'antiphonalPrayer', 'livingLiturgy'),
        }
        for skill, suffixes in expected.items():
            pool = self.rules['skills']['new-horizons:' + skill]['perks']
            self.assertEqual([p['id'] for p in pool],
                             ['new-horizons:' + skill + '.' + s for s in suffixes])
            self.assertEqual([p['requires'] for p in pool],
                             ['basic'] * 4 + ['advanced'] * 4 + ['expert'] * 2)
        self.assertEqual(self.rules['perkProgressionPolicyVersion'], 2)
        self.assertEqual(self.rules['maxPerksPerSkill'], 3)
        ids = [p['id'] for s in self.rules['skills'].values() for p in s['perks']]
        self.assertEqual(len(ids), len(set(ids)))

    def test_rank_policies_capture_exact_approved_values(self):
        self.validator.validate(self.rules)
        self.assertEqual(self.rules['divineMandatePolicy'], {
            'version': 4, 'potencyFaithBonusByRank': [0, 0, 50, 100],
            'ferventInvocationAdditionalFaith': 100, 'chosen': True})
        self.assertEqual(self.rules['metamagicPolicy'], {
            'version': 7, 'usesPerCombatByRank': [0, 1, 2, 3], 'followUpMustDiffer': True,
            'halonAdditionalUses': 1,
            'arcaneWardSpellPowerMultiplier': 5, 'manaSiphonMana': 4,
            'deepReservesAdditionalUses': 1, 'formulaReserveMana': 3,
            'arcaneCycleRoundInterval': 3, 'afterglowAdditionalRounds': 1,
            'crescendoSpellPowerPerSpentUse': 5, 'cleansingWeaveEffects': 1,
            'grandWeavingAdditionalUses': 2, 'spellstormManaReductionPercent': 25,
            'spellstormDoubleCastThreshold': 3})

    def test_policy_contract_rejects_missing_extra_and_misranked_values(self):
        for name in ('metamagicPolicy', 'divineMandatePolicy'):
            for field, value in self.rules[name].items():
                rules = copy.deepcopy(self.rules)
                del rules[name][field]
                with self.subTest(policy=name, missing=field):
                    self.assertTrue(list(self.validator.iter_errors(rules)))
                for invalid in (True, None, '100', [0, 30, 45], [0, 30, 45, 61]):
                    if invalid is value:
                        continue
                    rules = copy.deepcopy(self.rules)
                    rules[name][field] = invalid
                    with self.subTest(policy=name, field=field, invalid=invalid):
                        self.assertTrue(list(self.validator.iter_errors(rules)))
            rules = copy.deepcopy(self.rules)
            rules[name]['unexpected'] = 1
            self.assertTrue(list(self.validator.iter_errors(rules)))

    def test_runtime_module_mirrors_skill_help_and_prayer_localization(self):
        module = load('Mods/new-horizons/mod.json')
        skills = load('config/newHorizonsSkills.json')
        texts = load('config/newHorizonsCombatTexts.json')
        self.assertEqual(module['settings']['heroes']['newHorizonsPerks'], self.rules)
        for skill in ('metamagic', 'divineMandate'):
            self.assertEqual(module['skills'][skill], skills[skill])
            registry = self.rules['skills']['new-horizons:' + skill]
            for rank in ('basic', 'advanced', 'expert'):
                self.assertEqual(registry['ranks'][rank]['description'],
                                 skills[skill][rank]['description'])
            for perk in registry['perks']:
                self.assertLessEqual(len(perk['description'].split()), 25)
                self.assertEqual(perk['effect']['description'], perk['description'])
        for suffix in ('divinePurpose', 'dauntlessAssault', 'steadfastSoul',
                       'vicariousRedemption', 'unbrokenVow', 'heavensJudgment'):
            key = 'new-horizons.consecratedArms.prayers.' + suffix
            self.assertEqual(module['translations'][key], texts[key])
        log = texts['new-horizons.logs.consecratedArmsGranted']
        self.assertEqual(log.count('%s'), 2)
        self.assertEqual(log.count('%d'), 2)
        self.assertNotIn('combatStatus', skills['divineMandate'])


if __name__ == '__main__':
    unittest.main()
