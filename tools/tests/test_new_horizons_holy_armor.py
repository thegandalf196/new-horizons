#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Focused offline content checks for the registered Holy Armor spell."""
import json
from pathlib import Path
import re
import struct
import unittest

ROOT = Path(__file__).resolve().parents[2]
STRING = r'"(?:\\.|[^"\\])*"'


def load(path):
    text = (ROOT / path).read_text(encoding="utf-8")
    text = re.sub(STRING + r'|//[^\n]*|/\*[\s\S]*?\*/',
                  lambda match: match[0] if match[0].startswith('"') else '', text)
    text = re.sub(STRING + r'|,(?=\s*[}\]])',
                  lambda match: match[0] if match[0].startswith('"') else '', text)
    return json.loads(text)


def png_size(path):
    header = path.read_bytes()[:24]
    if header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise ValueError(f"Invalid PNG header: {path}")
    return struct.unpack(">II", header[16:24])


class HolyArmorContentTest(unittest.TestCase):
    def test_canonical_roster_and_definition(self):
        rules = load("config/newHorizonsMagic.json")
        roster = rules["spells"]["new-horizons:holyArmor"]
        self.assertEqual(roster["schools"], ["new-horizons:light"])
        self.assertEqual(roster["level"], 2)
        self.assertEqual(roster["costs"], [8, 8, 8, 8])

        definition = load("Mods/new-horizons/Content/config/spells/newHorizons.json")["holyArmor"]
        self.assertEqual(definition["name"], "Holy Armor")
        self.assertEqual(definition["type"], "combat")
        self.assertEqual(definition["school"], {"new-horizons:light": True})
        self.assertEqual(definition["level"], 2)
        self.assertEqual(definition["targetType"], "CREATURE")
        self.assertEqual(definition["flags"], {"positive": True})
        self.assertEqual(set(definition["levels"]), {"none", "basic", "advanced", "expert"})

        for rank, level in definition["levels"].items():
            with self.subTest(rank=rank):
                self.assertEqual(level["range"], "0")
                self.assertEqual(level["cost"], 8)
                self.assertEqual(level["targetModifier"], {"smart": True})
                self.assertIn("min(60%, 30% + 0.20% x Spell Power)", level["description"])
                self.assertIn("for 2 rounds", level["description"])
                self.assertIn("magical damage", level["description"])

                effects = level["battleEffects"]
                self.assertEqual(set(effects), {"holyArmor"})
                self.assertEqual(effects["holyArmor"]["type"], "timed")
                reduction = effects["holyArmor"]["bonus"]["magicalDamageReduction"]
                self.assertEqual(reduction, {
                    "val": 30,
                    "type": "SPELL_DAMAGE_REDUCTION",
                    "subtype": "any",
                    "duration": "N_TURNS",
                    "turns": 2,
                })

    def test_provisional_holy_armor_art_is_bound_at_each_ui_size(self):
        definition = load("Mods/new-horizons/Content/config/spells/newHorizons.json")["holyArmor"]
        expected = {
            "iconBook": ("NH_spell_holy_armor_44.png", (44, 44)),
            "iconScroll": ("NH_spell_holy_armor_44.png", (44, 44)),
            "iconScenarioBonus": ("NH_spell_holy_armor_32.png", (32, 32)),
            "iconEffect": ("NH_spell_holy_armor_30.png", (30, 30)),
            "iconImmune": ("NH_spell_holy_armor_30.png", (30, 30)),
        }
        for role, (filename, size) in expected.items():
            with self.subTest(role=role):
                self.assertEqual(definition["graphics"][role], filename)
                self.assertEqual(png_size(ROOT / "Mods/new-horizons/Images" / filename), size)

    def test_timed_script_scales_only_the_spell_power_term_and_keeps_aegis_inactive(self):
        script = (ROOT / "scripts/spells/timed.lua").read_text(encoding="utf-8")
        self.assertIn('local HOLY_ARMOR_SPELL = "new-horizons:holyArmor"', script)
        self.assertIn("mechanics:getSpellPowerCoefficientBasisPoints()", script)
        self.assertIn("mechanics:getEffectPower()", script)
        self.assertIn("HOLY_ARMOR_MAX_REDUCTION_PERCENT", script)
        self.assertIn("if spellKey == HOLY_ARMOR_SPELL then return end", script)
        self.assertIn("function Script:adjustHolyArmorPowerTerm", script)
        self.assertIn("return spellPowerTerm", script)

    def test_damage_script_keeps_legacy_immunity_but_defers_new_horizons_reduction(self):
        script = (ROOT / "scripts/spells/damage.lua").read_text(encoding="utf-8")
        self.assertIn("mechanics:usesNewHorizonsMultiplicativeMDR()", script)
        self.assertIn('subtype = "any"}) >= 100', script)
        self.assertIn("total >= 100 and not useIndependentMagicalDamageReduction", script)
        self.assertIn("return Base.isReceptive(self, mechanics, unit)", script)


if __name__ == "__main__":
    unittest.main()
