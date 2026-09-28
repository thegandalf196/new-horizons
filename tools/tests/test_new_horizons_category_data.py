#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline partial-roster/schema/composition checks, not native or gameplay proof."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from jsonschema import Draft4Validator, ValidationError
from test_new_horizons_content import load

ROOT = Path(__file__).resolve().parents[2]


class CreatureCategoryDataTest(unittest.TestCase):
    def setUp(self):
        self.rules = load('config/newHorizonsCreatureCategories.json')
        self.validator = Draft4Validator(load('config/schemas/newHorizonsCreatureCategories.json'))

    def test_schema_presence_and_absence(self):
        for rules in (None, {}, self.rules):
            self.validator.validate(rules)
        self.assertEqual(self.rules['sourceRulesetId'], 'new-horizons:creatureRules')
        self.assertEqual(self.rules['rulesetVersion'], 2)
        properties = load('config/schemas/gameSettings.json')['properties']['creatures']['properties']
        self.assertEqual(properties['newHorizonsCategories']['$ref'], 'newHorizonsCreatureCategories.json')
        self.assertEqual(load('config/gameConfig.json')['settings']['creatures']['newHorizonsCategories'], {})

    def test_complete_roster_is_explicit_not_level_inference(self):
        assignments = self.rules['creatures']
        self.assertEqual(len(assignments), 126)
        self.assertEqual(sum(value == 'core' for value in assignments.values()), 50)
        self.assertEqual(sum(value == 'elite' for value in assignments.values()), 58)
        self.assertEqual(sum(value == 'champion' for value in assignments.values()), 18)
        self.assertEqual(assignments['core:swordsman'], 'core')
        self.assertEqual(assignments['core:griffin'], 'elite')
        self.assertEqual(assignments['core:mage'], 'elite')
        self.assertEqual(assignments['core:genie'], 'elite')
        self.assertEqual(assignments['core:pikeman'], 'core')
        self.assertEqual(assignments['core:phoenix'], 'champion')
        self.assertEqual(set(self.rules['categories']), {'core', 'elite', 'champion'})

    def test_fortress_weekly_base_growth_is_authored_once_per_line_and_inherited_by_upgrades(self):
        expected = {
            'gnoll': 14,
            'lizardman': 9,
            'serpentFly': 8,
            'basilisk': 4,
            'gorgon': 3,
            'wyvern': 2,
            'hydra': 1,
        }
        lines = self.rules['growthLines']
        fortress = load('config/creatures/fortress.json')
        self.assertEqual(set(lines), {f'core:{name}' for name in expected})
        inherited_members = set()
        for base_name, weekly_growth in expected.items():
            with self.subTest(base_creature=base_name):
                base_key = f'core:{base_name}'
                line = lines[base_key]
                self.assertEqual(line['weeklyBaseGrowth'], weekly_growth)
                expected_members = [base_key] + [f'core:{name}' for name in fortress[base_name].get('upgrades', [])]
                self.assertEqual(line['members'], expected_members)
                self.assertTrue(set(line['members']) <= set(self.rules['creatures']))
                inherited_members.update(line['members'])
        self.assertEqual(len(inherited_members), 14)

    def test_texts_are_complete_and_separate(self):
        texts = load('config/newHorizonsCreatureCategoryTexts.json')
        ids = {value for definition in self.rules['categories'].values() for value in definition.values()}
        self.assertEqual(set(texts), ids)
        self.assertTrue(all(isinstance(text, str) and text for text in texts.values()))
        self.assertFalse(set(texts) & set(load('config/newHorizonsMasteryTexts.json')))

    def test_malformed_shapes_fail_without_restricting_entity_keys(self):
        changes = [
            lambda r: r.update(schemaVersion=2),
            lambda r: r.update(unreviewed=True),
            lambda r: r.update(creatures={}),
            lambda r: r['creatures'].update({'core:pixie': 1}),
            lambda r: r['creatures'].update({'core:pixie': 'boss'}),
            lambda r: r['categories'].pop('champion'),
            lambda r: r['categories']['core'].update(nameTextId=''),
            lambda r: r['categories']['elite'].update(numericalTier=4),
            lambda r: r['growthLines']['core:gnoll'].update(weeklyBaseGrowth=0),
            lambda r: r['growthLines']['core:gnoll'].update(members=[]),
            lambda r: r.update(rulesetVersion=1),
            lambda r: r.update(growthLines={})]
        for change in changes:
            rules = copy.deepcopy(self.rules)
            change(rules)
            with self.assertRaises(ValidationError):
                self.validator.validate(rules)
        rules = copy.deepcopy(self.rules)
        rules['creatures'] = {'core:notAnActualCreature': 'core'}
        self.validator.validate(rules)  # Runtime resolves canonical entity identity.

    def test_private_composition_and_write_guards(self):
        live = ROOT / 'Mods/new-horizons/mod.json'
        before = live.read_bytes()
        script = ROOT / 'tools/update-new-horizons-categories.py'
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            output = Path(temporary) / 'mod.json'
            command = [sys.executable, str(script), '--output', str(output)]
            subprocess.run(command, check=True, capture_output=True)
            subprocess.run(command + ['--check'], check=True, capture_output=True)
            preview = json.loads(output.read_text())
            baseline = json.loads(before)
            self.assertEqual(preview['version'], '0.14.1')
            self.assertEqual(preview['settings']['creatures'], {'newHorizonsCategories': self.rules})
            self.assertIn('private category diagnostic', preview['description'])
            for key in baseline['settings']:
                self.assertEqual(preview['settings'][key], baseline['settings'][key])
            self.assertEqual(preview['bonuses'], baseline['bonuses'])
            self.assertEqual(preview['filesystem'], baseline['filesystem'])
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            link = Path(temporary) / 'linked.json'
            link.symlink_to(output)
            self.assertNotEqual(subprocess.run([sys.executable, str(script), '--output', str(link), '--check'], capture_output=True).returncode, 0)
            output.write_text('{}')
            self.assertNotEqual(subprocess.run(command + ['--check'], capture_output=True).returncode, 0)
        with tempfile.TemporaryDirectory() as outside:
            for flags in ([], ['--check']):
                with self.subTest(outside_mode=flags):
                    missing = Path(outside) / ('outside-check.json' if flags else 'outside-new.json')
                    result = subprocess.run([sys.executable, str(script), '--output', str(missing), *flags], capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('preview output must remain under build/', result.stderr)
                    self.assertFalse(missing.exists())
        self.assertNotEqual(subprocess.run([sys.executable, str(script), '--output', str(live)], capture_output=True).returncode, 0)
        self.assertEqual(live.read_bytes(), before)
        self.assertEqual(json.loads(before)['version'], '0.14.0')
        self.assertEqual(json.loads(before)['settings']['creatures'],
                         {'newHorizonsCategories': self.rules})


if __name__ == '__main__':
    unittest.main()
