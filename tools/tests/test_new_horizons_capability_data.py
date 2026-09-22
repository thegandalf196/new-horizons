"""Unactivated capability data checks, not native or GUI acceptance."""
import json
from pathlib import Path
import unittest
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]

CANONICAL_CLASS_PROFILES = {
    'core:knight': {'base': 1025, 'perLevel': 55},
    'core:cleric': {'base': 725, 'perLevel': 35},
    'core:ranger': {'base': 950, 'perLevel': 50},
    'core:druid': {'base': 650, 'perLevel': 30},
    'core:alchemist': {'base': 875, 'perLevel': 45},
    'core:wizard': {'base': 650, 'perLevel': 30},
    'core:demoniac': {'base': 1025, 'perLevel': 55},
    'core:heretic': {'base': 725, 'perLevel': 35},
    'core:deathknight': {'base': 950, 'perLevel': 50},
    'core:necromancer': {'base': 725, 'perLevel': 35},
    'core:overlord': {'base': 1025, 'perLevel': 55},
    'core:warlock': {'base': 725, 'perLevel': 35},
    'core:barbarian': {'base': 1100, 'perLevel': 60},
    'core:battlemage': {'base': 875, 'perLevel': 45},
    'core:beastmaster': {'base': 1100, 'perLevel': 60},
    'core:witch': {'base': 725, 'perLevel': 35},
    'core:planeswalker': {'base': 875, 'perLevel': 45},
    'core:elementalist': {'base': 650, 'perLevel': 30},
}

CANONICAL_LEADERSHIP_REQUIREMENTS = {
    'core:pikeman': 60,
    'core:archer': 90,
    'core:swordsman': 150,
    'core:griffin': 220,
    'core:monk': 320,
    'core:cavalier': 450,
    'core:angel': 650,
    'core:centaur': 60,
    'core:dwarf': 90,
    'core:woodElf': 140,
    'core:pegasus': 210,
    'core:dendroidGuard': 280,
    'core:unicorn': 420,
    'core:greenDragon': 650,
    'core:gremlin': 50,
    'core:stoneGargoyle': 80,
    'core:ironGolem': 140,
    'core:mage': 300,
    'core:genie': 240,
    'core:naga': 450,
    'core:giant': 650,
    'core:imp': 50,
    'core:gog': 90,
    'core:hellHound': 150,
    'core:demon': 220,
    'core:pitFiend': 320,
    'core:efreet': 430,
    'core:devil': 650,
    'core:skeleton': 45,
    'core:walkingDead': 80,
    'core:wight': 130,
    'core:vampire': 230,
    'core:lich': 330,
    'core:blackKnight': 470,
    'core:boneDragon': 650,
    'core:troglodyte': 55,
    'core:harpy': 90,
    'core:beholder': 140,
    'core:medusa': 230,
    'core:minotaur': 330,
    'core:manticore': 430,
    'core:redDragon': 650,
    'core:goblin': 50,
    'core:goblinWolfRider': 90,
    'core:orc': 140,
    'core:ogre': 250,
    'core:roc': 300,
    'core:cyclop': 450,
    'core:behemoth': 650,
    'core:gnoll': 55,
    'core:lizardman': 90,
    'core:serpentFly': 140,
    'core:basilisk': 230,
    'core:gorgon': 340,
    'core:wyvern': 430,
    'core:hydra': 650,
    'core:pixie': 45,
    'core:sprite': 60,
    'core:airElemental': 180,
    'core:waterElemental': 200,
    'core:fireElemental': 220,
    'core:earthElemental': 240,
    'core:magicElemental': 320,
    'core:phoenix': 650,
}


class CapabilityDataTest(unittest.TestCase):
    def setUp(self):
        self.rules = json.loads((ROOT / 'config/newHorizonsCapabilities.json').read_text())

    def test_exact_canonical_class_profiles(self):
        self.assertEqual(self.rules['schemaVersion'], 1)
        self.assertEqual(self.rules['rulesetVersion'], 3)
        self.assertEqual(self.rules['classProfiles'], CANONICAL_CLASS_PROFILES)

    def test_canonical_per_slot_leadership_requirements(self):
        leadership = self.rules['leadership']
        self.assertEqual(set(leadership), {
            'globalScalePercent', 'upgradeMultiplierPercent', 'upgradeRounding',
            'creatureRequirements',
        })
        self.assertEqual(leadership['globalScalePercent'], 100)
        self.assertEqual(leadership['creatureRequirements'], CANONICAL_LEADERSHIP_REQUIREMENTS)

    def test_leadership_upgrade_multiplier_rounds_to_nearest_ten(self):
        leadership = self.rules['leadership']
        self.assertEqual(leadership['upgradeMultiplierPercent'], 120)
        self.assertEqual(leadership['upgradeRounding'], 10)

        def upgraded_requirement(base):
            multiplier = leadership['upgradeMultiplierPercent']
            rounding = leadership['upgradeRounding']
            return ((base * multiplier + 50 * rounding) // (100 * rounding)) * rounding

        for base, expected in ((45, 50), (55, 70), (60, 70), (90, 110),
                               (140, 170), (150, 180), (220, 260),
                               (320, 380), (430, 520), (470, 560),
                               (650, 780)):
            with self.subTest(base=base):
                self.assertEqual(upgraded_requirement(base), expected)

    def test_siege_rank_table_remains_canonical(self):
        self.assertEqual(self.rules['siege'], {
            'ballistaDamageMultiplier': [1, 2, 3, 4],
            'siegeRating': [0, 20, 40, 60],
            'outputs': {
                'ballistaDamage': {'base': 50, 'siegeCoefficientHalf': 4},
                'catapultStructuralDamage': {'base': 100, 'siegeCoefficientHalf': 6},
                'firstAidHealing': {'base': 75, 'siegeCoefficientHalf': 6},
                'defensiveTowerDamage': {'base': 60, 'siegeCoefficientHalf': 3},
            },
            'directControlChance': [0, 100, 100, 100],
        })

    def test_private_capability_generation_preserves_live_module(self):
        live = ROOT / 'Mods/new-horizons/mod.json'
        before = live.read_bytes()
        script = ROOT / 'tools/update-new-horizons-module.py'
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            output = Path(temporary) / 'mod.json'
            command = [sys.executable, str(script), '--capability-preview-output', str(output)]
            subprocess.run(command, check=True, capture_output=True)
            metadata = json.loads(output.read_text())
            self.assertEqual(metadata['version'], '0.4.0')
            self.assertEqual(metadata['settings']['heroes']['newHorizonsCapabilities'], self.rules)
            subprocess.run(command + ['--check'], check=True, capture_output=True)
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
        self.assertEqual(live.read_bytes(), before)
        self.assertNotEqual(subprocess.run([sys.executable, str(script), '--capability-preview-output', str(live)], capture_output=True).returncode, 0)

    def test_capability_only_control_is_separate_and_non_overwriting(self):
        script = ROOT / 'tools/update-new-horizons-module.py'
        live = ROOT / 'Mods/new-horizons/mod.json'
        before = live.read_bytes()
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            output = Path(temporary) / 'control.json'
            command = [sys.executable, str(script), '--capability-only-control-output', str(output)]
            subprocess.run(command, check=True, capture_output=True)
            metadata = json.loads(output.read_text())
            self.assertEqual(metadata['settings']['heroes']['newHorizons'], {})
            self.assertEqual(metadata['settings']['heroes']['newHorizonsCapabilities'], self.rules)
            self.assertIn('diagnostic', metadata['name'])
            subprocess.run(command + ['--check'], check=True, capture_output=True)
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertNotEqual(subprocess.run(command + ['--capability-preview-output', str(output)], capture_output=True).returncode, 0)
        self.assertEqual(live.read_bytes(), before)
        self.assertNotEqual(subprocess.run([sys.executable, str(script), '--capability-only-control-output', str(live)], capture_output=True).returncode, 0)

    def test_schema_registered_and_default_module_matches_capabilities(self):
        schema = json.loads((ROOT / 'config/schemas/gameSettings.json').read_text())
        self.assertEqual(schema['properties']['heroes']['properties']['newHorizonsCapabilities']['$ref'], 'newHorizonsCapabilities.json')
        module = json.loads((ROOT / 'Mods/new-horizons/mod.json').read_text())
        self.assertEqual(module['version'], '0.8.0')
        self.assertEqual(module['settings']['heroes']['newHorizonsCapabilities'], self.rules)


if __name__ == '__main__':
    unittest.main()
