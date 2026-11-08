# SPDX-License-Identifier: GPL-2.0-or-later
"""Nine specialty authoring contracts; not native mechanics or art acceptance."""

import copy
import json
import re
import unittest
from pathlib import Path

from jsonschema import Draft4Validator


ROOT = Path(__file__).resolve().parents[2]
EXPECTED = {
    "bloodScentMaximumDamagePercent": 100, "treantmootProtection": 1,
    "chillingPresenceReductionPercent": 10, "chillingPresenceMoraleReduction": 1,
    "chillingPresenceLuckReduction": 1, "boneFormationFirstBandSize": 10,
    "boneFormationFirstBandLimit": 100, "boneFormationSecondBandSize": 20,
    "boneFormationSecondBandLimit": 300, "boneFormationRemainingBandSize": 50,
    "perfectMountMagicalDamageImmunity": 1, "furyOfTheHordeAttack": 2,
    "furyOfTheHordeDefense": 2, "furyOfTheHordeDamage": 1,
    "orcRageMaximumDamagePercent": 100, "wyvernVenomPolicyVersion": 1,
    "dispellingShotMageChancePercent": 20, "dispellingShotArchMageChancePercent": 40,
    "cyclopeanStunChancePercent": 25,
}
RETIRED = (
    "bloodScentDamagePercent", "livingBastionDefense", "boneFormationDefense",
    "wingbeatInitiativePercent", "cruelVolleyDamagePercent", "wyvernsPreyDamagePercent",
    "measuredVolleyMdrPenetrationPercent", "cyclopeanBreachIgnorePercent",
)


def load(path):
    return json.loads((ROOT / path).read_text(encoding="utf-8"))


class SpecialtyBatch3DataTest(unittest.TestCase):
    def setUp(self):
        self.rules = load("config/newHorizonsHeroes.json")
        self.policy = self.rules["additionalSpecialties"]["ordinaryCreatureSpecialties"]
        self.validator = Draft4Validator(load("config/schemas/newHorizonsHeroes.json"))

    def test_current_fixed_scalars_and_shipped_profile_match(self):
        self.assertEqual({key: self.policy[key] for key in EXPECTED}, EXPECTED)
        self.validator.validate(self.rules)
        module = load("Mods/new-horizons/mod.json")
        self.assertEqual(module["settings"]["heroes"]["newHorizons"], self.rules)
        for key in RETIRED:
            self.assertNotIn(key, self.policy)
            changed = copy.deepcopy(self.rules)
            changed["additionalSpecialties"]["ordinaryCreatureSpecialties"][key] = 1
            self.assertFalse(self.validator.is_valid(changed), key)

    def test_fixed_values_reject_zero_wrong_amounts_and_wrong_types(self):
        for key, expected in EXPECTED.items():
            for invalid in (0, expected + 1, None, True, str(expected), 0.5, {}, []):
                changed = copy.deepcopy(self.rules)
                changed["additionalSpecialties"]["ordinaryCreatureSpecialties"][key] = invalid
                self.assertFalse(self.validator.is_valid(changed), (key, invalid))

    def test_compound_policies_require_every_member(self):
        groups = (
            ("dispellingShotMageChancePercent", "dispellingShotArchMageChancePercent"),
            ("chillingPresenceReductionPercent", "chillingPresenceMoraleReduction",
             "chillingPresenceLuckReduction"),
            ("boneFormationFirstBandSize", "boneFormationFirstBandLimit",
             "boneFormationSecondBandSize", "boneFormationSecondBandLimit",
             "boneFormationRemainingBandSize"),
            ("furyOfTheHordeAttack", "furyOfTheHordeDefense", "furyOfTheHordeDamage",
             "orcRageMaximumDamagePercent"),
        )
        for group in groups:
            for absent in group:
                changed = copy.deepcopy(self.rules)
                changed["additionalSpecialties"]["ordinaryCreatureSpecialties"].pop(absent)
                self.assertFalse(self.validator.is_valid(changed), absent)

    def test_nine_metadata_rows_keep_stable_heroes_and_selected_art_paths(self):
        registry = (ROOT / "lib/entities/hero/OrdinaryCreatureSpecialtyRegistry.cpp").read_text()
        expected = {
            "core:vokial": ("Blood Scent", "bloodScentMaximumDamagePercent", "VokialBloodScent"),
            "core:ryland": ("Treantmoot", "treantmootProtection", "RylandLivingBastion"),
            "core:charna": ("Chilling Presence", "chillingPresenceReductionPercent", "CharnaChillingPresence"),
            "core:galthran": ("Bone Formation", "boneFormationFirstBandSize", "GalthranBoneFormation"),
            "core:aeris": ("Perfect Mount", "perfectMountMagicalDamageImmunity", "AerisWingbeat"),
            "core:jabarkas": ("Fury of the Horde", "furyOfTheHordeAttack", "JabarkasCruelVolley"),
            "core:broghild": ("Wyvern's Prey", "wyvernVenomPolicyVersion", "BroghildWyvernsPrey"),
            "core:theodorus": ("Nullifying Shot", "dispellingShotMageChancePercent", "TheodorusMeasuredVolley"),
            "core:yog": ("Concussive Shot", "cyclopeanStunChancePercent", "YogCyclopeanBreach"),
        }
        for hero, (name, key, artwork) in expected.items():
            rows = re.findall(r'\{"' + re.escape(hero) + r'",.*?\},', registry, re.S)
            self.assertEqual(len(rows), 1, hero)
            row = rows[0]
            self.assertIn(json.dumps(name) + ', "' + key + '", BonusType::NONE', row)
            self.assertIn('"newHorizons/heroSpecialties/' + artwork + '"', row)
            self.assertNotIn("BonusType::PRIMARY_SKILL", row)


if __name__ == "__main__":
    unittest.main()
