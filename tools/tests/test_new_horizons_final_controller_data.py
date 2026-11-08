# SPDX-License-Identifier: GPL-2.0-or-later
"""Final six specialty authoring contracts; not native/runtime acceptance."""

import copy
import re
import unittest

from jsonschema import Draft4Validator

from test_new_horizons_content import ROOT, load


EXPECTED = {
    "relentlessBarrageDamagePercent": 50, "pointBlankShotDamageBonusPercent": 100,
    "cripplingBoltSpeedReduction": 3, "cripplingBoltDuration": 3,
    "incendiaryShotPolicyVersion": 1, "ogreBloodlustPolicyVersion": 1,
    "runicReflectionPolicyVersion": 1,
}
RETIRED = (
    "closeQuartersDrill", "rangefinderFullDamageDistance", "fieldMaintenanceHealingHP",
    "gunneryProfessorInitiativeFlat", "gunneryProfessorFollowUpBonusPoints",
    "ogreBulwarkRetaliations", "runicEnduranceReductionPercent",
)


class FinalControllerDataTest(unittest.TestCase):
    def setUp(self):
        self.rules = load("config/newHorizonsHeroes.json")
        self.policy = self.rules["additionalSpecialties"]["ordinaryCreatureSpecialties"]
        self.validator = Draft4Validator(load("config/schemas/newHorizonsHeroes.json"))

    def test_exact_current_scalars_and_module_match_without_retired_fields(self):
        self.assertEqual({key: self.policy[key] for key in EXPECTED}, EXPECTED)
        self.validator.validate(self.rules)
        module = load("Mods/new-horizons/mod.json")
        self.assertEqual(module["settings"]["heroes"]["newHorizons"], self.rules)
        for key in RETIRED:
            self.assertNotIn(key, self.policy)
            changed = copy.deepcopy(self.rules)
            changed["additionalSpecialties"]["ordinaryCreatureSpecialties"][key] = 1
            self.assertFalse(self.validator.is_valid(changed), key)

    def test_fixed_values_and_cripple_pair_cannot_be_mispriced_or_partial(self):
        for key, expected in EXPECTED.items():
            for value in (0, expected + 1, True, None, str(expected), 0.5):
                changed = copy.deepcopy(self.rules)
                changed["additionalSpecialties"]["ordinaryCreatureSpecialties"][key] = value
                self.assertFalse(self.validator.is_valid(changed), (key, value))
        for missing in ("cripplingBoltSpeedReduction", "cripplingBoltDuration"):
            changed = copy.deepcopy(self.rules)
            changed["additionalSpecialties"]["ordinaryCreatureSpecialties"].pop(missing)
            self.assertFalse(self.validator.is_valid(changed), missing)

    def test_current_names_keep_the_six_stable_hero_and_art_identities(self):
        registry = (ROOT / "lib/entities/hero/OrdinaryCreatureSpecialtyRegistry.cpp").read_text()
        expected = {
            "core:valeska": ("Relentless Barrage", "relentlessBarrageDamagePercent", "ValeskaCloseQuartersDrill"),
            "core:christian": ("Point-Blank Shot", "pointBlankShotDamageBonusPercent", "ChristianRangefinder"),
            "core:gerwulf": ("Trapping Shot", "cripplingBoltSpeedReduction", "GerwulfFieldMaintenance"),
            "core:torosar": ("Fiery Shot", "incendiaryShotPolicyVersion", "TorosarGunneryProfessor"),
            "core:krellion": ("Ogre Bulwark", "ogreBloodlustPolicyVersion", "KrellionOgreBulwark"),
            "core:ufretin": ("Runic Endurance", "runicReflectionPolicyVersion", "UfretinRunicEndurance"),
        }
        for hero, (name, key, art) in expected.items():
            rows = re.findall(r'\{"' + re.escape(hero) + r'",.*?\},', registry, re.S)
            self.assertEqual(len(rows), 1, hero)
            self.assertIn(f'"{name}", "{key}", BonusType::NONE', rows[0])
            self.assertIn('"newHorizons/heroSpecialties/' + art + '"', rows[0])

    def test_reflection_does_not_add_or_remove_native_resistance(self):
        creatures = load("config/creatures/rampart.json")
        for key, chance in (("dwarf", 20), ("battleDwarf", 40)):
            resistance = [bonus for bonus in creatures[key]["abilities"].values()
                          if bonus["type"] == "MAGIC_RESISTANCE"]
            self.assertEqual(resistance, [{"type": "MAGIC_RESISTANCE", "val": chance}])
        metadata = load("config/bonuses.json")["NATIVE_RESISTANCE_REFLECTION"]
        self.assertIn("ordinary resistance roll", metadata["description"])
        self.assertIn("adds no Magic Resistance or independent chance", metadata["description"])


if __name__ == "__main__":
    unittest.main()
