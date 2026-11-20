#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate the New Horizons SecondarySkill entity composition.

The six school skills, Offense, Estates, Learning, Luck, Sylvan Tactics, Necromancy,
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
if __package__:
    from .nhart_test_resources import ArtPath
else:
    from nhart_test_resources import ArtPath

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
    "luck": {
        "basic": {
            "luck": {"type": "LUCK", "valueType": "BASE_NUMBER", "val": 1},
        },
        "advanced": {
            "luck": {"type": "LUCK", "valueType": "BASE_NUMBER", "val": 2},
        },
        "expert": {
            "luck": {"type": "LUCK", "valueType": "BASE_NUMBER", "val": 3},
        },
    },
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
        "basic": {"initiative": {"type": "STACKS_INITIATIVE_FLAT", "valueType": "BASE_NUMBER", "val": 1}},
        "advanced": {"initiative": {"type": "STACKS_INITIATIVE_FLAT", "valueType": "BASE_NUMBER", "val": 2}},
        "expert": {"initiative": {"type": "STACKS_INITIATIVE_FLAT", "valueType": "BASE_NUMBER", "val": 3}},
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
    "metamagic": {"basic": {}, "advanced": {}, "expert": {}},
}

# These ranks are implemented by authoritative engine state rather than a
# generic bonus entity, so their JSON skill effect deliberately remains the
# zero-valued placeholder while the canonical registry marks them active.
RUNTIME_ACTIVE_RANKS = {
    "elementalRebirth",
    "divineMandate",
    "armorer",
    "archery",
    "battlecraft",
    "command",
    "warMachines",
    "discipline",
    "recruitment",
    "wisdom",
    "warcasting",
    "demonicGating",
    "logistics",
    "shroudOfMalassa",
    "bloodrage",
    "bulwarkOfTheMire",
    "spellcraft",
    "diplomacy",
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
    return (ArtPath() / image).is_file()


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
        self.assertEqual(len(runtime_ids), 32)
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

    def test_all_rank_images_resolve_to_selected_pack_or_core_frames(self):
        for key, skill in self.skills.items():
            for rank in RANKS:
                for slot, image in skill[rank]["images"].items():
                    with self.subTest(skill=key, rank=rank, slot=slot):
                        self.assertTrue(image_is_module_file_or_core_skill(image))

    def test_school_rank_descriptions_show_acquisition_caps_without_casting_locks(self):
        maximum_levels = {"basic": 3, "advanced": 4, "expert": 5}
        for key in SCHOOL_SKILLS:
            skill = self.skills[key]
            school_name = skill["name"][:-len(" Magic")]
            for rank, maximum_level in maximum_levels.items():
                with self.subTest(skill=key, rank=rank):
                    description = skill[rank]["description"]
                    self.assertIn(
                        f"Learn {school_name} spells up to Level {maximum_level}",
                        description,
                    )
                    self.assertIn(
                        "inscribed combat spells remain castable at any rank",
                        description,
                    )
                    self.assertNotIn("Wisdom still governs access", description)
                    self.assertNotRegex(
                        description,
                        r"(?i)(?:school rank|this rank).{0,50}(?:locks|requires).{0,30}(?:cast|casting)",
                    )

    def test_metamagic_uses_actual_rank_without_retired_bonus_or_provider(self):
        skill = self.skills["metamagic"]
        self.assertNotIn("combatStatus", skill)
        policy = self.perks["metamagicPolicy"]
        self.assertEqual(policy["version"], 7)
        # Double-cast uses come from the captured policy, never a skill bonus.
        self.assertEqual(policy["usesPerCombatByRank"], [0, 1, 2, 3])
        for rank, uses in zip(RANKS, ("1 double-cast use", "2 double-cast uses", "3 double-cast uses")):
            self.assertEqual(skill[rank]["effects"], {})
            description = skill[rank]["description"]
            self.assertIn("immediately cast one more combat spell at its normal Mana cost", description)
            self.assertIn(f"{uses} per combat", description)
            self.assertNotIn("Arcane Overflow", description)
            self.assertEqual(skill[rank]["description"], self.perks["skills"]
                             ["new-horizons:metamagic"]["ranks"][rank]["description"])
        providers = load("config/schemas/skill.json")["properties"]["combatStatus"]
        self.assertNotIn("metamagicUses", providers["properties"]["provider"]["enum"])

    def test_consecrated_arms_uses_dedicated_prayer_policy_without_generic_bonus(self):
        skill = self.skills["divineMandate"]
        self.assertEqual(skill["name"], "Consecrated Arms")
        self.assertEqual(self.perks["divineMandatePolicy"], {
            "version": 4,
            "potencyFaithBonusByRank": [0, 0, 50, 100],
            "ferventInvocationAdditionalFaith": 100,
            "chosen": True,
        })
        for rank, faith in zip(RANKS, (0, 50, 100)):
            self.assertEqual(skill[rank]["effects"], {})
            description = skill[rank]["description"]
            self.assertIn("Each committed Order grants one chosen prayer to one chosen recipient", description)
            self.assertIn(f"Prayer potency uses Spell Power + Knowledge + {faith} Faith", description)
            self.assertIn("duration uses actual Spell Power + Knowledge: min(4, 2 + floor(Faith / 150)) rounds", description)
            canonical = self.perks["skills"]["new-horizons:divineMandate"]["ranks"][rank]
            self.assertEqual(canonical["effect"]["status"], "active")
            self.assertEqual(description, canonical["description"])

    def test_generated_runtime_definitions_contain_no_retired_metamagic_carrier(self):
        module = load("Mods/new-horizons/mod.json")
        self.assertEqual(module["skills"]["metamagic"], self.skills["metamagic"])
        definitions = [("module", module)]
        for category in ("heroes", "spells", "creatures", "artifacts", "heroClasses"):
            definitions.extend((name, load("Mods/new-horizons/Content/" + name))
                               for name in module[category])

        def check(value, path):
            if isinstance(value, list):
                for index, child in enumerate(value):
                    check(child, f"{path}[{index}]")
            elif isinstance(value, dict):
                self.assertNotIn(value.get("type"),
                                 {"METAMAGIC_USES_PER_COMBAT", "RESERVED_METAMAGIC_USES_PER_COMBAT"}, path)
                self.assertNotEqual(value.get("provider"), "metamagicUses", path)
                for key, child in value.items():
                    check(child, path + "." + key)

        for name, definition in definitions:
            check(definition, name)

    def test_gain_chances_expose_only_general_active_skills(self):
        for key, skill in self.skills.items():
            with self.subTest(skill=key):
                if key in ACTIVE_GENERAL_GAIN_SKILLS:
                    expected = {"might": 6, "magic": 2} if key == "offense" else {"might": 2, "magic": 6}
                    self.assertEqual(skill["gainChance"], expected)
                    self.assertNotIn("special", skill.get("tags", {}))
                elif key == "authority":
                    self.assertEqual(skill["gainChance"], {"might": 1, "magic": 1})
                    self.assertTrue(skill["tags"].get("special"))
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
                    if key in {"authority", "divineMandate"}:
                        self.assertEqual(canonical["ranks"][rank]["effect"]["status"], "active")
                        self.assertEqual(skill[rank]["effects"], {})
                    elif key in ACTIVE_RANK_EFFECTS:
                        self.assertEqual(
                            canonical["ranks"][rank]["effect"]["status"], "active"
                        )
                        self.assertEqual(skill[rank]["effects"], ACTIVE_RANK_EFFECTS[key][rank])
                    elif key in RUNTIME_ACTIVE_RANKS:
                        self.assertEqual(
                            canonical["ranks"][rank]["effect"]["status"], "active"
                        )
                        self.assertTrue(skill[rank]["effects"])
                        if key in {"spellcraft", "diplomacy"}:
                            # Their active mechanics are resolved by the engine,
                            # not a duplicate generic SecondarySkill bonus.
                            self.assertEqual(skill[rank]["effects"], NO_OP)
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
