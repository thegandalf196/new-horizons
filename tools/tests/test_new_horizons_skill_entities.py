#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate the New Horizons SecondarySkill entity composition.

The six school skills are active data.  The remaining canonical skills are
registered with their canonical rank text but deliberately carry only a zero
bonus until their runtime effect handlers exist.
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

    def test_gain_chances_fail_closed_until_specialized_availability_exists(self):
        for key, skill in self.skills.items():
            with self.subTest(skill=key):
                if key in SCHOOL_SKILLS:
                    self.assertEqual(skill["gainChance"], {"might": 2, "magic": 6})
                    self.assertNotIn("special", skill["tags"])
                else:
                    self.assertEqual(skill["gainChance"], {"might": 0, "magic": 0})
                    self.assertTrue(skill["tags"].get("special"))

    def test_non_school_skills_use_canonical_rank_text_and_inert_effects(self):
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
                    self.assertEqual(skill[rank]["effects"], NO_OP)
                    for image in skill[rank]["images"].values():
                        self.assertTrue((ROOT / "Mods/new-horizons/Images" / image).is_file())


if __name__ == "__main__":
    unittest.main()
