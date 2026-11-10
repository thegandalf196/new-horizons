#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Content admission and identity checks; no native or gameplay acceptance."""
import copy
import json
from pathlib import Path
import unittest

from jsonschema import Draft4Validator, RefResolver
if __package__:
    from .test_new_horizons_skill_entities import parse_jsonc
else:
    from test_new_horizons_skill_entities import parse_jsonc


ROOT = Path(__file__).resolve().parents[2]
AUTHORITY = "new-horizons:authority"
WARFARE = "new-horizons:command"


def load(relative):
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


def validator(name):
    schema = load("config/schemas/" + name + ".json")
    Draft4Validator.check_schema(schema)
    bonus_path = ROOT / "config/schemas/bonusInstance.json"
    store = {bonus_path.as_uri(): parse_jsonc(bonus_path.read_text(encoding="utf-8"))} if name == "skill" else {}
    return Draft4Validator(schema, resolver=RefResolver(
        base_uri=(ROOT / "config/schemas").as_uri() + "/", referrer=schema, store=store))


class CommandAuthorityContentTest(unittest.TestCase):
    def setUp(self):
        self.perks = load("config/newHorizonsPerks.json")
        self.heroes = load("config/newHorizonsHeroes.json")
        self.skills = load("config/newHorizonsSkills.json")
        self.perk_validator = validator("newHorizonsPerks")
        self.hero_validator = validator("newHorizonsHeroes")

    def test_current_registries_and_entity_registration(self):
        self.perk_validator.validate(self.perks)
        self.hero_validator.validate(self.heroes)
        self.assertEqual(len(self.perks["skills"]), 32)
        self.assertEqual(set(self.perks["skills"]), {"new-horizons:" + key for key in self.skills})
        entity_validator = validator("skill")
        for entity in self.skills.values():
            entity_validator.validate(entity)
        for row in self.heroes["skillOfferWeights"].values():
            self.assertEqual(set(row), set(self.perks["skills"]))
        self.assertEqual(self.skills["command"]["name"], "Warfare")
        self.assertEqual(self.skills["discipline"]["name"], "Leadership")
        self.assertEqual(self.perks["skills"][WARFARE]["availability"], "Might-exclusive")

    def test_marker_is_strict_and_cannot_reinterpret_a_31_skill_capture(self):
        for invalid in (None, {}, [], True, {"version": True}, {"version": 0},
                        {"version": 2}, {"version": "1"}, {"version": 1, "extra": 0}):
            for document, check in ((self.perks, self.perk_validator), (self.heroes, self.hero_validator)):
                with self.subTest(invalid=invalid, document="skills" in document):
                    candidate = copy.deepcopy(document)
                    candidate["commandAuthorityPolicy"] = invalid
                    self.assertFalse(check.is_valid(candidate))
        historical = copy.deepcopy(self.perks)
        historical.pop("commandAuthorityPolicy")
        historical.pop("warfarePolicyVersion")
        historical["commandPolicyVersion"] = 1
        historical["skills"].pop(AUTHORITY)
        historical.pop("necromancyPolicy")
        self.perk_validator.validate(historical)
        historical["commandAuthorityPolicy"] = {"version": 1}
        self.assertFalse(self.perk_validator.is_valid(historical))
        historical_heroes = copy.deepcopy(self.heroes)
        historical_heroes.pop("commandAuthorityPolicy")
        for row in historical_heroes["skillOfferWeights"].values():
            row.pop(AUTHORITY)
        self.hero_validator.validate(historical_heroes)
        historical_heroes["commandAuthorityPolicy"] = {"version": 1}
        self.assertFalse(self.hero_validator.is_valid(historical_heroes))

    def test_current_marker_requires_complete_tables_and_no_finite_policy(self):
        candidate = copy.deepcopy(self.perks)
        candidate["commandPolicyVersion"] = 1
        self.assertFalse(self.perk_validator.is_valid(candidate))
        for change in ("missingTable", "missingAuthority", "extraSkill", "missingMarker"):
            candidate = copy.deepcopy(self.heroes)
            if change == "missingTable":
                candidate.pop("skillOfferWeights")
            elif change == "missingMarker":
                candidate.pop("commandAuthorityPolicy")
            else:
                row = candidate["skillOfferWeights"]["core:knight"]
                if change == "missingAuthority":
                    row.pop(AUTHORITY)
                else:
                    row["new-horizons:unknown"] = 1
            with self.subTest(change=change):
                self.assertFalse(self.hero_validator.is_valid(candidate))

    def test_authority_values_are_slot_scoped_and_no_followup_choice_is_registered(self):
        skill = self.perks["skills"][AUTHORITY]
        self.assertEqual(skill["availability"], "All heroes")
        self.assertEqual([skill["ranks"][rank]["effect"]["parameters"] for rank in
                          ("basic", "advanced", "expert")], [{"commandBonus": value} for value in (1, 2, 3)])
        expected = [
            ("offensiveDrills", "basic", ["core"], {"creatureAttack": 2}),
            ("defensiveDrills", "basic", ["core"], {"creatureDefense": 2}),
            ("conditioning", "basic", ["core"], {"maximumHealthPercent": 10}),
            ("battleReadiness", "basic", ["core"], {"initiative": 1}),
            ("officerCorps", "advanced", ["elite"], {"creatureAttack": 2, "creatureDefense": 2}),
            ("maneuverTraining", "advanced", ["core", "elite", "champion"], {"battlefieldSpeed": 1}),
            ("counterattackTraining", "advanced", ["core", "elite", "champion"], {"additionalRetaliations": 1}),
            ("protectiveTraining", "advanced", ["core", "elite", "champion"], {"magicalReductionPercent": 10}),
            ("veteranCorps", "expert", ["core"], {"directAttackDamagePercent": 15}),
            ("grandMarshal", "expert", ["elite", "champion"], {"directAttackDamagePercent": 10}),
        ]
        actual = [(row["id"], row["requires"], row["effect"]["parameters"]) for row in skill["perks"]]
        self.assertEqual(actual, [(AUTHORITY + "." + key, tier, {"slotGrades": grades, **values})
                                  for key, tier, grades, values in expected])
        self.assertTrue(all(row["effect"]["status"] == "active" for row in skill["perks"]))
        self.assertTrue(all(row["effect"]["status"] == "active" for row in skill["ranks"].values()))

    def test_authority_offers_follow_physical_growth_and_bind_all_twelve_packed_roles(self):
        self.assertEqual(self.skills["authority"]["gainChance"], {"might": 1, "magic": 1})
        self.assertNotIn("offerWeightsUnresolved", self.skills["authority"]["tags"])
        expected = dict(zip(self.heroes["classProfiles"], (8, 3, 8, 2, 6, 1, 8, 3, 7, 3, 8, 2, 10, 6, 10, 2, 6, 1)))
        self.assertEqual({key: row[AUTHORITY] for key, row in self.heroes["skillOfferWeights"].items()}, expected)
        resources = {row["resource"] for row in load("assets/new-horizons/runtime-art-manifest.json")["entries"]}
        for rank in ("basic", "advanced", "expert"):
            for role in ("small", "medium", "large", "scenarioBonus"):
                name = f"NH_authority_{rank}_{role}.png"
                self.assertEqual(self.skills["authority"][rank]["images"][role], name)
                self.assertIn("SPRITES/" + name, resources)
            self.assertEqual(self.skills["authority"][rank]["effects"], {})
        candidate = copy.deepcopy(self.heroes)
        candidate["skillOfferWeights"]["core:wizard"][AUTHORITY] = 0
        self.assertFalse(self.hero_validator.is_valid(candidate))

    def test_released_passive_ids_and_grand_marshal_migration_are_distinct(self):
        rows = {row["id"]: row for row in self.perks["skills"][WARFARE]["perks"]}
        self.assertEqual(len(rows), 10)
        self.assertEqual(rows[WARFARE + ".crisisCommand"]["name"], "Unbroken Ranks")
        self.assertEqual(rows[WARFARE + ".doubleCommand"]["name"], "Master Commander")
        self.assertNotIn(WARFARE + ".seizeInitiative", rows)
        decisive = rows[WARFARE + ".decisiveOrders"]
        self.assertEqual(decisive["legacyIdentity"], {"id": WARFARE + ".seizeInitiative", "commandPolicyVersion": 1})
        candidate = copy.deepcopy(self.perks)
        candidate["skills"][AUTHORITY]["perks"][-1]["legacyIdentity"] = decisive["legacyIdentity"]
        self.assertFalse(self.perk_validator.is_valid(candidate))
        for invalid in (0, True, None):
            candidate = copy.deepcopy(self.perks)
            candidate["skills"][WARFARE]["perks"][-1]["legacyIdentity"]["commandPolicyVersion"] = invalid
            self.assertFalse(self.perk_validator.is_valid(candidate))

    def test_effect_metadata_rejects_unknown_keys_bad_slots_and_invalid_values(self):
        for invalid in ({}, {"slotGrades": ["core", "core"]}, {"slotGrades": ["conflux"]},
                        {"creatureAttack": -1}, {"creatureAttack": "2"}, {"creatureAttack": True},
                        {"extraChoice": 1}, {"requiresActiveOrder": False}):
            candidate = copy.deepcopy(self.perks)
            candidate["skills"][AUTHORITY]["perks"][0]["effect"]["parameters"] = invalid
            with self.subTest(invalid=invalid):
                self.assertFalse(self.perk_validator.is_valid(candidate))

    def test_order_resource_profiles_are_mutually_exclusive(self):
        rules = load("config/newHorizonsCombat.json")["combat"]["heroCommands"]
        check = validator("newHorizonsCombatV4")
        check.validate(rules)
        self.assertEqual(rules["warfarePolicyVersion"], 1)
        candidate = copy.deepcopy(rules)
        candidate["commandResourcePolicyVersion"] = 1
        self.assertFalse(check.is_valid(candidate))
        candidate.pop("warfarePolicyVersion")
        candidate.pop("formationRating")
        candidate["commands"]["feint"]["effects"]["retaliationDamageReductionPercent"] = {"base": 15, "leadership": .015, "cap": 60}
        candidate["commands"]["vanguard"]["duration"] = {"leadershipPerRound": 750, "minimum": 1, "maximum": 3}
        candidate["commands"]["secondWind"]["effects"]["additionalActivationDamagePercent"] = {"base": 50, "attack": 0, "defense": 0}
        check.validate(candidate)
        candidate.pop("commandResourcePolicyVersion")
        self.assertFalse(check.is_valid(candidate))

    def test_formation_policy_is_complete_strict_and_cannot_relabel_legacy_formulas(self):
        rules = load("config/newHorizonsCombat.json")["combat"]["heroCommands"]
        check = validator("newHorizonsCombatV4")
        self.assertEqual(rules["formationRating"], {"version": 1, "attributeDivisor": 2, "commandMultiplier": 10})
        self.assertEqual(rules["commands"]["secondWind"]["effects"]["additionalActivationDamagePercent"],
                         {"base": 50, "formationRating": .10, "cap": 100})
        self.assertEqual(rules["commands"]["feint"]["effects"]["retaliationDamageReductionPercent"],
                         {"base": 15, "formationRating": .10, "cap": 60})
        self.assertEqual(rules["commands"]["vanguard"]["duration"],
                         {"formationRatingPerRound": 150, "minimum": 1, "maximum": 3})
        for invalid in (None, {}, {"version": True, "attributeDivisor": 2, "commandMultiplier": 10},
                        {"version": 1, "attributeDivisor": 0, "commandMultiplier": 10},
                        {"version": 1, "attributeDivisor": 2, "commandMultiplier": -1},
                        {"version": 1, "attributeDivisor": 2, "commandMultiplier": 10, "unknown": 1}):
            candidate = copy.deepcopy(rules)
            candidate["formationRating"] = invalid
            self.assertFalse(check.is_valid(candidate), invalid)
        candidate = copy.deepcopy(rules)
        candidate["commands"]["feint"]["effects"]["retaliationDamageReductionPercent"]["leadership"] = .015
        self.assertFalse(check.is_valid(candidate))
        candidate = copy.deepcopy(rules)
        candidate["commands"]["vanguard"]["duration"]["leadershipPerRound"] = 750
        self.assertFalse(check.is_valid(candidate))

    def test_formation_replacement_preserves_other_attribute_components(self):
        commands = load("config/newHorizonsCombat.json")["combat"]["heroCommands"]["commands"]
        expected = {
            "charge": {"meleeDamagePercent": {"base": 10, "attack": .20, "defense": 0}},
            "holdTheLine": {"damageReductionPercent": {"base": 10, "attack": 0, "defense": .20}},
            "focusFire": {"rangedDamagePercent": {"base": 5, "attack": .15, "defense": 0}},
            "riposte": {"meleeDamageReductionPercent": {"base": 5, "attack": 0, "defense": .10},
                        "retaliationDamagePercent": {"base": 5, "attack": .15, "defense": 0}},
            "brace": {"preemptiveDamagePercent": {"base": 50, "attack": 0, "defense": .25}},
            "vanguard": {"primaryAndMorale": {"base": 1, "ratingStep": 75, "cap": 3},
                         "initiativePercent": {"base": 5, "minimumRating": .05, "cap": 18}},
        }
        for order, effects in expected.items():
            with self.subTest(order=order):
                self.assertEqual(commands[order]["effects"], effects)

    def test_approved_faction_replacements_leave_no_current_leadership_discount(self):
        necromancy = self.perks["skills"]["new-horizons:necromancy"]
        lord = necromancy["perks"][-1]
        self.assertEqual(lord["effect"]["parameters"], {"maximumHealthPercent": 25})
        self.assertNotIn("Leadership", json.dumps(necromancy))
        policy = self.perks["necromancyPolicy"]
        self.assertEqual(policy["version"], 3)
        self.assertEqual(policy["lordOfTheDeadMaximumHealthPercent"], 25)
        self.assertFalse(any("Leadership" in key for key in policy))
        primordial = next(row for row in self.perks["skills"]["new-horizons:elementalRebirth"]["perks"]
                          if row["name"] == "Primordial Host")
        self.assertEqual(primordial["id"], "new-horizons:elementalRebirth.phoenixSpark")
        self.assertEqual(primordial["effect"]["parameters"], {"directAttackDamagePercent": 15})
        self.assertIn("independent of Elemental Attunement", primordial["details"])


if __name__ == "__main__":
    unittest.main()
