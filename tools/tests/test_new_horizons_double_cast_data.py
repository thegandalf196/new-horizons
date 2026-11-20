#!/usr/bin/env python3
"""Metamagic 6 (Double Cast, policy 7) and Consecrated Arms 4 (Chosen Prayers) data.

Captured policy 6 / 3 documents stay executable for existing saves; the pinned
fixture keeps their shipped text and values under test.
"""
import json
from pathlib import Path
import unittest

from jsonschema import Draft4Validator

ROOT = Path(__file__).resolve().parents[2]
RANKS = ("basic", "advanced", "expert")
FIXTURE = "test/testdata/vcmi-test/content/test/newHorizonsOverkillRandomPrayerRules.json"
EXPECTED_PERKS = (
    ('arcaneWard', 'Arcane Ward', 'basic', 'Spell Power × 5'),
    ('manaSiphon', 'Mana Siphon', 'basic', 'drains 4 Mana'),
    ('deepReserves', 'Deep Reserves', 'basic', '+1 Metamagic use per combat'),
    ('formulaRecovery', 'Formula Reserve', 'basic', 'Restore 3 Mana'),
    ('arcaneCycle', 'Arcane Cycle', 'advanced', 'every third round'),
    ('afterglow', 'Afterglow', 'advanced', 'one additional round'),
    ('crescendo', 'Crescendo', 'advanced', '+5 Spell Power'),
    ('cleansingWeave', 'Cleansing Weave', 'advanced', 'one negative magical effect'),
    ('grandWeaving', 'Grand Weaving', 'expert', '+2 Metamagic uses per combat'),
    ('spellstorm', 'Spellstorm', 'expert', '25% less Mana'),
)


def load(path):
    return json.loads((ROOT / path).read_text(encoding="utf-8"))


class DoubleCastDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rules = load("config/newHorizonsPerks.json")
        cls.schema = load("config/schemas/newHorizonsPerks.json")
        cls.historical = load("test/testdata/vcmi-test/content/test/newHorizonsHistoricalFactionPerks.json")
        cls.overkill = load(FIXTURE)

    def test_current_metamagic_describes_double_cast_and_ten_perks(self):
        skill = self.rules['skills']['new-horizons:metamagic']
        entities = load('config/newHorizonsSkills.json')['metamagic']
        self.assertIn('immediately cast one more combat spell', skill['description'])
        self.assertIn('without spending a use', skill['description'])
        for rank, uses in zip(RANKS, ('1 double-cast use', '2 double-cast uses', '3 double-cast uses')):
            row = skill['ranks'][rank]
            self.assertIn(f'{uses} per combat', row['description'])
            self.assertIn('normal Mana cost', row['description'])
            self.assertEqual(row['description'], row['effect']['description'])
            self.assertEqual(row['description'], entities[rank]['description'])
            self.assertIn('cannot issue an Order and cannot trigger another double cast', row['details'])
            self.assertIn('forfeits it without spending a use', row['details'])
            for retired in ('Overflow', 'friendly damage', 'retry', 'unravel'):
                self.assertNotIn(retired, row['description'] + row['details'])
        perks = skill['perks']
        self.assertEqual([p['id'].split('.')[-1] for p in perks], [row[0] for row in EXPECTED_PERKS])
        for perk, (key, name, rank, text) in zip(perks, EXPECTED_PERKS):
            with self.subTest(perk=key):
                self.assertEqual(perk['name'], name)
                self.assertEqual(perk['requires'], rank)
                self.assertIn(text, perk['description'])
                self.assertEqual(perk['description'], perk['effect']['description'])

    def test_double_cast_perks_never_reuse_a_retired_metamagic_identity(self):
        current = {p['id'] for p in self.rules['skills']['new-horizons:metamagic']['perks']}
        retired = {p['id'] for p in self.overkill['skills']['new-horizons:metamagic']['perks']}
        retired |= {p['id'] for p in self.historical['skills']['new-horizons:metamagic']['perks']}
        retired |= {'new-horizons:metamagic.' + key for key in (
            'manaRecycling', 'purgingFormula', 'hexWeaving', 'widenSpell', 'matterbreaker',
            'magicalMemory', 'arcaneRupture', 'arcanePerfection', 'spellEcho', 'countersequence')}
        self.assertIn('new-horizons:metamagic.formulaReserve', retired)
        self.assertEqual(len(current), 10)
        self.assertFalse(current & retired)
        names = {p['name'] for p in self.rules['skills']['new-horizons:metamagic']['perks']}
        wisdom = {p['name'] for p in self.rules['skills']['new-horizons:wisdom']['perks']}
        self.assertFalse(names & wisdom)

    def test_double_cast_policy_rejects_incomplete_and_out_of_range_values(self):
        validator = Draft4Validator(self.schema)
        current = self.rules['metamagicPolicy']
        for key in current:
            with self.subTest(missing=key):
                policy = {k: v for k, v in current.items() if k != key}
                self.assertTrue(list(validator.iter_errors({**self.rules, 'metamagicPolicy': policy})))
        for key, value in (('halonAdditionalUses', 4), ('halonAdditionalUses', -1),
                           ('arcaneCycleRoundInterval', 0), ('spellstormManaReductionPercent', 91),
                           ('spellstormDoubleCastThreshold', 0), ('manaSiphonMana', 1.5),
                           ('formulaReserveMana', '3'), ('followUpMustDiffer', 1), ('usesPerCombatByRank', [1, 1, 2, 3]),
                           ('usesPerCombatByRank', [0, 0, 2, 3]), ('usesPerCombatByRank', [0, 1, 2]),
                           ('usesPerCombatByRank', [0, 1, 2, 6]), ('removalChancePercent', [0, 20, 30, 40])):
            with self.subTest(key=key, value=value):
                policy = {**current, key: value}
                self.assertTrue(list(validator.iter_errors({**self.rules, 'metamagicPolicy': policy})))
        for chosen in (False, 1, None):
            policy = {**self.rules['divineMandatePolicy'], 'chosen': chosen}
            self.assertTrue(list(validator.iter_errors({**self.rules, 'divineMandatePolicy': policy})))

    def test_captured_overkill_and_random_prayer_fixture_stays_valid(self):
        self.assertEqual(self.overkill['metamagicPolicy']['version'], 6)
        self.assertEqual(self.overkill['divineMandatePolicy']['version'], 3)
        captured = json.loads(json.dumps(self.rules))
        for key in ('metamagicPolicy', 'divineMandatePolicy'):
            captured[key] = self.overkill[key]
        captured['skills'].update(self.overkill['skills'])
        Draft4Validator(self.schema).validate(captured)
        perks = {p['id'].split('.')[-1]: p for p in
                 self.overkill['skills']['new-horizons:divineMandate']['perks']}
        self.assertIn('two different uniformly random prayers', perks['antiphonalPrayer']['description'])
        self.assertIn('Sample without replacement', perks['antiphonalPrayer']['details'])

    def test_metamagic_overflow_policy_preserves_captured_countermagic(self):
        validator = Draft4Validator(self.schema)
        current = self.overkill['metamagicPolicy']
        historical = {
            'version': 4, 'removalChancePercent': [0, 20, 30, 40],
            'damagePerMana': 10, 'restorationPerMana': 15,
            'halonRemovalChanceBonusPercent': 5,
        }
        overflow = {
            'version': 5, 'removalChancePercent': [0, 20, 30, 40],
            'damagePerMana': 10, 'restorationPerMana': 15,
            'halonRemovalChanceBonusPercent': 5,
            'arcaneOverflowPercentByRank': [0, 25, 50, 75],
        }
        for policy in (historical, overflow):
            validator.validate({**self.rules, 'metamagicPolicy': policy})
        for policy in (historical, overflow):
            for field in ('friendlyDamageReductionPercentByRank',
                          'debuffRetryChancePercentByRank'):
                invalid_policy = {**policy, field: [0, 30, 45, 60]}
                self.assertTrue(list(validator.iter_errors(
                    {**self.rules, 'metamagicPolicy': invalid_policy})))
        invalid = (
            {**historical, 'arcaneOverflowPercentByRank': [0, 25, 50, 75]},
            {**historical, 'version': 5},
            *({**policy, 'arcaneOverflowPercentByRank': values}
              for policy in (overflow, current)
              for values in ([0, 25, 50, 100], [0, True, 50, 75], [0, 25, 50])),
        )
        for policy in invalid:
            with self.subTest(policy=policy):
                self.assertTrue(list(validator.iter_errors({**self.rules, 'metamagicPolicy': policy})))

    def test_captured_overkill_metamagic_keeps_three_core_channels_and_ten_perks(self):
        skill = self.overkill['skills']['new-horizons:metamagic']
        for rank, percent, friendly in zip(RANKS, (30, 45, 60), (70, 55, 40)):
            row = skill['ranks'][rank]
            self.assertIn(f'Arcane Overflow transfers {percent}%', row['description'])
            self.assertIn('primary single-target spell overkill', row['description'])
            self.assertIn('nearest other enemy', row['description'])
            self.assertIn(f'area, chain and global spells deal {friendly}% friendly damage',
                          row['description'])
            self.assertIn(f'single-target debuffs have a {percent}% chance to retry once',
                          row['description'])
            self.assertIn('normal resistance', row['description'])
            self.assertEqual(row['description'], row['effect']['description'])
            self.assertIn('damage only and cannot recurse', row['details'])
            self.assertIn('Each secondary keeps normal magical defenses', row['details'])
            self.assertIn('immunity is not a resisted debuff', row['details'])
            self.assertIn('No extra Mana, Hero Action or manual secondary target', row['details'])
            self.assertIn('Unraveling and reconstitution require their respective perks',
                          row['details'])
        template = load('config/newHorizonsCombatTexts.json')['new-horizons.metamagic.arcaneOverflow.preview']
        self.assertEqual(template, ' | Arcane Overflow: %s, %s potential damage')
        self.assertEqual(tuple(p['id'].split('.')[-1] for p in skill['perks']), (
            'conservedFormula', 'disruptiveCasting', 'unravelingMagic',
            'reconstitutingMagic', 'spellSculpting', 'widenSpells', 'levitation',
            'arcaneBallistics', 'arcaneCascade', 'empoweredSpells'))
        perks = {p['id'].split('.')[-1]: p for p in skill['perks']}
        for key in ('unravelingMagic', 'reconstitutingMagic'):
            self.assertEqual(perks[key]['requires'], 'basic')
            self.assertIn('20/30/40%', perks[key]['description'])
        self.assertIn('10 magical damage', perks['unravelingMagic']['description'])
        self.assertIn('15 shared healing and resurrection HP',
                      perks['reconstitutingMagic']['description'])


if __name__ == "__main__":
    unittest.main()
