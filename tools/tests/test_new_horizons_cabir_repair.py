#!/usr/bin/env python3
"""Focused content guard for the Cabir Master repair creature ability."""

import json
from pathlib import Path
import unittest

from jsonschema import Draft4Validator, ValidationError


ROOT = Path(__file__).resolve().parents[2]
SPELL_PATH = ROOT / "Mods/new-horizons/Content/config/spells/cabirRepair.json"
SCRIPT_CONFIG_PATH = ROOT / "Mods/new-horizons/Content/config/scripts/cabirRepair.json"
EFFECT_PATH = ROOT / "lib/spells/effects/native/CreatureUtilities.cpp"


class CabirRepairContentTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spell = json.loads(SPELL_PATH.read_text(encoding="utf-8"))["cabirRepair"]
        cls.script_config = json.loads(SCRIPT_CONFIG_PATH.read_text(encoding="utf-8"))["cabirRepair"]
        source = EFFECT_PATH.read_text(encoding="utf-8")
        cls.effect = source.split("class CabirRepair final", 1)[1].split("\nclass ", 1)[0]

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

    def test_effect_parameters_allow_discriminator_but_reject_unknown_fields(self):
        schema = self.script_config["schema"]
        effect_parameters = self.spell["levels"]["base"]["battleEffects"]["repair"]
        validator = Draft4Validator(schema)

        Draft4Validator.check_schema(schema)
        validator.validate(effect_parameters)

        with self.assertRaises(ValidationError):
            validator.validate({**effect_parameters, "unexpectedRepairOption": True})

    def test_only_the_four_requested_core_creature_ids_are_repairable(self):
        for creature in ("core:stoneGargoyle", "core:obsidianGargoyle", "core:ironGolem", "core:stoneGolem"):
            self.assertIn('key != "' + creature + '"', self.effect)
        self.assertEqual(4, self.effect.count('key != "core:'))
        self.assertIn("!unit->alive()", self.effect)
        self.assertIn("!mechanics->ownerMatches(unit)", self.effect)
        self.assertIn("!survivingCaster(mechanics)", self.effect)
        self.assertIn("unit->getCount() <= 0", self.effect)

    def test_heal_preview_and_server_effect_permanently_restore_only_eligible_remains(self):
        self.assertIn("EHealLevel::RESURRECT", self.effect)
        self.assertIn("EHealPower::PERMANENT", self.effect)
        self.assertIn("unit->getUnusableRemains()) * unit->getMaxHealth()", self.effect)
        self.assertIn("unit->acquireState()", self.effect)
        self.assertIn("mechanics->getEffectValue()", self.effect)
        self.assertIn("mechanics->applySpellBonus", self.effect)
        self.assertIn("caster->getPhantomInitialIntegrity() > 0", self.effect)

if __name__ == "__main__":
    unittest.main()
