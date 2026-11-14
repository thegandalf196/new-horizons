#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Seth content contracts; native recruitment and rendered art are separate gates."""

import copy
import importlib.util
import json
from pathlib import Path
import re
import unittest

from jsonschema import Draft4Validator, RefResolver

ROOT = Path(__file__).resolve().parents[2]


def load(path):
    text = (ROOT / path).read_text(encoding="utf-8")
    quoted = r'"(?:\\.|[^"\\])*"'
    text = re.sub(quoted + r"|//[^\n]*|/\*[\s\S]*?\*/",
                  lambda m: m[0] if m[0].startswith('"') else "", text)
    text = re.sub(quoted + r"|,(?=\s*[}\]])",
                  lambda m: m[0] if m[0].startswith('"') else "", text)
    return json.loads(text)


class SethContentTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.creatures = load("Mods/new-horizons/Content/config/creatures/mummies.json")
        cls.hero = load("Mods/new-horizons/Content/config/heroes/seth.json")["core:nimbus"]

    def test_stable_hero_identity_does_not_replace_initialization(self):
        baseline = load("config/heroes/necropolis.json")["nimbus"]
        self.assertEqual(baseline["index"], 75)
        self.assertFalse(baseline["female"])
        self.assertEqual(baseline["class"], "necromancer")
        self.assertEqual(self.hero["texts"]["name"], "Seth")
        self.assertEqual(self.hero["texts"]["specialty"]["name"], "Ancient Oaths")
        self.assertLessEqual(set(self.hero), {"texts", "images"})
        self.assertNotIn("biography", self.hero["texts"])
        self.assertNotIn("initialArmy", self.hero)
        self.assertNotIn("skills", self.hero)
        self.assertNotIn("spellbook", self.hero)

    def test_biography_is_exact_authored_runtime_text(self):
        text = load("config/newHorizonsHeroBiographies/necropolis.json")["nimbus"]
        self.assertEqual(len(re.findall(r"[a-z0-9']+", text.casefold())), 72)
        self.assertTrue(text.startswith("Seth once ruled a kingdom"))
        self.assertTrue(text.endswith("even beyond the grave."))
        self.assertEqual(load("Mods/new-horizons/Content/config/heroes/biographies.json")
                         ["core:nimbus"]["texts"]["biography"], text)

    def test_upgrade_preserves_ordinary_loaded_mummy_combat_profile(self):
        base, royal = self.creatures["core:mummy"], self.creatures["royalMummy"]
        self.assertEqual(base["upgrades"], ["new-horizons:royalMummy"])
        # Original CRTRAITS Mummy profile, independently read during the audit.
        expected = {"hitPoints": 30, "attack": 7, "defense": 7,
                    "speed": 5, "damage": {"min": 3, "max": 5},
                    "fightValue": 270, "aiValue": 270}
        for key, value in expected.items():
            self.assertNotIn(key, base)
            self.assertEqual(royal[key], value, key)
        self.assertNotIn("initiative", royal)  # Preserve Mummy's native Speed fallback.
        self.assertEqual(royal["faction"], "core:neutral")
        self.assertTrue(royal["excludeFromRandomization"])
        self.assertTrue(royal["special"])
        self.assertNotIn("index", royal)

    def test_economics_and_growth_are_single_pipeline_inputs(self):
        self.assertEqual(self.creatures["core:mummy"]["cost"], {"gold": 200})
        self.assertEqual(self.creatures["royalMummy"]["cost"], {"gold": 230})
        for definition in self.creatures.values():
            self.assertEqual(definition["growth"], 7)
            self.assertNotIn("growthMultiplier", definition)
        # Neither source town production IDs nor its seven rows are rewritten.
        self.assertNotIn("factions", self.creatures)
        necropolis = load("config/factions/necropolis.json")["necropolis"]
        self.assertEqual(necropolis["town"]["creatures"][2], ["wight", "wraith"])

    def test_curse_and_shooting_have_no_extra_damage_or_penalty_exemptions(self):
        royal = self.creatures["royalMummy"]
        self.assertEqual(royal["shots"], 12)
        abilities = royal["abilities"]
        self.assertEqual(abilities["castCurse"], {
            "type": "SPELL_AFTER_ATTACK", "subtype": "core:curse", "val": 25})
        self.assertEqual({bonus["type"] for bonus in abilities.values()},
                         {"UNDEAD", "SPELL_AFTER_ATTACK", "SHOOTER", "NO_MELEE_PENALTY"})
        self.assertNotIn("abilities", self.creatures["core:mummy"])

    def test_category_and_leadership_preserve_existing_requirement(self):
        categories = load("config/newHorizonsCreatureCategories.json")["creatures"]
        for creature in ("core:mummy", "new-horizons:royalMummy"):
            self.assertEqual(categories[creature], "core")
        capabilities = load("config/newHorizonsCapabilities.json")["leadership"]
        self.assertEqual(capabilities["creatureRequirements"]["core:mummy"], 50)
        self.assertNotIn("new-horizons:royalMummy", capabilities["creatureRequirements"])
        self.assertEqual(capabilities["upgradeMultiplierPercent"], 100)

    def test_captured_policy_schema_is_optional_and_strict(self):
        rules = load("config/newHorizonsHeroes.json")
        schema = load("config/schemas/newHorizonsHeroes.json")
        validator = Draft4Validator(schema, resolver=RefResolver(
            base_uri=(ROOT / "config/schemas").as_uri() + "/", referrer=schema))
        validator.validate(rules)
        self.assertEqual(rules["additionalSpecialties"]["ancientOathsPolicyVersion"], 1)
        historical = copy.deepcopy(rules)
        del historical["additionalSpecialties"]["ancientOathsPolicyVersion"]
        validator.validate(historical)
        for invalid in (None, 0, 2, "1", True, 1.0, 1.5):
            changed = copy.deepcopy(rules)
            changed["additionalSpecialties"]["ancientOathsPolicyVersion"] = invalid
            self.assertFalse(validator.is_valid(changed), repr(invalid))

    def test_new_creature_and_selected_graphics_validate_against_schema(self):
        schema = load("config/schemas/creature.json")
        # Native JSON accepts comments/trailing commas, so resolve sanitized
        # schemas rather than jsonschema's strict remote JSON decoder.
        store = {path.as_uri(): load(str(path.relative_to(ROOT)))
                 for path in (ROOT / "config/schemas").glob("*.json")}
        resolver = RefResolver(base_uri=(ROOT / "config/schemas").as_uri() + "/",
                               referrer=schema, store=store)
        Draft4Validator(schema, resolver=resolver).validate(self.creatures["royalMummy"])

    def test_selected_map_footprints_and_three_direction_projectile_contract(self):
        base = self.creatures["core:mummy"]["graphics"]
        royal = self.creatures["royalMummy"]["graphics"]
        self.assertEqual(base["map"], "NH_MUMMY_MAP")
        self.assertEqual(royal["map"], "AVWmumy.DEF")
        for graphics in (base, royal):
            self.assertEqual(graphics["mapMask"], ["VV", "VA"])
            self.assertNotEqual(graphics["mapAttackFromLeft"], graphics["mapAttackFromRight"])
        self.assertEqual(base["mapAttackFromLeft"], "newHorizons/creatures/MummyMapAttackLeft")
        self.assertEqual(base["mapAttackFromRight"], "newHorizons/creatures/MummyMapAttackRight")
        stock = load("config/creatures/neutral.json")["mummy"]["graphics"]
        for direction in ("mapAttackFromLeft", "mapAttackFromRight"):
            self.assertEqual(royal[direction], stock[direction])
        self.assertEqual(royal["iconSmall"], "newHorizons/creatures/RoyalMummySmall:0:0")
        self.assertEqual(royal["iconLarge"], "newHorizons/creatures/RoyalMummyLarge:0:0")
        missile = royal["missile"]
        self.assertEqual(missile["frameAngles"], [45, 0, -45])
        self.assertEqual(missile["framesPerAngle"], 4)
        self.assertEqual(missile["attackClimaxFrame"], 5)
        self.assertEqual(missile["offset"], {
            "upperX": 31, "upperY": -108, "middleX": 58, "middleY": -42,
            "lowerX": 65, "lowerY": -7})
        self.assertEqual(missile["projectile"], "NH_ROYAL_MUMMY_PROJECTILE")
        self.assertEqual(missile["impact"], "NH_ROYAL_MUMMY_IMPACT")

    def test_generated_registration_preserves_override_order(self):
        module = load("Mods/new-horizons/mod.json")
        for field, path in (("heroes", "config/heroes/seth.json"),
                            ("creatures", "config/creatures/mummies.json")):
            self.assertEqual(module[field].count(path), 1)
        self.assertGreater(module["heroes"].index("config/heroes/seth.json"),
                           module["heroes"].index("config/heroes/halon.json"))
        spec = importlib.util.spec_from_file_location(
            "nh_seth_generator", ROOT / "tools/update-new-horizons-module.py")
        generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generator)
        self.assertEqual(generator.content_patch_files()[0], module["heroes"])
        self.assertEqual(generator.content_patch_files()[2], module["creatures"])


if __name__ == "__main__":
    unittest.main()
