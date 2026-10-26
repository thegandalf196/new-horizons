#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute The Chosen's Lua lifecycle; native tests cover engine integration."""
import unittest
import json
from pathlib import Path
import re

if __package__:
    from . import test_primary_scale_lua as lua_runner
else:
    import test_primary_scale_lua as lua_runner


def load(path):
    text = (Path(__file__).resolve().parents[2] / path).read_text(encoding='utf-8')
    strings = r'"(?:\\.|[^"\\])*"'
    text = re.sub(strings + r'|//[^\n]*|/\*[\s\S]*?\*/',
                  lambda match: match[0] if match[0].startswith('"') else '', text)
    text = re.sub(strings + r'|,(?=\s*[}\]])',
                  lambda match: match[0] if match[0].startswith('"') else '', text)
    return json.loads(text)


class TheChosenTest(unittest.TestCase):
    execute = lua_runner.PrimaryScaleLuaTest.execute

    def test_lua54_lifecycle(self):
        self.execute('lua5.4', True, 'the-chosen.lua')

    def test_luajit_lifecycle(self):
        self.execute('luajit-5.1', False, 'the-chosen.lua')

    def test_registered_current_spell_preserves_historical_definition(self):
        spells = load('Mods/new-horizons/Content/config/spells/newHorizons.json')
        rules = load('config/newHorizonsMagic.json')['spells']
        self.assertFalse(rules['new-horizons:crusade']['active'])
        self.assertEqual(rules['new-horizons:crusade']['costs'], [24] * 4)
        row = rules['new-horizons:theChosen']
        self.assertEqual(row['schools'], ['new-horizons:light'])
        self.assertEqual(row['level'], 5)
        self.assertEqual(row['costs'], [25] * 4)
        self.assertEqual(row['theChosen'], dict(baseDuration=2, maximumDuration=4,
                                               spellPowerPerRound=150, joustingPercent=5,
                                               exaltedChampionDurationBonus=1))
        self.assertEqual(spells['crusade']['levels']['expert']['range'], 'X')
        self.assertEqual(spells['crusade']['levels']['expert']['cost'], 24)
        for level in spells['theChosen']['levels'].values():
            self.assertEqual(level['range'], '0')
            self.assertEqual(level['cost'], 25)
            self.assertEqual(level['targetModifier'], {'smart': True})
            self.assertEqual(level['battleEffects'], {'theChosen': {'type': 'theChosenEffect'}})
        base = load('config/schemas/newHorizonsMagic.json')
        v3 = load('config/schemas/newHorizonsMagicV3.json')
        self.assertEqual(v3['definitions']['spell']['properties']['theChosen'],
                         {'$ref': 'vcmi:newHorizonsMagic#/definitions/theChosen'})
        self.assertEqual(set(base['definitions']['theChosen']['required']), set(row['theChosen']))
        registration = load('config/scriptsSpells.json')['theChosenEffect']
        self.assertEqual(registration['script'], 'spells/theChosen')
        self.assertEqual(set(registration['schema']['properties']), {'type', 'optional', 'indirect'})
        self.assertFalse(registration['schema']['additionalProperties'])


if __name__ == '__main__':
    unittest.main()
