#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate the New Horizons SecondarySkill entity composition.

The six school skills, Offense, Estates, Learning, Sylvan Luck, Necromancy,
and Metamagic are active rank data.  The
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
    "estates": {
        "basic": {
            "dailyGold": {
                "type": "GENERATE_RESOURCE", "subtype": "gold",
                "valueType": "BASE_NUMBER", "val": 125,
            },
        },
        "advanced": {
            "dailyGold": {
                "type": "GENERATE_RESOURCE", "subtype": "gold",
                "valueType": "BASE_NUMBER", "val": 250,
            },
        },
        "expert": {
            "dailyGold": {
                "type": "GENERATE_RESOURCE", "subtype": "gold",
                "valueType": "BASE_NUMBER", "val": 500,
            },
        },
    },
    "learning": {
        "basic": {
            "experience": {
                "type": "HERO_EXPERIENCE_GAIN_PERCENT",
                "valueType": "PERCENT_TO_BASE", "val": 10,
            },
        },
        "advanced": {
            "experience": {
                "type": "HERO_EXPERIENCE_GAIN_PERCENT",
                "valueType": "PERCENT_TO_BASE", "val": 20,
            },
        },
        "expert": {
            "experience": {
                "type": "HERO_EXPERIENCE_GAIN_PERCENT",
                "valueType": "PERCENT_TO_BASE", "val": 30,
            },
        },
    },
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
    "necromancy": {
        "basic": {
            "main": {
                "type": "UNDEAD_RAISE_PERCENTAGE",
                "valueType": "BASE_NUMBER",
                "val": 10,
            },
        },
        "advanced": {
            "main": {
                "type": "UNDEAD_RAISE_PERCENTAGE",
                "valueType": "BASE_NUMBER",
                "val": 20,
            },
        },
        "expert": {
            "main": {
                "type": "UNDEAD_RAISE_PERCENTAGE",
                "valueType": "BASE_NUMBER",
                "val": 30,
            },
        },
    },
    "metamagic": {
        "basic": {
            "metamagicUses": {
                "type": "METAMAGIC_USES_PER_COMBAT",
                "valueType": "BASE_NUMBER",
                "val": 1,
            },
        },
        "advanced": {
            "metamagicUses": {
                "type": "METAMAGIC_USES_PER_COMBAT",
                "valueType": "BASE_NUMBER",
                "val": 2,
            },
        },
        "expert": {
            "metamagicUses": {
                "type": "METAMAGIC_USES_PER_COMBAT",
                "valueType": "BASE_NUMBER",
                "val": 3,
            },
        },
    },
}

# These ranks are implemented by authoritative engine state rather than a
# generic bonus entity, so their JSON skill effect deliberately remains the
# zero-valued placeholder while the canonical registry marks them active.
RUNTIME_ACTIVE_RANKS = {
    "armorer",
    "archery",
    "battlecraft",
    "command",
    "warMachines",
    "discipline",
    "logistics",
    "shroudOfMalassa",
    "bloodrage",
    "bulwarkOfTheMire",
}
ACTIVE_GENERAL_GAIN_SKILLS = set(SCHOOL_SKILLS) | {"offense"}
NO_OP = {
    "newHorizonsPlaceholder": {
        "type": "MORALE",
        "valueType": "BASE_NUMBER",
        "val": 0,
    }
}

CORE_SKILL_IMAGE = re.compile(r"^(SECSK32|SECSKILL|SECSK82):0:[0-9]+$")


def image_is_module_file_or_core_skill(image):
    """Accept explicit core DEF frame references alongside module PNGs."""
    if CORE_SKILL_IMAGE.fullmatch(image):
        return True
    return (ROOT / "Mods/new-horizons/Images" / image).is_file()


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

    def test_metamagic_rank_uses_are_bound_to_metamagic_skill(self):
        """Guard the rank bonus against being copied onto another skill."""
        expected = {"basic": 1, "advanced": 2, "expert": 3}
        for rank, uses in expected.items():
            with self.subTest(rank=rank):
                metamagic_effect = self.skills["metamagic"][rank]["effects"]["metamagicUses"]
                self.assertEqual(metamagic_effect["type"], "METAMAGIC_USES_PER_COMBAT")
                self.assertEqual(metamagic_effect["valueType"], "BASE_NUMBER")
                self.assertEqual(metamagic_effect["val"], uses)
                self.assertNotIn(
                    "METAMAGIC_USES_PER_COMBAT",
                    json.dumps(self.skills["armorer"][rank]["effects"]),
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
                    elif key in RUNTIME_ACTIVE_RANKS:
                        self.assertEqual(
                            canonical["ranks"][rank]["effect"]["status"], "active"
                        )
                        self.assertTrue(skill[rank]["effects"])
                    else:
                        self.assertEqual(
                            canonical["ranks"][rank]["effect"]["status"], "planned"
                        )
                        self.assertEqual(skill[rank]["effects"], NO_OP)
                    for image in skill[rank]["images"].values():
                        self.assertTrue(image_is_module_file_or_core_skill(image))
            if key == "necromancy":
                # The New Horizons resolver always raises canonical Skeletons
                # and optional Zombies.  The legacy bonus would select the
                # strongest eligible creature/upgrades and must not leak into
                # this faction skill's active entity.
                self.assertNotIn("IMPROVED_NECROMANCY", json.dumps(skill))


if __name__ == "__main__":
    unittest.main()
