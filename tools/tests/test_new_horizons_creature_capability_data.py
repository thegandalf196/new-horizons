# SPDX-License-Identifier: GPL-2.0-or-later
"""Current capability authoring and identity contracts; not runtime acceptance."""

import copy
import re
import unittest

from jsonschema import Draft4Validator

from test_new_horizons_content import ROOT, load


class CreatureCapabilityDataTest(unittest.TestCase):
    def setUp(self):
        self.rules = load("config/newHorizonsHeroes.json")
        self.policy = self.rules["additionalSpecialties"]["ordinaryCreatureSpecialties"]
        self.validator = Draft4Validator(load("config/schemas/newHorizonsHeroes.json"))

    def test_current_fields_have_exact_values_and_superseded_fields_are_rejected(self):
        self.validator.validate(self.rules)
        for key, value in (("stonecallerAmmunition", 12), ("archshamanSpellMastery", 2),
                           ("goldStandardUpgrade", 1)):
            self.assertEqual(self.policy[key], value)
            for invalid in (0, value + 1, True, None, str(value), 0.5):
                changed = copy.deepcopy(self.rules)
                changed["additionalSpecialties"]["ordinaryCreatureSpecialties"][key] = invalid
                self.assertFalse(self.validator.is_valid(changed), (key, invalid))
        for key in ("defendDefense", "ogreWarcallerDamagePercent", "fieldReassemblyHealingHP"):
            self.assertNotIn(key, self.policy)
            changed = copy.deepcopy(self.rules)
            changed["additionalSpecialties"]["ordinaryCreatureSpecialties"][key] = 1
            self.assertFalse(self.validator.is_valid(changed), key)
        module = load("Mods/new-horizons/mod.json")
        self.assertEqual(module["settings"]["heroes"]["newHorizons"], self.rules)

    def test_ogre_magi_retain_the_existing_shared_cast_pool_and_power(self):
        ogre = load("config/creatures/stronghold.json")["ogreMage"]
        abilities = ogre["abilities"]
        self.assertEqual(ogre["index"], 91)
        self.assertEqual(abilities["castsCount"], {"type": "CASTS", "val": 3})
        self.assertEqual(abilities["castLength"], {"type": "CREATURE_ENCHANT_POWER", "val": 3})
        self.assertEqual(abilities["castsBloodlust"],
                         {"type": "SPELLCASTER", "subtype": "bloodlust", "val": 2})

    def test_inverted_golem_identifiers_remain_unchanged(self):
        tower = load("config/creatures/tower.json")
        self.assertEqual(tower["ironGolem"]["index"], 32)  # Actual Stone Golem.
        self.assertEqual(tower["stoneGolem"]["index"], 33)  # Actual Iron Golem.
        self.assertEqual(load("config/creatures/neutral.json")["goldGolem"]["index"], 116)

    def test_metadata_names_and_art_are_current_without_reinterpreting_erdamon_hp(self):
        registry = (ROOT / "lib/entities/hero/OrdinaryCreatureSpecialtyRegistry.cpp").read_text()
        expected = {
            "core:thunar": ("core:earthElemental", "Stonecaller", "stonecallerAmmunition", "ThunarEarthshaper"),
            "core:vey": ("core:ogreMage", "Archshaman", "archshamanSpellMastery", "VeyOgreWarcaller"),
            "core:josephine": ("core:stoneGolem", "Gold Standard", "goldStandardUpgrade", "JosephineFieldReassembly"),
            "core:erdamon": ("core:earthElemental", "Earthshaper", "maximumHealthPercent", "ErdamonStonecaller"),
        }
        for hero, (creature, name, key, art) in expected.items():
            rows = re.findall(r'\{"' + re.escape(hero) + r'",.*?\},', registry, re.S)
            self.assertEqual(len(rows), 1, hero)
            self.assertIn(f'"{creature}", "{name}", "{key}"', rows[0])
            self.assertIn('"newHorizons/heroSpecialties/' + art + '"', rows[0])
        self.assertEqual(self.policy["maximumHealthPercent"], 20)
        erdamon = re.search(r'\{"core:erdamon",.*?\},', registry, re.S)[0]
        self.assertIn("BonusType::STACK_HEALTH", erdamon)
        self.assertIn("BonusValueType::PERCENT_TO_BASE", erdamon)
        self.assertNotIn("Rebirth-created", erdamon)


if __name__ == "__main__":
    unittest.main()
