# SPDX-License-Identifier: GPL-2.0-or-later
"""Public native bonus labels and hidden internal-marker contracts."""

import unittest

from jsonschema import Draft4Validator

from test_new_horizons_content import load


class NativeBonusMetadataTest(unittest.TestCase):
    def test_public_native_bonus_metadata_is_schema_valid_and_precise(self):
        bonuses = load("config/bonuses.json")
        validator = Draft4Validator(load("config/schemas/bonus.json"))
        for key in ("MAGICAL_DAMAGE_IMMUNITY", "CANNOT_SHOOT",
                    "CREATURE_ATTACK_TARGET_PROTECTION", "CASUALTY_DAMAGE_PERCENTAGE",
                    "PHYSICAL_DEFENSE", "NATIVE_RESISTANCE_REFLECTION"):
            row = bonuses[key]
            validator.validate(row)
            self.assertFalse(row.get("hidden", False), key)
            self.assertTrue(row["description"].startswith("{"), key)
        self.assertIn("Non-damaging magical effects remain applicable",
                      bonuses["MAGICAL_DAMAGE_IMMUNITY"]["description"])
        self.assertIn("does not prevent spells or collateral damage",
                      bonuses["CREATURE_ATTACK_TARGET_PROTECTION"]["description"])
        self.assertIn("retaliations", bonuses["CASUALTY_DAMAGE_PERCENTAGE"]["description"])
        self.assertIn("does not reduce magical damage", bonuses["PHYSICAL_DEFENSE"]["description"])
        self.assertIn("adds no Magic Resistance or independent chance",
                      bonuses["NATIVE_RESISTANCE_REFLECTION"]["description"])

    def test_dynamic_dependency_marker_is_hidden_and_does_not_propagate_help(self):
        row = load("config/bonuses.json")["DYNAMIC_CREATURE_DAMAGE"]
        Draft4Validator(load("config/schemas/bonus.json")).validate(row)
        self.assertEqual(row, {"hidden": True, "blockDescriptionPropagation": True})


if __name__ == "__main__":
    unittest.main()
