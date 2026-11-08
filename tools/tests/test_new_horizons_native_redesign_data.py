# SPDX-License-Identifier: GPL-2.0-or-later
"""Current captured metadata and content contracts; not runtime acceptance."""

import copy
import json
import unittest
from pathlib import Path

from jsonschema import Draft4Validator
from referencing import Registry, Resource


ROOT = Path(__file__).resolve().parents[2]


def load(path):
    return json.loads((ROOT / path).read_text(encoding="utf-8"))


class NativeRedesignDataTest(unittest.TestCase):
    def setUp(self):
        self.perks = load("config/newHorizonsPerks.json")
        self.magic = load("config/newHorizonsMagic.json")
        self.orders = load("config/newHorizonsCombat.json")["combat"]["heroCommands"]
        self.skill_entities = load("config/newHorizonsSkills.json")
        schemas = {name: load(f"config/schemas/{name}.json") for name in
                   ("newHorizonsMagic", "newHorizonsMagicV2", "newHorizonsMagicV3")}
        registry = Registry().with_resources((f"vcmi:{key}", Resource.from_contents(value))
                                             for key, value in schemas.items())
        self.magic_validator = Draft4Validator(schemas["newHorizonsMagicV3"], registry=registry)

    def test_new_default_profiles_and_module_are_identical(self):
        self.assertEqual(self.perks["commandPolicyVersion"], 1)
        self.assertEqual(self.perks["elementalAttunementPolicy"], {"version": 1})
        self.assertEqual(self.orders["commandResourcePolicyVersion"], 1)
        self.magic_validator.validate(self.magic)
        Draft4Validator(load("config/schemas/newHorizonsPerks.json")).validate(self.perks)
        Draft4Validator(load("config/schemas/newHorizonsCombatV4.json")).validate(self.orders)
        module = load("Mods/new-horizons/mod.json")
        self.assertEqual(module["settings"]["magic"]["newHorizons"], self.magic)
        self.assertEqual(module["settings"]["heroes"]["newHorizonsPerks"], self.perks)
        self.assertEqual(module["settings"]["combat"]["heroCommands"], self.orders)
        self.assertEqual(module["skills"], self.skill_entities)

    def test_active_profiles_cannot_omit_or_spoof_finite_command_markers(self):
        for rules, schema, key in (
                (self.perks, "newHorizonsPerks", "commandPolicyVersion"),
                (self.perks, "newHorizonsPerks", "elementalAttunementPolicy"),
                (self.orders, "newHorizonsCombatV4", "commandResourcePolicyVersion")):
            validator = Draft4Validator(load(f"config/schemas/{schema}.json"))
            missing = copy.deepcopy(rules)
            missing.pop(key)
            self.assertFalse(validator.is_valid(missing))
            for value in (None, 0, 2, True, "1", {}, []):
                changed = copy.deepcopy(rules)
                changed[key] = value
                self.assertFalse(validator.is_valid(changed), (key, value))

    def test_fiona_authoring_selects_speed_and_removes_old_damage_coefficient(self):
        heroes = load("config/newHorizonsHeroes.json")
        ordinary = heroes["additionalSpecialties"]["ordinaryCreatureSpecialties"]
        self.assertNotIn("packPursuitSpeed", ordinary)
        prepared = heroes["additionalSpecialties"]["preparedCreatureSpecialties"]["heroes"]["core:fiona"]["mechanics"]
        self.assertEqual(prepared["operations"][0]["value"], 5)
        self.assertEqual(prepared["operations"][0]["type"], "STACKS_SPEED")
        self.assertNotIn("packPursuitDamagePercent", ordinary)
        module = load("Mods/new-horizons/mod.json")
        self.assertEqual(module["settings"]["heroes"]["newHorizons"], heroes)
        registry = (ROOT / "lib/entities/hero/OrdinaryCreatureSpecialtyRegistry.cpp").read_text()
        start = registry.index('{"core:fiona",')
        row = registry[start:registry.index('},', start)]
        self.assertIn('"packPursuitSpeed", BonusType::STACKS_SPEED', row)
        self.assertIn('BonusValueType::ADDITIVE_VALUE', row)
        self.assertNotIn("primary melee damage", row)
        self.assertIn("FionaPackPursuit", row)

    def test_event_specialties_have_explicit_coefficients_not_retired_meanings(self):
        heroes = load("config/newHorizonsHeroes.json")
        ordinary = heroes["additionalSpecialties"]["ordinaryCreatureSpecialties"]
        expected = {"undertowDefensePerAttack": 1, "goblinFrenzyDamagePercent": 50,
                    "evasiveFlightReductionPercentPerHex": 3, "risingTideCreaturesPerRound": 1,
                    "shieldAndSwordPhysicalReductionPercent": 40, "undyingHealingHP": 100}
        self.assertEqual({key: ordinary[key] for key in expected}, expected)
        retired = ("undertowRetaliationPercent", "mobPressureDamagePercent",
                   "evasiveFlightReductionPercent", "tidalRenewalHealingHP",
                   "sepulchralGripSpeedReduction")
        validator = Draft4Validator(load("config/schemas/newHorizonsHeroes.json"))
        validator.validate(heroes)
        for key in retired:
            self.assertNotIn(key, ordinary)
            changed = copy.deepcopy(heroes)
            changed["additionalSpecialties"]["ordinaryCreatureSpecialties"][key] = 1
            self.assertFalse(validator.is_valid(changed), key)

    def test_arrow_coefficient_and_ordinary_price_are_explicit(self):
        arrow = self.magic["spells"]["core:magicArrow"]
        self.assertEqual(arrow["magicArrow"], {"version": 1})
        self.assertEqual(arrow["directDamage"], {"base": 20, "powerCoefficient": 10})
        self.assertEqual(arrow["costs"], [4, 4, 4, 4])
        self.assertNotIn("elementalTag", arrow)
        overcharger = next(row for row in self.perks["skills"]["new-horizons:sorceryMagic"]["perks"]
                           if row["id"].endswith(".overcharger"))
        self.assertEqual(overcharger["description"],
                         "Magic Arrow gains one additional maximum Overcharge level.")

    def test_command_stable_ids_select_passive_replacements(self):
        command = self.perks["skills"]["new-horizons:command"]
        self.assertEqual(command["availability"], "Might-exclusive")
        expected = {"battlePlan": "Battle Plan", "crisisCommand": "Unbroken Ranks",
                    "doubleCommand": "Master Commander", "seizeInitiative": "Grand Marshal"}
        for row in command["perks"]:
            suffix = row["id"].split(".")[-1]
            if suffix in expected:
                self.assertEqual(row["name"], expected[suffix])
            self.assertNotIn("immediately issue", row["description"])
        for rank, extra in zip(("basic", "advanced", "expert"), (1, 2, 3)):
            text = command["ranks"][rank]["description"]
            self.assertIn(f"maximum Command by {extra}", text)
            self.assertEqual(self.skill_entities["command"][rank]["description"], text)

    def test_attunement_ten_perks_preserve_tier_counts_and_ids(self):
        skill = self.perks["skills"]["new-horizons:elementalRebirth"]
        self.assertEqual(skill["name"], "Elemental Attunement")
        self.assertEqual([row["name"] for row in skill["perks"]], [
            "Magma Weapons", "Storm Bolts", "Elemental Conduit", "Water Veil",
            "Greater Essence", "Stonebound Magic", "Primal Vigilance", "Elemental Cataclysm",
            "Primordial Host", "Perfect Convergence"])
        self.assertEqual([row["requires"] for row in skill["perks"]],
                         ["basic"] * 4 + ["advanced"] * 4 + ["expert"] * 2)
        for rank, bonus in zip(("basic", "advanced", "expert"), (20, 30, 40)):
            self.assertIn(f"+{bonus}% attack damage", skill["ranks"][rank]["description"])
            self.assertEqual(self.skill_entities["elementalRebirth"][rank]["description"],
                             skill["ranks"][rank]["description"])

    def test_explicit_elements_include_beneficial_spells_and_exclude_generic_magic(self):
        rows = self.magic["spells"]
        for identifier, element in {"core:fireball": "fire", "core:fireShield": "fire",
                                    "core:iceBolt": "water", "core:protectWater": "water",
                                    "core:chainLightning": "air", "core:airShield": "air",
                                    "core:earthquake": "earth", "core:stoneSkin": "earth"}.items():
            self.assertEqual(rows[identifier]["elementalTag"], element)
        for identifier in ("core:magicArrow", "core:armageddon", "core:implosion",
                           "new-horizons:holyWrath", "new-horizons:soulReaper",
                           "new-horizons:heavenlyGale"):
            self.assertNotIn("elementalTag", rows[identifier])
        for identifier in ("core:fireball", "core:frostRing", "core:inferno", "core:meteorShower"):
            self.assertIs(rows[identifier]["elementalAreaDamage"], True)
        for identifier in ("core:chainLightning", "new-horizons:masterChainLightning", "core:landMine"):
            self.assertNotIn("elementalAreaDamage", rows[identifier])

    def test_metadata_schema_rejects_invalid_tags_and_area_without_element(self):
        for value in (None, 1, "havoc", "Fire", "", {}, []):
            candidate = copy.deepcopy(self.magic)
            candidate["spells"]["core:fireball"]["elementalTag"] = value
            self.assertFalse(self.magic_validator.is_valid(candidate), repr(value))
        candidate = copy.deepcopy(self.magic)
        candidate["spells"]["core:magicArrow"]["elementalAreaDamage"] = True
        self.assertFalse(self.magic_validator.is_valid(candidate))
        for value in (None, 0, 2, True, "1"):
            candidate = copy.deepcopy(self.magic)
            candidate["spells"]["core:magicArrow"]["magicArrow"]["version"] = value
            self.assertFalse(self.magic_validator.is_valid(candidate), repr(value))


if __name__ == "__main__":
    unittest.main()
