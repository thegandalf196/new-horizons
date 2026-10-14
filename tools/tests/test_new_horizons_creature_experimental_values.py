#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Focused effective content checks; native mechanics are covered separately."""
import json
from pathlib import Path
import unittest

from jsonschema import Draft7Validator

if __package__:
    from .test_new_horizons_content import load as load_jsonc
else:
    from test_new_horizons_content import load as load_jsonc

ROOT = Path(__file__).resolve().parents[2]


def load(path):
    return json.loads((ROOT / path).read_text(encoding='utf-8'))


class CreatureExperimentalValuesTest(unittest.TestCase):
    def test_hostile_opt_in_schema_accepts_only_explicit_numeric_policy(self):
        branch = load_jsonc('config/schemas/bonusInstance.json')['allOf'][0]
        validator = Draft7Validator(branch)
        for info in (None, 0, 1):
            bonus = {'type': 'RANDOM_SPELLCASTER', 'val': 2}
            if info is not None:
                bonus['addInfo'] = info
            self.assertFalse(list(validator.iter_errors(bonus)))
        for info in (True, '1', 2, -1, [1], {}):
            self.assertTrue(list(validator.iter_errors({'type': 'RANDOM_SPELLCASTER', 'addInfo': info})))

    def test_exact_bounded_rows_and_generated_mount_order(self):
        values = load('config/newHorizonsCreatureExperimentalValues.json')
        generated = load('Mods/new-horizons/Content/config/creatures/experimentalValues.json')
        self.assertEqual(values, generated)
        self.assertEqual(set(values), {'core:griffin', 'core:royalGriffin', 'core:genie', 'core:masterGenie'})
        mounts = load('Mods/new-horizons/mod.json')['creatures']
        self.assertGreater(mounts.index('config/creatures/experimentalValues.json'), mounts.index('config/creatures/castle.json'))
        self.assertGreater(mounts.index('config/creatures/experimentalValues.json'), mounts.index('config/creatures/tower.json'))

    def test_griffin_stat_boosts_preserve_economy_mobility_and_intrinsics(self):
        values = load('config/newHorizonsCreatureExperimentalValues.json')
        base = load('Mods/new-horizons/Content/config/creatures/castle.json')
        for creature, attack, low, high, hp, speed, gold in (
                ('core:griffin', 12, 7, 10, 45, 7, 300),
                ('core:royalGriffin', 14, 9, 12, 55, 9, 450)):
            self.assertEqual(values[creature], {'attack': attack, 'defense': attack,
                                             'damage': {'min': low, 'max': high}, 'hitPoints': hp})
            effective = {**base[creature], **values[creature]}
            self.assertEqual((effective['speed'], effective['initiative'], effective['growth'], effective['cost']),
                             (speed, speed, 5, {'gold': gold}))
            self.assertNotIn('abilities', values[creature])
            self.assertEqual(load('config/newHorizonsCreatureCategories.json')['creatures'][creature], 'elite')

    def test_genie_has_existing_three_cast_blessing_and_master_only_opts_into_curses(self):
        values = load('config/newHorizonsCreatureExperimentalValues.json')
        abilities = values['core:genie']['abilities']
        self.assertEqual(abilities, {
            'casts': {'type': 'CASTS', 'val': 3},
            'spellsLength': {'type': 'CREATURE_ENCHANT_POWER', 'val': 5},
            'randomSpellcaster': {'type': 'RANDOM_SPELLCASTER', 'val': 2},
        })
        self.assertEqual(values['core:masterGenie'], {'abilities': {
            'randomSpellcaster': {'type': 'RANDOM_SPELLCASTER', 'val': 2, 'addInfo': 1}}})
        for creature in ('core:genie', 'core:masterGenie'):
            self.assertEqual(set(values[creature]), {'abilities'})


if __name__ == '__main__':
    unittest.main()
