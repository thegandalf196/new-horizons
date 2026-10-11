#!/usr/bin/env python3
"""Focused config checks for both ranged Cabir creature definitions."""

import json
import hashlib
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
CORE_TOWER = ROOT / "config/creatures/tower.json"
CORE_DUNGEON = ROOT / "config/creatures/dungeon.json"
CORE_CONFLUX = ROOT / "config/creatures/conflux.json"
MODULE_TOWER = ROOT / "Mods/new-horizons/Content/config/creatures/tower.json"
CABIR_REPAIR = ROOT / "Mods/new-horizons/Content/config/spells/cabirRepair.json"
ART_PACK = ROOT / "Mods/new-horizons/NewHorizons.nhart"
ART_MANIFEST = ROOT / "assets/new-horizons/runtime-art-manifest.json"

sys.path.insert(0, str(ROOT / "tools"))
import nhart


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
        verified = nhart.verify(ART_PACK, ART_MANIFEST)
        cls.packed_resources = {row["resource"]: row for row in verified["entries"]}
        cls.selected_resources = {row["resource"]: row for row in verified["manifest"]["entries"]}

    def packed_payload(self, resource):
        self.assertIn(resource, self.selected_resources)
        row = self.packed_resources[resource]
        selected = self.selected_resources[resource]
        with ART_PACK.open("rb") as stream:
            stream.seek(row["offset"])
            payload = stream.read(row["size"])
        self.assertEqual(len(payload), selected["size"])
        self.assertGreater(len(payload), 0)
        self.assertEqual(hashlib.sha256(payload).hexdigest(), selected["sha256"])
        self.assertEqual(row["sha256"], selected["sha256"])
        return payload

    def assert_packed_frames(self, descriptor, frames):
        self.assertTrue(frames)
        for frame in frames:
            payload = self.packed_payload("SPRITES/" + descriptor["basepath"] + frame)
            self.assertTrue(payload.startswith(b"\x89PNG\r\n\x1a\n"))
            self.assertGreater(len(payload), 8)

    def test_both_cabir_forms_have_normal_shooter_ammunition_and_missile(self):
        expected_missile = self.creatures["core:masterGremlin"]["graphics"]["missile"]
        self.assertEqual(expected_missile["projectile"], "NH_CabirHandoffFireball.def")
        self.assertEqual(expected_missile["attackClimaxFrame"], 4)
        self.assertEqual(expected_missile["frameAngles"], [90, 72, 45, 27, 0, -27, -45, -72, -90])
        self.assertEqual(
            expected_missile["offset"],
            {
                "upperX": 26,
                "upperY": -48,
                "middleX": 33,
                "middleY": -42,
                "lowerX": 33,
                "lowerY": -28,
            },
        )

        for creature_id, creature in self.creatures.items():
            with self.subTest(creature=creature_id):
                self.assertEqual(creature["shots"], 8)
                self.assertEqual(creature["abilities"]["shooter"]["type"], "SHOOTER")
                self.assertEqual(creature["graphics"]["missile"], expected_missile)
                self.assertNotIn("noMeleePenalty", creature["abilities"])
                projectile = json.loads(self.packed_payload(
                    "SPRITES/" + Path(creature["graphics"]["missile"]["projectile"]).with_suffix(".json").name))
                self.assertEqual([(image["group"], image["frame"]) for image in projectile["images"]],
                                 [(0, frame) for frame in range(len(expected_missile["frameAngles"]))])
                self.assert_packed_frames(projectile, [image["file"] for image in projectile["images"]])
                battle = json.loads(self.packed_payload(
                    "SPRITES/" + Path(creature["graphics"]["animation"]).with_suffix(".json").name))
                groups = {sequence["group"]: sequence["frames"] for sequence in battle["sequences"]}
                # BattleConstants.h: SHOOT_UP/FRONT/DOWN. Climax must resolve
                # an actual selected frame for each directional shooting pose.
                for group in (14, 15, 16):
                    self.assertGreater(len(groups[group]), expected_missile["attackClimaxFrame"])
                    self.assert_packed_frames(battle, groups[group])

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
