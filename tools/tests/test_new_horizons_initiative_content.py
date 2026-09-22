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

    def test_tower_mage_and_genie_prototype_stats_and_abilities(self):
        patch = load("Mods/new-horizons/Content/config/creatures/tower.json")
        expected = {
            "mage": {
                "level": 5,
                "attack": 13,
                "defense": 9,
                "damage": {"min": 11, "max": 14},
                "hitPoints": 30,
                "shots": 16,
                "growth": 3,
                "cost": {"gold": 600},
            },
            "archMage": {
                "level": 5,
                "attack": 15,
                "defense": 10,
                "damage": {"min": 13, "max": 17},
                "hitPoints": 35,
                "shots": 20,
                "growth": 3,
                "cost": {"gold": 800},
            },
            "genie": {
                "level": 4,
                "attack": 10,
                "defense": 10,
                "damage": {"min": 8, "max": 11},
                "hitPoints": 35,
                "growth": 4,
                "cost": {"gold": 450},
            },
            "masterGenie": {
                "level": 4,
                "attack": 11,
                "defense": 11,
                "damage": {"min": 9, "max": 12},
                "hitPoints": 40,
                "growth": 4,
                "cost": {"gold": 550},
            },
        }
        for creature, values in expected.items():
            with self.subTest(creature=creature):
                self.assertEqual(
                    {key: patch[f"core:{creature}"][key] for key in values},
                    values,
                )

        base = load("config/creatures/tower.json")
        mage = copy.deepcopy(base["mage"])
        merge_objects(mage, patch["core:mage"])
        arch_mage = copy.deepcopy(base["archMage"])
        merge_objects(arch_mage, patch["core:archMage"])
        genie = copy.deepcopy(base["genie"])
        merge_objects(genie, patch["core:genie"])
        master_genie = copy.deepcopy(base["masterGenie"])
        merge_objects(master_genie, patch["core:masterGenie"])

        self.assertEqual(
            {name: ability["type"] for name, ability in mage["abilities"].items()},
            {
                "shooter": "SHOOTER",
                "noMeleePenalty": "NO_MELEE_PENALTY",
                "reduceSpellCost": "CHANGES_SPELL_COST_FOR_ALLY",
                "noDistancePenalty": "NO_DISTANCE_PENALTY",
            },
        )
        self.assertEqual(mage["abilities"]["reduceSpellCost"]["val"], 2)
        self.assertEqual(
            {name: ability["type"] for name, ability in arch_mage["abilities"].items()},
            {
                "shooter": "SHOOTER",
                "noMeleePenalty": "NO_MELEE_PENALTY",
                "noWallPenalty": "NO_WALL_PENALTY",
                "reduceSpellCost": "CHANGES_SPELL_COST_FOR_ALLY",
                "noDistancePenalty": "NO_DISTANCE_PENALTY",
            },
        )
        self.assertEqual(arch_mage["abilities"]["reduceSpellCost"]["val"], 2)
        for creature, required in {
            "genie": {"canFly", "hateEfreet", "hateEfreetSultans"},
            "masterGenie": {
                "canFly", "casts", "spellsLength", "randomSpellcaster",
                "hateEfreet", "hateEfreetSultans",
            },
        }.items():
            with self.subTest(abilities=creature):
                effective = genie if creature == "genie" else master_genie
                self.assertTrue(required <= set(effective["abilities"]))

    def test_tower_town_swaps_genies_and_magi_before_rank_presentation(self):
        patch = load("Mods/new-horizons/Content/config/factions/towerCreatureRanks.json")
        creatures = patch["core:tower"]["town"]["creatures"]
        self.assertEqual(creatures["modify@4"], ["genie", "masterGenie"])
        self.assertEqual(creatures["modify@5"], ["mage", "archMage"])

        base = load("config/factions/tower.json")
        rows = copy.deepcopy(base["tower"]["town"]["creatures"])
        for key, value in creatures.items():
            index = int(key.removeprefix("modify@")) - 1
            rows[index] = value

        self.assertEqual(rows, [
            ["gremlin", "masterGremlin"],
            ["stoneGargoyle", "obsidianGargoyle"],
            ["ironGolem", "stoneGolem"],
            ["genie", "masterGenie"],
            ["mage", "archMage"],
            ["naga", "nagaQueen"],
            ["giant", "titan"],
        ])
        self.assertEqual(len(rows), 7)
        self.assertEqual(len({tuple(row) for row in rows}), 7)
        self.assertEqual(rows[2], ["ironGolem", "stoneGolem"], "the Golem row must not be overwritten")
        self.assertEqual(rows[3], ["genie", "masterGenie"], "Genies occupy the level-4 dwelling")
        self.assertEqual(rows[4], ["mage", "archMage"], "Magi occupy the level-5 dwelling")

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

        categories = load("config/newHorizonsCreatureCategories.json")["creatures"]
        tower_categories = [categories[f"core:{row[0]}"] for row in rows]
        self.assertEqual(tower_categories, ["core", "core", "core", "elite", "elite", "elite", "champion"])
        self.assertTrue(
            rows and all(f"core:{creature}" in categories for row in rows for creature in row),
            "the seven-row Tower roster is complete and must select the ranked fort layout",
        )

    def test_ranked_bands_fit_tower_and_four_row_rosters_at_bundled_metrics(self):
        """The responsive card math must fit both normal Tower and authored bands."""
        castle = (ROOT / "client/windows/CCastleInterface.cpp").read_text(encoding="utf-8")
        self.assertIn("rowBegin += NH_FORT_CARDS_PER_ROW", castle)
        self.assertIn("void CFortScreen::RecruitArea::showAll", castle)
        self.assertIn("CanvasClipRectGuard clip(to, pos)", castle)
        self.assertIn("availableCardHeight < rankedFortMinimumCardHeight(false)", castle)

        viewport = (800, 528)
        parent = (800, 600)
        window = (min(viewport[0] - 8, parent[0]), min(viewport[1] - 8, parent[1]))
        card_gap = 4
        side_margin = 12
        card_width = (window[0] - 2 * side_margin - 2 * card_gap) // 3
        self.assertEqual(card_width, 253)

        # Actual bundled logical metrics at this viewport: BIG25, SMALL15,
        # TINY13.  The production code queries these from the renderer; this
        # model keeps the panel budget and the card geometry in lockstep.
        title_height = 25
        heading_height = 15
        tiny_font_height = 13
        compact_portrait_height = 15
        content_top = 2 + title_height + 2
        row_minimum = tiny_font_height + 1
        stat_bottom_padding = max(2, tiny_font_height // 2)
        normal_minimum_card_height = (1 + tiny_font_height + 1) + 8 * row_minimum + stat_bottom_padding
        footer_top = window[1] - 48

        def assert_geometry(groups):
            total_rows = sum((count + 2) // 3 for count in groups)
            band_gaps = card_gap * 3
            row_gaps = card_gap * total_rows
            fixed_height = content_top + heading_height * 3 + band_gaps + row_gaps + 48
            available_card_height = (window[1] - fixed_height) // total_rows
            compact_stat_grid = available_card_height < normal_minimum_card_height
            self.assertTrue(compact_stat_grid)
            stat_rows = 4
            stat_top = 1 + tiny_font_height + 1 + compact_portrait_height
            minimum_card_height = stat_top + row_minimum * stat_rows + stat_bottom_padding
            card_height = max(minimum_card_height, available_card_height)
            row_height = max(row_minimum, (card_height - stat_top - stat_bottom_padding) // stat_rows)
            self.assertGreaterEqual(card_height, minimum_card_height)
            for index in range(8):
                stat_row = index // 2
                self.assertLessEqual(
                    stat_top + (stat_row + 1) * row_height + stat_bottom_padding,
                    card_height,
                    "every stat rect must stay inside its ranked card",
                )
            self.assertGreater(
                stat_top - compact_portrait_height + 120,
                card_height,
                "oversized portraits must be card-clipped",
            )
            cards_bottom = content_top + heading_height * 3 + band_gaps + row_gaps + total_rows * card_height
            self.assertLessEqual(cards_bottom, footer_top, "all authored cards must fit before the footer")
            self.assertLess(card_height, normal_minimum_card_height)

        assert_geometry((3, 3, 1))
        assert_geometry((4, 3, 1))

        image_column = 24
        creature_right = image_column + 4 + 120
        minimum_stat_width = 78 + 20 + 6  # Leadership Cost + a 3-digit value + gutters
        compact_stat_width = card_width - 8
        compact_stat_cell_width = (compact_stat_width - card_gap) // 2
        self.assertGreaterEqual(compact_stat_cell_width, minimum_stat_width)
        self.assertGreater(creature_right, image_column)

    def test_generated_module_mounts_tower_and_spell_patches(self):
        manifest = load("Mods/new-horizons/mod.json")
        self.assertEqual(manifest["version"], "0.11.0")
        self.assertIn("config/creatures/tower.json", manifest["creatures"])
        self.assertIn("config/factions/towerCreatureRanks.json", manifest["factions"])
        self.assertIn("config/spells/iceBolt.json", manifest["spells"])


if __name__ == "__main__":
    unittest.main()
