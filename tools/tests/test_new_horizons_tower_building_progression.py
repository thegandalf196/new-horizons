#!/usr/bin/env python3
"""Regression checks for New Horizons Tower Mage/Genie building progression."""

import copy
import json
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
STRING = r'"(?:\\.|[^"\\])*"'


def parse_jsonc(text):
    """Parse repository JSON-with-comments/trailing-comma content."""
    text = re.sub(STRING + r'|//[^\n]*|/\*[\s\S]*?\*/',
                  lambda match: match[0] if match[0].startswith('"') else '', text)
    text = re.sub(STRING + r'|,(?=\s*[}\]])',
                  lambda match: match[0] if match[0].startswith('"') else '', text)
    return json.loads(text)


def load(relative):
    return parse_jsonc((ROOT / relative).read_text(encoding="utf-8"))


def merge_objects(base, patch):
    """Apply recursive-object, replace-array, and #override patch semantics."""
    for patch_key, value in patch.items():
        override = patch_key.endswith("#override")
        key = patch_key.removesuffix("#override") if override else patch_key
        if override or not (isinstance(value, dict) and isinstance(base.get(key), dict)):
            base[key] = copy.deepcopy(value)
        else:
            merge_objects(base[key], value)


class NewHorizonsTowerBuildingProgressionTest(unittest.TestCase):
    def test_mage_library_and_genie_follow_ranked_hall_progression(self):
        mod_factions = load("Mods/new-horizons/mod.json")["factions"]
        tower_patch_order = [
            path for path in mod_factions
            if path in {
                "config/factions/uniqueBuildings.json",
                "config/factions/universalMageGuilds.json",
                "config/factions/towerCreatureRanks.json",
            }
        ]
        self.assertEqual(tower_patch_order, [
            "config/factions/uniqueBuildings.json",
            "config/factions/universalMageGuilds.json",
            "config/factions/towerCreatureRanks.json",
        ])

        town = copy.deepcopy(load("config/factions/tower.json")["tower"]["town"])
        creatures = copy.deepcopy(town["creatures"])
        for path in tower_patch_order:
            patch = load(f"Mods/new-horizons/Content/{path}")
            tower_patch = copy.deepcopy(patch.get("core:tower", {}).get("town", {}))
            creature_changes = tower_patch.pop("creatures", {})
            for key, value in creature_changes.items():
                index = int(key.removeprefix("modify@")) - 1
                creatures[index] = value
            merge_objects(town, tower_patch)
        town["creatures"] = creatures

        self.assertEqual(town["creatures"][3], ["genie", "masterGenie"])
        self.assertEqual(town["creatures"][4], ["mage", "archMage"])

        expected_hall_slots = [
            [["villageHall", "townHall", "cityHall", "capitol"],
             ["fort", "citadel", "castle"], ["tavern"], ["blacksmith"]],
            [["marketplace", "resourceSilo"],
             ["mageGuild1", "mageGuild2", "mageGuild3", "mageGuild4", "mageGuild5"],
             ["special4"]],
            [["special1"], ["special2"], ["horde1", "horde1Upgr"]],
            [["dwellingLvl1", "dwellingUpLvl1"],
             ["dwellingLvl2", "dwellingUpLvl2"],
             ["dwellingLvl3", "dwellingUpLvl3"],
             ["dwellingLvl4", "dwellingUpLvl4"]],
            [["dwellingLvl5", "dwellingUpLvl5"],
             ["special3"],
             ["dwellingLvl6", "dwellingUpLvl6"],
             ["dwellingLvl7", "dwellingUpLvl7"]],
        ]
        self.assertEqual(town["hallSlots"], expected_hall_slots)
        self.assertEqual(town["hallSlots"][3][3], ["dwellingLvl4", "dwellingUpLvl4"])
        self.assertEqual(town["hallSlots"][4][0], ["dwellingLvl5", "dwellingUpLvl5"])
        self.assertEqual(town["hallSlots"][4][1], ["special3"], "Library immediately follows the Mage dwelling")

        base_slots = load("config/factions/tower.json")["tower"]["town"]["hallSlots"]
        flatten = lambda slots: [building for row in slots for box in row for building in box]
        self.assertCountEqual(flatten(town["hallSlots"]), flatten(base_slots))
        self.assertEqual(len(flatten(town["hallSlots"])), len(set(flatten(town["hallSlots"]))))

        # Hall selection uses the generic building ID as its icon frame. Keep
        # the remapped HALLTOWR frame, name, and source construction profile in
        # step with the creature occupying that building tier.
        building_ids = load("config/buildingsLibrary.json")
        self.assertEqual(building_ids["dwellingLvl4"]["id"], 33)
        self.assertEqual(building_ids["dwellingLvl5"]["id"], 34)
        self.assertEqual(building_ids["dwellingUpLvl4"]["id"], 40)
        self.assertEqual(building_ids["dwellingUpLvl5"]["id"], 41)
        self.assertEqual(town["buildingsIcons"], "NH_tower_buildings")

        # Original Tower BUILDING.TXT prices are assigned to their new creature
        # tiers, and #override replaces rather than recursively merges prices.
        expected_dwelling_profiles = {
            "dwellingLvl4": (
                "Altar of Wishes",
                "The Altar of Wishes recruits Genies and can be upgraded to recruit Master Genies.",
                {"wood": 5, "mercury": 5, "ore": 5, "sulfur": 5,
                 "crystal": 5, "gems": 5, "gold": 2500},
            ),
            "dwellingUpLvl4": (
                "Upgraded Altar of Wishes",
                "The Upgraded Altar of Wishes recruits Master Genies.",
                {"wood": 5, "mercury": 0, "ore": 0, "sulfur": 0,
                 "crystal": 0, "gems": 0, "gold": 2000},
            ),
            "dwellingLvl5": (
                "Mage Tower",
                "The Mage Tower recruits Mages and can be upgraded to recruit Arch Mages.",
                {"wood": 5, "mercury": 0, "ore": 5, "sulfur": 0,
                 "crystal": 6, "gems": 6, "gold": 3000},
            ),
            "dwellingUpLvl5": (
                "Upgraded Mage Tower",
                "The Upgraded Mage Tower recruits Arch Mages.",
                {"wood": 5, "mercury": 0, "ore": 0, "sulfur": 0,
                 "crystal": 0, "gems": 0, "gold": 2000},
            ),
        }
        for building, (name, description, cost) in expected_dwelling_profiles.items():
            with self.subTest(building=building):
                profile = town["buildings"][building]
                self.assertEqual(profile["name"], name)
                self.assertEqual(profile["description"], description)
                self.assertEqual(profile["cost"], cost)

        descriptor = load("Mods/new-horizons/Images/NH_tower_buildings.json")["images"]
        self.assertEqual([frame["frame"] for frame in descriptor], list(range(44)))
        source_frame = {frame: frame for frame in range(44)}
        for left, right in ((33, 34), (40, 41)):
            source_frame[left], source_frame[right] = right, left
        for image in descriptor:
            with self.subTest(frame=image["frame"]):
                self.assertEqual(image["group"], 0)
                self.assertEqual(image["defFile"], "HALLTOWR.DEF")
                self.assertEqual(image["defGroup"], 0)
                self.assertEqual(image["defFrame"], source_frame[image["frame"]])

        creature_profiles = load("Mods/new-horizons/Content/config/creatures/tower.json")
        for creature, expected_level, expected_gold in (
            ("mage", 5, 600),
            ("archMage", 5, 800),
            ("genie", 4, 450),
            ("masterGenie", 4, 550),
        ):
            with self.subTest(creature=creature):
                profile = creature_profiles[f"core:{creature}"]
                self.assertEqual(profile["level"], expected_level)
                self.assertEqual(profile["cost"], {"gold": expected_gold})

        library = town["buildings"]["special3"]
        self.assertEqual(library["requires"], ["allOf", ["dwellingLvl5"], ["mageGuild4"]])
        self.assertEqual(library["cost"], {
            "gold": 15000,
            "wood": 10,
            "ore": 10,
            "mercury": 5,
            "sulfur": 5,
            "crystal": 5,
            "gems": 5,
        })
        self.assertIsNone(town["buildings"]["dwellingUpLvl4"]["requires"])
        self.assertEqual(town["buildings"]["dwellingUpLvl5"]["requires"], ["special3"])


if __name__ == "__main__":
    unittest.main()
