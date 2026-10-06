#!/usr/bin/env python3
"""Focused content guard for the Cabir Master repair creature ability."""

import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPELL_PATH = ROOT / "Mods/new-horizons/Content/config/spells/cabirRepair.json"
SCRIPT_CONFIG_PATH = ROOT / "Mods/new-horizons/Content/config/scripts/cabirRepair.json"
SCRIPT_PATH = ROOT / "Mods/new-horizons/Content/Scripts/spells/cabirRepair.lua"


class CabirRepairContentTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spell = json.loads(SPELL_PATH.read_text(encoding="utf-8"))["cabirRepair"]
        cls.script_config = json.loads(SCRIPT_CONFIG_PATH.read_text(encoding="utf-8"))["cabirRepair"]
        cls.script = SCRIPT_PATH.read_text(encoding="utf-8")

    def test_spell_is_creature_only_and_not_in_hero_acquisition_roster(self):
        self.assertEqual("ability", self.spell["type"])
        self.assertEqual("CREATURE", self.spell["targetType"])
        self.assertEqual({}, self.spell["school"])
        self.assertEqual({}, self.spell["gainChance"])
        self.assertEqual(0, self.spell["defaultGainChance"])
        base = self.spell["levels"]["base"]
        self.assertEqual({"smart": True}, base["targetModifier"])
        self.assertEqual(
            {"type": "new-horizons:cabirRepair"},
            base["battleEffects"]["repair"],
        )

    def test_module_script_registration_points_to_effect_implementation(self):
        self.assertEqual("spellEffect", self.script_config["implements"])
        self.assertEqual("spells/cabirRepair", self.script_config["script"])
        self.assertEqual([], self.script_config["patches"])
        self.assertFalse(self.script_config["schema"]["additionalProperties"])

    def test_only_the_four_requested_core_creature_ids_are_repairable(self):
        expected = {
            '["core:stoneGargoyle"]',
            '["core:obsidianGargoyle"]',
            '["core:ironGolem"]',
            '["core:stoneGolem"]',
        }
        actual = {
            line.strip().split(" =", 1)[0]
            for line in self.script.splitlines()
            if line.strip().startswith('["core:')
        }
        self.assertEqual(expected, actual)
        self.assertIn("not unit:isAlive()", self.script)
        self.assertIn("not mechanics:ownerMatches(unit)", self.script)
        self.assertIn("not hasSurvivingRepairCaster(mechanics)", self.script)
        self.assertIn("unit:getCount() <= 0", self.script)

    def test_heal_preview_and_server_effect_permanently_restore_only_eligible_remains(self):
        self.assertIn("ENUM.HealLevel.resurrect", self.script)
        self.assertIn("ENUM.HealPower.permanent", self.script)
        self.assertIn("unit:getUnusableRemains() * unit:getMaxHealth()", self.script)
        self.assertIn("unit:copy():heal(", self.script)
        self.assertIn("server:healUnit(", self.script)
        self.assertIn("result.hpDelta = result.hpDelta + healedHP", self.script)
        self.assertIn("result.unitsDelta = result.unitsDelta + restored", self.script)
        self.assertIn("mechanics:getEffectValue()", self.script)
        self.assertIn("mechanics:applySpellBonus", self.script)
        self.assertIn("caster:getPhantomInitialIntegrity() > 0", self.script)


if __name__ == "__main__":
    unittest.main()
