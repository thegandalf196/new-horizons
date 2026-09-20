#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate the New Horizons SecondarySkill entity composition.

The six school skills, Offense, and Sylvan Luck are active rank data.  The
remaining canonical skills are registered with their canonical rank text but
deliberately carry only a zero bonus until their runtime effect handlers exist.
Faction skills remain specially assigned/progressed rather than appearing in
the ordinary random gain table.
"""
import json
from pathlib import Path
import re
import unittest

from jsonschema import Draft4Validator, RefResolver

ROOT = Path(__file__).resolve().parents[2]
RANKS = ("basic", "advanced", "expert")
SCHOOL_SKILLS = {
    "lightMagic": "light",
    "natureMagic": "nature",
    "sorceryMagic": "sorcery",
    "havocMagic": "havoc",
    "shadowMagic": "shadow",
    "chaosMagic": "chaos",
}
ACTIVE_RANK_EFFECTS = {
    "offense": {
        "basic": {
            "meleeDamage": {
                "type": "PERCENTAGE_DAMAGE_BOOST",
                "subtype": "damageTypeMelee",
                "valueType": "BASE_NUMBER",
                "val": 10,
            }
        },
        "advanced": {
            "meleeDamage": {
                "type": "PERCENTAGE_DAMAGE_BOOST",
                "subtype": "damageTypeMelee",
                "valueType": "BASE_NUMBER",
                "val": 20,
            }
        },
        "expert": {
            "meleeDamage": {
                "type": "PERCENTAGE_DAMAGE_BOOST",
                "subtype": "damageTypeMelee",
                "valueType": "BASE_NUMBER",
                "val": 30,
            }
        },
    },
    "sylvanLuck": {
        "basic": {
            "luck": {"type": "LUCK", "valueType": "BASE_NUMBER", "val": 1},
            "luckyStrikeDamage": {
                "type": "LUCKY_STRIKE_DAMAGE_PERCENTAGE",
                "valueType": "BASE_NUMBER",
                "val": 25,
            },
        },
        "advanced": {
            "luck": {"type": "LUCK", "valueType": "BASE_NUMBER", "val": 2},
            "luckyStrikeDamage": {
                "type": "LUCKY_STRIKE_DAMAGE_PERCENTAGE",
                "valueType": "BASE_NUMBER",
                "val": 60,
            },
        },
        "expert": {
            "luck": {"type": "LUCK", "valueType": "BASE_NUMBER", "val": 3},
            "luckyStrikeDamage": {
                "type": "LUCKY_STRIKE_DAMAGE_PERCENTAGE",
                "valueType": "BASE_NUMBER",
                "val": 100,
            },
        },
    },
}
ACTIVE_GENERAL_GAIN_SKILLS = set(SCHOOL_SKILLS) | {"offense"}
NO_OP = {
    "newHorizonsPlaceholder": {
        "type": "MORALE",
        "valueType": "BASE_NUMBER",
        "val": 0,
    }
}
def parse_jsonc(text):
    string = r'"(?:\\.|[^"\\])*"'
    text = re.sub(string + r'|//[^\n]*|/\*[\s\S]*?\*/',
                  lambda match: match[0] if match[0].startswith('"') else '', text)
    text = re.sub(string + r'|,(?=\s*[}\]])',
                  lambda match: match[0] if match[0].startswith('"') else '', text)
    return json.loads(text)


def load(path):
    return parse_jsonc((ROOT / path).read_text(encoding="utf-8"))


class NewHorizonsSkillEntitiesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.perks = load("config/newHorizonsPerks.json")
        cls.skills = load("config/newHorizonsSkills.json")

    def test_exact_namespaced_canonical_coverage_without_core_aliases(self):
        canonical_ids = tuple(self.perks["skills"])
        runtime_ids = tuple(f"new-horizons:{key}" for key in self.skills)
        self.assertEqual(runtime_ids, canonical_ids)
        self.assertEqual(len(runtime_ids), 31)
        for key, skill in self.skills.items():
            with self.subTest(skill=key):
                self.assertNotIn(":", key)
                self.assertNotIn("compatibilityIdentifiers", skill)
                self.assertNotIn("core:", json.dumps(skill))
                self.assertEqual(skill["name"], self.perks["skills"][f"new-horizons:{key}"]["name"])

    def test_skill_objects_validate_against_vcmi_schema(self):
        schema = load("config/schemas/skill.json")
        bonus_schema = load("config/schemas/bonusInstance.json")
        base_uri = (ROOT / "config/schemas/skill.json").resolve().as_uri()
        bonus_uri = (ROOT / "config/schemas/bonusInstance.json").resolve().as_uri()
        resolver = RefResolver(base_uri, schema, store={bonus_uri: bonus_schema})
        validator = Draft4Validator(schema, resolver=resolver)
        for key, skill in self.skills.items():
            with self.subTest(skill=key):
                validator.validate(skill)

    def test_school_effects_remain_exactly_active(self):
        for key, school in SCHOOL_SKILLS.items():
            skill = self.skills[key]
            for value, rank in enumerate(RANKS, 1):
                with self.subTest(skill=key, rank=rank):
                    self.assertEqual(
                        skill[rank]["effects"],
                        {
                            "schoolMastery": {
                                "type": "MAGIC_SCHOOL_SKILL",
                                "subtype": f"new-horizons:{school}",
                                "valueType": "BASE_NUMBER",
                                "val": value,
                            }
                        },
                    )

    def test_gain_chances_expose_only_general_active_skills(self):
        for key, skill in self.skills.items():
            with self.subTest(skill=key):
                if key in ACTIVE_GENERAL_GAIN_SKILLS:
                    expected = {"might": 6, "magic": 2} if key == "offense" else {"might": 2, "magic": 6}
                    self.assertEqual(skill["gainChance"], expected)
                    self.assertNotIn("special", skill.get("tags", {}))
                else:
                    self.assertEqual(skill["gainChance"], {"might": 0, "magic": 0})
                    self.assertTrue(skill["tags"].get("special"))

    def test_non_school_skills_use_canonical_rank_text_and_declared_effects(self):
        school_keys = set(SCHOOL_SKILLS)
        for canonical_id, canonical in self.perks["skills"].items():
            key = canonical_id.removeprefix("new-horizons:")
            if key in school_keys:
                continue
            skill = self.skills[key]
            for rank in RANKS:
                with self.subTest(skill=key, rank=rank):
                    self.assertEqual(
                        skill[rank]["description"],
                        canonical["ranks"][rank]["description"],
                    )
                    if key in ACTIVE_RANK_EFFECTS:
                        self.assertEqual(
                            canonical["ranks"][rank]["effect"]["status"], "active"
                        )
                        self.assertEqual(skill[rank]["effects"], ACTIVE_RANK_EFFECTS[key][rank])
                    else:
                        self.assertEqual(
                            canonical["ranks"][rank]["effect"]["status"], "planned"
                        )
                        self.assertEqual(skill[rank]["effects"], NO_OP)
                    for image in skill[rank]["images"].values():
                        self.assertTrue((ROOT / "Mods/new-horizons/Images" / image).is_file())


if __name__ == "__main__":
    unittest.main()
