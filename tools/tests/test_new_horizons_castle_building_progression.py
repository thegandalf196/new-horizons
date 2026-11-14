#!/usr/bin/env python3
"""Offline Castle dwelling progression checks; not native/rendered acceptance."""

import copy
import importlib.util
import json
from pathlib import Path
import re
import unittest

from jsonschema import Draft4Validator


ROOT = Path(__file__).resolve().parents[2]
PATCH = "config/factions/castleCreatureRanks.json"
RESOURCES = ("wood", "mercury", "ore", "sulfur", "crystal", "gems", "gold")
STRING = r'"(?:\\.|[^"\\])*"'


def load(relative):
    text = (ROOT / relative).read_text(encoding="utf-8")
    text = re.sub(STRING + r"|//[^\n]*|/\*[\s\S]*?\*/",
                  lambda match: match[0] if match[0].startswith('"') else "", text)
    text = re.sub(STRING + r"|,(?=\s*[}\]])",
                  lambda match: match[0] if match[0].startswith('"') else "", text)
    return json.loads(text)


def merge(base, patch):
    for name, value in patch.items():
        override = name.endswith("#override")
        key = name.removesuffix("#override")
        if not override and isinstance(value, dict) and isinstance(base.get(key), dict):
            merge(base[key], value)
        else:
            base[key] = copy.deepcopy(value)


def castle_town(include_castle_patch=True):
    town = copy.deepcopy(load("config/factions/castle.json")["castle"]["town"])
    for path in load("Mods/new-horizons/mod.json")["factions"]:
        if not include_castle_patch and path == PATCH:
            continue
        patch = load("Mods/new-horizons/Content/" + path)
        merge(town, patch.get("core:castle", {}).get("town", {}))
    return town


def leaves(expression):
    if expression is None:
        return set()
    if isinstance(expression, str):
        return set() if expression in ("allOf", "anyOf", "noneOf") else {expression}
    return set().union(*(leaves(item) for item in expression))


def satisfies(expression, built):
    if not expression:
        return True
    if isinstance(expression, str):
        return expression in built
    if expression[0] == "allOf":
        return all(satisfies(item, built) for item in expression[1:])
    if expression[0] == "anyOf":
        return any(satisfies(item, built) for item in expression[1:])
    if expression[0] == "noneOf":
        return not any(satisfies(item, built) for item in expression[1:])
    return all(satisfies(item, built) for item in expression)


class NewHorizonsCastleBuildingProgressionTest(unittest.TestCase):
    def test_patch_is_registered_once_after_the_guild_hall_layout(self):
        paths = load("Mods/new-horizons/mod.json")["factions"]
        self.assertEqual(paths.count(PATCH), 1)
        self.assertGreater(paths.index(PATCH), paths.index("config/factions/universalMageGuilds.json"))
        spec = importlib.util.spec_from_file_location("curated_module", ROOT / "tools/update-new-horizons-module.py")
        generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generator)
        self.assertEqual(generator.content_patch_files()[1], paths)

    def test_hall_order_moves_both_barracks_forms_before_both_griffin_forms(self):
        before = castle_town(False)
        town = castle_town()
        expected = copy.deepcopy(before["hallSlots"])
        expected[3][2], expected[3][3] = expected[3][3], expected[3][2]
        self.assertEqual(town["hallSlots"], expected)
        self.assertEqual(town["hallSlots"][3][2], ["dwellingLvl4", "dwellingUpLvl4"])
        self.assertEqual(town["hallSlots"][3][3], ["dwellingLvl3", "dwellingUpLvl3"])
        flattened = [building for row in town["hallSlots"] for slot in row for building in slot]
        original = [building for row in before["hallSlots"] for slot in row for building in slot]
        self.assertCountEqual(flattened, original)
        self.assertEqual(len(flattened), len(set(flattened)))
        self.assertIn("mageGuild5", flattened)

    def test_original_construction_prices_are_exchanged_without_resource_leftovers(self):
        buildings = castle_town()["buildings"]
        # Original Castle BUILDING.TXT construction profiles, exchanged between
        # the two existing lines. No purchaser archive is needed by this test.
        profiles = {
            "dwellingLvl4": (0, 0, 5, 0, 0, 0, 1000),
            "dwellingUpLvl4": (0, 0, 5, 0, 0, 0, 1000),
            "dwellingLvl3": (0, 0, 5, 0, 0, 0, 2000),
            "dwellingUpLvl3": (0, 0, 5, 0, 5, 0, 2000),
        }
        patch = load("Mods/new-horizons/Content/" + PATCH)["core:castle"]["town"]["buildings"]
        for name, values in profiles.items():
            with self.subTest(building=name):
                expected = dict(zip(RESOURCES, values))
                self.assertEqual(patch[name]["cost#override"], expected)
                self.assertEqual(buildings[name]["cost"], expected)
        self.assertEqual(buildings["dwellingUpLvl4"]["cost"]["crystal"], 0)
        self.assertEqual(buildings["dwellingUpLvl3"]["cost"]["crystal"], 5)

    def test_stable_producers_art_and_griffin_growth_binding_do_not_swap_identity(self):
        before = castle_town(False)
        town = castle_town()
        for field in ("creatures", "structures", "buildingsIcons", "horde"):
            with self.subTest(field=field):
                self.assertEqual(town[field], before[field])
        self.assertEqual(town["creatures"][3], ["swordsman", "crusader"])
        self.assertEqual(town["creatures"][2], ["griffin", "royalGriffin"])
        self.assertEqual(town["horde"][0], 2)
        self.assertEqual(town["buildings"]["horde1"]["upgrades"], "dwellingLvl3")
        self.assertEqual(town["buildings"]["horde1Upgr"]["upgrades"], "dwellingUpLvl3")
        for name in ("dwellingLvl3", "dwellingLvl4", "dwellingUpLvl3", "dwellingUpLvl4"):
            self.assertNotIn("name", town["buildings"][name])
            self.assertNotIn("description", town["buildings"][name])

    def test_requirements_keep_core_barracks_before_elite_griffins_and_own_upgrades(self):
        buildings = castle_town()["buildings"]
        self.assertEqual(buildings["dwellingLvl4"]["requires"], ["dwellingLvl1"])
        self.assertEqual(buildings["dwellingLvl3"]["requires"],
                         ["allOf", ["dwellingLvl4"], ["blacksmith"]])
        core_built = {"fort", "dwellingLvl1"}
        self.assertTrue(satisfies(buildings["dwellingLvl4"]["requires"], core_built))
        self.assertFalse(satisfies(buildings["dwellingLvl3"]["requires"], core_built))
        self.assertFalse(satisfies(buildings["dwellingLvl3"]["requires"], core_built | {"dwellingLvl4"}))
        self.assertTrue(satisfies(buildings["dwellingLvl3"]["requires"],
                                  core_built | {"dwellingLvl4", "blacksmith"}))
        self.assertEqual(buildings["dwellingLvl5"]["requires"],
                         ["allOf", ["mageGuild1"], ["dwellingLvl4"], ["blacksmith"]])
        self.assertEqual(buildings["special2"]["requires"],
                         ["allOf", ["dwellingLvl4"], ["blacksmith"]])
        for number in (3, 4):
            base, upgraded = f"dwellingLvl{number}", f"dwellingUpLvl{number}"
            self.assertEqual(buildings[upgraded]["upgrades"], base)
            self.assertEqual(buildings[upgraded]["requires"], [base])
            self.assertFalse(satisfies(buildings[upgraded]["requires"], set()))
            self.assertTrue(satisfies(buildings[upgraded]["requires"], {base}))

    def test_requirement_graph_has_no_missing_references_or_cycles(self):
        buildings = copy.deepcopy(load("config/buildingsLibrary.json"))
        for name, row in castle_town()["buildings"].items():
            if row is not None:
                merge(buildings.setdefault(name, {}), row)
        graph = {}
        for name, row in buildings.items():
            dependencies = leaves(row.get("requires"))
            if row.get("upgrades"):
                dependencies.add(row["upgrades"])
            self.assertNotIn(name, dependencies)
            self.assertLessEqual(dependencies, buildings.keys())
            graph[name] = dependencies

        def visit(name, active, finished):
            self.assertNotIn(name, active, "construction requirement cycle")
            if name not in finished:
                for child in graph[name]:
                    visit(child, active | {name}, finished)
                finished.add(name)

        finished = set()
        for name in graph:
            visit(name, set(), finished)

        def closure(name, nodes):
            row = nodes[name]
            direct = leaves(row.get("requires"))
            if row.get("upgrades"):
                direct.add(row["upgrades"])
            return direct | set().union(*(closure(child, nodes) for child in direct))

        old_buildings = copy.deepcopy(load("config/buildingsLibrary.json"))
        for name, row in castle_town(False)["buildings"].items():
            if row is not None:
                merge(old_buildings.setdefault(name, {}), row)
        for name in buildings:
            with self.subTest(building=name):
                if name not in ("dwellingLvl4", "dwellingUpLvl4"):
                    self.assertEqual(closure(name, buildings), closure(name, old_buildings))
                else:
                    self.assertEqual(closure(name, buildings), closure(name, old_buildings) - {"blacksmith"})

    def test_complete_building_profiles_validate_against_existing_schema(self):
        library = load("config/buildingsLibrary.json")
        town = castle_town()
        validator = Draft4Validator(load("config/schemas/townBuilding.json"))
        for name in ("dwellingLvl3", "dwellingLvl4", "dwellingUpLvl3", "dwellingUpLvl4", "dwellingLvl5", "special2"):
            with self.subTest(building=name):
                profile = copy.deepcopy(library[name])
                merge(profile, town["buildings"][name])
                # Native JsonValidator accepts null optional fields (typeCheck).
                # Match that absence convention for the strict Draft4 checker.
                profile = {key: value for key, value in profile.items() if value is not None}
                validator.validate(profile)


if __name__ == "__main__":
    unittest.main()
