#!/usr/bin/env python3
"""Validate the canonical New Horizons Skill/perk registry shape only.

This is deliberately a data-contract test.  It does not claim that a planned
effect is implemented by the engine, AI, save format, or user interface.
"""
import hashlib
import json
from pathlib import Path
import unittest

from jsonschema import Draft4Validator

ROOT = Path(__file__).resolve().parents[2]
RANKS = ("basic", "advanced", "expert")
ACTIVE_PERKS = {
    "new-horizons:sorceryMagic.overcharger",
    "new-horizons:sorceryMagic.selectiveDispel",
    "new-horizons:sorceryMagic.temporalField",
    "new-horizons:sorceryMagic.temporalist",
    "new-horizons:sorceryMagic.teleporter",
}
ACTIVE_RANK_SKILLS = {
    "new-horizons:offense",
}
EXPECTED_SKILLS = (
    "new-horizons:offense",
    "new-horizons:armorer",
    "new-horizons:archery",
    "new-horizons:battlecraft",
    "new-horizons:warMachines",
    "new-horizons:discipline",
    "new-horizons:recruitment",
    "new-horizons:command",
    "new-horizons:lightMagic",
    "new-horizons:shadowMagic",
    "new-horizons:natureMagic",
    "new-horizons:havocMagic",
    "new-horizons:sorceryMagic",
    "new-horizons:chaosMagic",
    "new-horizons:spellcraft",
    "new-horizons:wisdom",
    "new-horizons:warcasting",
    "new-horizons:logistics",
    "new-horizons:diplomacy",
    "new-horizons:estates",
    "new-horizons:learning",
    "new-horizons:luck",
    "new-horizons:divineMandate",
    "new-horizons:sylvanLuck",
    "new-horizons:metamagic",
    "new-horizons:shroudOfMalassa",
    "new-horizons:demonicGating",
    "new-horizons:necromancy",
    "new-horizons:bloodrage",
    "new-horizons:bulwarkOfTheMire",
    "new-horizons:elementalRebirth",
)


def load(path):
    return json.loads((ROOT / path).read_text(encoding="utf-8"))


class NewHorizonsPerkDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rules = load("config/newHorizonsPerks.json")
        cls.schema = load("config/schemas/newHorizonsPerks.json")

    def test_registry_validates_against_schema(self):
        Draft4Validator(self.schema).validate(self.rules)

    def test_source_identity_and_selection_limits(self):
        self.assertEqual(self.rules["schemaVersion"], 1)
        self.assertEqual(self.rules["rulesetVersion"], 1)
        self.assertEqual(self.rules["sourceDocument"], "docs/design-sources/New Horizons.docx")
        source = ROOT / self.rules["sourceDocument"]
        self.assertTrue(source.is_file())
        self.assertEqual(
            hashlib.sha256(source.read_bytes()).hexdigest(), self.rules["sourceSha256"]
        )
        self.assertEqual(self.rules["maxSkillChoices"], 2)
        self.assertEqual(self.rules["maxPerkChoices"], 2)
        self.assertEqual(self.rules["maxPerksPerSkill"], 3)

    def test_canonical_31_skill_roster(self):
        self.assertEqual(tuple(self.rules["skills"]), EXPECTED_SKILLS)
        self.assertEqual(len(self.rules["skills"]), 31)

    def test_each_skill_has_ranked_effects_and_4_4_2_perks(self):
        all_perk_ids = []
        for skill_id, skill in self.rules["skills"].items():
            with self.subTest(skill=skill_id):
                self.assertEqual(skill["id"], skill_id)
                self.assertEqual(set(skill["ranks"]), set(RANKS))
                for rank in RANKS:
                    effect = skill["ranks"][rank]["effect"]
                    expected_status = "active" if skill_id in ACTIVE_RANK_SKILLS else "planned"
                    self.assertEqual(effect["status"], expected_status)
                    self.assertTrue(effect["description"])
                    self.assertEqual(skill["ranks"][rank]["description"], effect["description"])

                perks = skill["perks"]
                self.assertEqual(len(perks), 10)
                self.assertEqual(
                    {rank: sum(perk["requires"] == rank for perk in perks) for rank in RANKS},
                    {"basic": 4, "advanced": 4, "expert": 2},
                )
                for perk in perks:
                    self.assertTrue(perk["id"].startswith(skill_id + "."))
                    self.assertEqual(perk["description"], perk["effect"]["description"])
                    expected_status = "active" if perk["id"] in ACTIVE_PERKS else "planned"
                    self.assertEqual(perk["effect"]["status"], expected_status)
                    self.assertTrue(perk["name"])
                    self.assertTrue(perk["description"])
                    all_perk_ids.append(perk["id"])

        self.assertEqual(len(all_perk_ids), 310)
        self.assertEqual(len(set(all_perk_ids)), 310)

    def test_game_settings_schema_exposes_registry_without_activating_it(self):
        settings = load("config/schemas/gameSettings.json")
        heroes = settings["properties"]["heroes"]["properties"]
        self.assertEqual(heroes["newHorizonsPerks"], {"$ref": "newHorizonsPerks.json"})

    def test_default_module_carries_canonical_planned_registry(self):
        module = load("Mods/new-horizons/mod.json")
        self.assertEqual(module["version"], "0.7.0")
        self.assertEqual(module["settings"]["heroes"]["newHorizonsPerks"], self.rules)


if __name__ == "__main__":
    unittest.main()
