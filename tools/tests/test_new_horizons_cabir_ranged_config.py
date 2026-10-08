#!/usr/bin/env python3
"""Focused config checks for both ranged Cabir creature definitions."""

import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CORE_TOWER = ROOT / "config/creatures/tower.json"
CORE_DUNGEON = ROOT / "config/creatures/dungeon.json"
CORE_CONFLUX = ROOT / "config/creatures/conflux.json"
MODULE_TOWER = ROOT / "Mods/new-horizons/Content/config/creatures/tower.json"
CABIR_REPAIR = ROOT / "Mods/new-horizons/Content/config/spells/cabirRepair.json"


def _strip_jsonc_comments(source: str) -> str:
    output = []
    in_string = False
    escaped = False
    index = 0
    while index < len(source):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if in_string:
            output.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            index += 1
        elif char == '"':
            in_string = True
            output.append(char)
            index += 1
        elif char == "/" and following == "/":
            while index < len(source) and source[index] not in "\r\n":
                index += 1
        elif char == "/" and following == "*":
            index += 2
            while index + 1 < len(source) and source[index:index + 2] != "*/":
                index += 1
            index = min(len(source), index + 2)
        else:
            output.append(char)
            index += 1
    return "".join(output)


def _deep_merge(base, patch):
    merged = dict(base)
    for key, value in patch.items():
        if isinstance(value, dict) and isinstance(merged.get(key), dict):
            merged[key] = _deep_merge(merged[key], value)
        else:
            merged[key] = value
    return merged


class CabirRangedConfigTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.core = json.loads(_strip_jsonc_comments(CORE_TOWER.read_text(encoding="utf-8")))
        cls.dungeon = json.loads(_strip_jsonc_comments(CORE_DUNGEON.read_text(encoding="utf-8")))
        cls.conflux = json.loads(_strip_jsonc_comments(CORE_CONFLUX.read_text(encoding="utf-8")))
        cls.module = json.loads(_strip_jsonc_comments(MODULE_TOWER.read_text(encoding="utf-8")))
        cls.repair_spell = json.loads(CABIR_REPAIR.read_text(encoding="utf-8"))["cabirRepair"]
        cls.creatures = {
            "core:gremlin": _deep_merge(cls.core["gremlin"], cls.module["core:gremlin"]),
            "core:masterGremlin": _deep_merge(cls.core["masterGremlin"], cls.module["core:masterGremlin"]),
        }

    def test_both_cabir_forms_have_normal_shooter_ammunition_and_missile(self):
        expected_missile = self.creatures["core:masterGremlin"]["graphics"]["missile"]
        self.assertEqual(expected_missile["projectile"], "CPRGOGX.DEF")
        self.assertEqual(expected_missile["attackClimaxFrame"], 3)
        self.assertEqual(expected_missile["frameAngles"], [90, 72, 45, 27, 0, -27, -45, -72, -90])
        self.assertEqual(
            expected_missile["offset"],
            {
                "upperX": 28,
                "upperY": -45,
                "middleX": 40,
                "middleY": -34,
                "lowerX": 31,
                "lowerY": -18,
            },
        )

        for creature_id, creature in self.creatures.items():
            with self.subTest(creature=creature_id):
                self.assertEqual(creature["shots"], 8)
                self.assertEqual(creature["abilities"]["shooter"]["type"], "SHOOTER")
                self.assertEqual(creature["graphics"]["missile"], expected_missile)
                self.assertNotIn("noMeleePenalty", creature["abilities"])

    def test_cabir_elemental_defenses_and_master_repair_are_preserved(self):
        expected_resistances = {
            "cabirFireResistance": {"type": "ELEMENTAL_SPELL_DAMAGE_RECEIVED", "subtype": "spellElementFire", "val": -50},
            "cabirWaterWeakness": {"type": "ELEMENTAL_SPELL_DAMAGE_RECEIVED", "subtype": "spellElementWater", "val": 25},
        }
        for creature_id, creature in self.creatures.items():
            with self.subTest(creature=creature_id):
                abilities = creature["abilities"]
                for name, expected in expected_resistances.items():
                    self.assertEqual(abilities[name], expected)

        master_abilities = self.creatures["core:masterGremlin"]["abilities"]
        self.assertEqual(master_abilities["cabirRepair"]["subtype"], "new-horizons:cabirRepair")
        self.assertEqual(master_abilities["cabirRepairUses"]["val"], 1)
        self.assertEqual(master_abilities["cabirRepairPower"]["val"], 10)
        self.assertNotIn("cabirRepair", self.creatures["core:gremlin"]["abilities"])

    def test_original_upgrade_relationship_and_master_ammunition_are_retained(self):
        self.assertEqual(self.core["gremlin"]["upgrades"], ["masterGremlin"])
        self.assertEqual(self.core["masterGremlin"]["shots"], 8)
        self.assertEqual(self.core["masterGremlin"]["abilities"]["shooter"]["type"], "SHOOTER")

    def test_both_forms_use_gog_shot_and_claw_sounds_without_changing_other_cues(self):
        inferno = json.loads(_strip_jsonc_comments((CORE_TOWER.parent / "inferno.json").read_text(encoding="utf-8")))
        sound_sources = {
            "core:gremlin": self.dungeon["troglodyte"],
            "core:masterGremlin": self.dungeon["infernalTroglodyte"],
        }

        for creature_id, claw_source in sound_sources.items():
            with self.subTest(creature=creature_id):
                creature = self.creatures[creature_id]
                self.assertEqual(creature["sound"]["shoot"], inferno["gog"]["sound"]["shoot"])
                self.assertEqual(creature["sound"]["attack"], claw_source["sound"]["attack"])
                self.assertTrue(creature["sound"]["shoot"].endswith(".wav"))
                self.assertTrue(creature["sound"]["attack"].endswith(".wav"))
                for unchanged_sound in ("defend", "killed", "move", "wince"):
                    self.assertEqual(
                        creature["sound"][unchanged_sound],
                        self.core["gremlin" if creature_id == "core:gremlin" else "masterGremlin"]["sound"][unchanged_sound],
                    )

        self.assertEqual(self.repair_spell["sounds"]["cast"], "REGENER")


if __name__ == "__main__":
    unittest.main(verbosity=2)
