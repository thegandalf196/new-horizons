#!/usr/bin/env python3
"""Focused contract checks for New Horizons speed/initiative content patches.

The spell assertion composes the canonical Ice Bolt definition with the NH
object patch before checking it.  This catches a patch that looks valid in
isolation but would erase the base damage effect when the object is loaded.
"""

import copy
import json
from pathlib import Path
import re
import unittest

from jsonschema import Draft4Validator

ROOT = Path(__file__).resolve().parents[2]
STRING = r'"(?:\\.|[^"\\])*"'


def parse_jsonc(text):
    """Parse the repository's JSON-with-comments/trailing-comma format."""
    text = re.sub(STRING + r'|//[^\n]*|/\*[\s\S]*?\*/',
                  lambda match: match[0] if match[0].startswith('"') else '', text)
    text = re.sub(STRING + r'|,(?=\s*[}\]])',
                  lambda match: match[0] if match[0].startswith('"') else '', text)
    return json.loads(text)


def load(relative):
    return parse_jsonc((ROOT / relative).read_text(encoding="utf-8"))


def merge_objects(base, patch):
    """Apply the object-field merge used by a content patch."""
    for key, value in patch.items():
        if isinstance(value, dict) and isinstance(base.get(key), dict):
            merge_objects(base[key], value)
        else:
            base[key] = copy.deepcopy(value)


class NewHorizonsInitiativeContentTest(unittest.TestCase):
    def test_ice_bolt_composes_speed_only_movement_effect(self):
        patch = load("Mods/new-horizons/Content/config/spells/iceBolt.json")["core:iceBolt"]
        base = load("config/spells/offensive.json")["iceBolt"]
        effective = copy.deepcopy(base)
        merge_objects(effective, patch)
        level = effective["levels"]["base"]
        effects = level["battleEffects"]
        effect = effects["speedDebuff"]
        bonus = effect["bonus"]["stacksMovementRange"]

        self.assertEqual(effect["type"], "timed")
        self.assertEqual(bonus["type"], "STACKS_MOVEMENT_RANGE")
        self.assertEqual(bonus["val"], -2)
        self.assertEqual(bonus["duration"], "N_TURNS")
        self.assertIn("directDamage", effects)
        self.assertNotIn("effects", level)
        battle_effect_schema = load("config/schemas/spell.json")["definitions"]["battleEffect"]
        for battle_effect in effects.values():
            Draft4Validator(battle_effect_schema).validate(battle_effect)
        self.assertNotIn("STACKS_SPEED", json.dumps(effective))
        self.assertNotIn("STACKS_INITIATIVE", json.dumps(effective))

    def test_slow_is_converted_to_initiative_by_new_horizons_timed_script(self):
        script = (ROOT / "scripts/spells/timed.lua").read_text(encoding="utf-8")
        self.assertIn('spellKey == "core:slow" and mechanics:usesNewHorizonsMagic()', script)
        self.assertIn('nb.type = "STACKS_INITIATIVE"', script)
        self.assertIn('nb.valueType = "ADDITIVE_VALUE"', script)

    def test_arch_mage_keeps_mage_speed_but_has_higher_initiative(self):
        patch = load("Mods/new-horizons/Content/config/creatures/tower.json")
        mage = patch["core:mage"]
        arch_mage = patch["core:archMage"]

        self.assertEqual(mage["speed"], arch_mage["speed"])
        self.assertEqual(mage["speed"], 5)
        self.assertEqual(arch_mage["initiative"], 7)
        self.assertLess(mage["initiative"], arch_mage["initiative"])
        self.assertEqual(mage["level"], arch_mage["level"], "Magi share the Elite rank")
        self.assertEqual(patch["core:genie"]["level"], 4)
        self.assertEqual(patch["core:masterGenie"]["level"], 4)

    def test_tower_town_swaps_genies_and_magi_before_rank_presentation(self):
        patch = load("Mods/new-horizons/Content/config/factions/towerCreatureRanks.json")
        creatures = patch["core:tower"]["town"]["creatures"]
        self.assertEqual(creatures["modify@3"], ["genie", "masterGenie"])
        self.assertEqual(creatures["modify@4"], ["mage", "archMage"])

        structures = patch["core:tower"]["town"]["structures"]
        expected_structures = {
            "dwellingLvl4": ("TBTWDW_4.def", 613, 95, "BoTGen1.pcx", "TOTGEN1.bmp", "TZTGEN1.bmp"),
            "dwellingLvl5": ("TBTWDW_3.def", 511, 75, "BoTMag1.pcx", "TOTMAG1.bmp", "TZTMAG1.bmp"),
            "dwellingUpLvl4": ("TBTWUP_4.def", 613, 74, "BoTGen2.pcx", "TOTGEN2.bmp", "TZTGEN2.bmp"),
            "dwellingUpLvl5": ("TBTWUP_3.def", 511, 8, "BoTMag2.pcx", "TOTMAG2.bmp", "TZTMAG2.bmp"),
        }
        for name, (animation, x, y, campaign_bonus, border, area) in expected_structures.items():
            with self.subTest(structure=name):
                self.assertEqual(structures[name]["animation"], animation)
                self.assertEqual((structures[name]["x"], structures[name]["y"]), (x, y))
                self.assertEqual(structures[name]["campaignBonus"], campaign_bonus)
                self.assertEqual(structures[name]["border"], border)
                self.assertEqual(structures[name]["area"], area)
        self.assertEqual(
            patch["core:tower"]["town"]["buildings"]["special3"]["requires"],
            ["allOf", ["dwellingLvl5"], ["mageGuild4"]],
        )
        buildings = patch["core:tower"]["town"]["buildings"]
        self.assertIsNone(buildings["dwellingUpLvl4"]["requires"])
        self.assertEqual(buildings["dwellingUpLvl5"]["requires"], ["special3"])

    def test_generated_module_mounts_tower_and_spell_patches(self):
        manifest = load("Mods/new-horizons/mod.json")
        self.assertEqual(manifest["version"], "0.8.0")
        self.assertIn("config/creatures/tower.json", manifest["creatures"])
        self.assertIn("config/factions/towerCreatureRanks.json", manifest["factions"])
        self.assertIn("config/spells/iceBolt.json", manifest["spells"])


if __name__ == "__main__":
    unittest.main()
