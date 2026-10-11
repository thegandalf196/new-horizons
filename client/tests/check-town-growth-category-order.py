#!/usr/bin/env python3
"""Source/data contract for category-grouped town growth boxes.

This is a presentation-order/source guard, not C++ execution or rendered proof.
The independent ordering examples check original dwelling-index permutations;
the source assertions bind that contract to the actual widget construction.
"""
from pathlib import Path
import json
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "client/windows/CCastleInterface.cpp").read_text()
BODY = SOURCE.split("void CCastleInterface::recreateIcons()", 1)[1].split(
    "void CCastleInterface::keyPressed", 1
)[0]


def display_order(ranks, complete):
    levels = list(range(8))
    if complete:
        keys = list(ranks) + [3] * (8 - len(ranks))
        levels.sort(key=lambda level: keys[level])
    return levels


class TownGrowthCategoryOrderTest(unittest.TestCase):
    def test_conflux_wisp_is_second_core_original_slot_five(self):
        data = json.loads((ROOT / "Mods/new-horizons/Content/config/factions/confluxCreatureRanks.json").read_text())
        # JSON merge modify@6 is one-based; the authoritative dwelling slot stays five.
        self.assertEqual(data["core:conflux"]["town"]["creatures"]["modify@6"],
                         ["new-horizons:wisp", "new-horizons:wispUpgrade"])
        self.assertEqual(display_order([0, 1, 1, 1, 1, 0, 2], True), [0, 5, 1, 2, 3, 4, 6, 7])

    def test_existing_grouped_roster_and_empty_extra_stay_in_order(self):
        self.assertEqual(display_order([0, 0, 1, 1, 1, 2, 2], True), list(range(8)))
        self.assertEqual(display_order([0, 1, 2], True), list(range(8)))

    def test_within_each_category_is_stable(self):
        self.assertEqual(display_order([2, 1, 0, 2, 0, 1, 0], True), [2, 4, 6, 1, 5, 0, 3, 7])

    def test_legacy_partial_and_empty_context_keep_original_slots(self):
        for ranks in ([0, 1, 1, 1, 1, 0, 2], [0, None, 2], []):
            self.assertEqual(display_order(ranks, False), list(range(8)))

    def test_production_only_groups_complete_saved_context(self):
        self.assertIn("if(hasCompleteCreatureCategoryContext(town))", BODY)
        self.assertIn("currentCreatureCategory(creatureAtDwellingLevel(town, static_cast<int>(level)))", BODY)
        self.assertIn("categoryRanks[level] = creatureCategoryRank(category)", BODY)
        self.assertIn("categoryRanks.fill(3)", BODY)
        self.assertIn("std::stable_sort(displayLevels.begin(), displayLevels.end()", BODY)
        self.assertIn("return categoryRanks[left] < categoryRanks[right]", BODY)

    def test_widget_uses_original_dwelling_and_unchanged_two_row_geometry(self):
        self.assertIn("CCreaInfo>(position, town, displayLevels[displayIndex]", BODY)
        self.assertIn("14 + 55 * static_cast<int>(displayIndex % 4)", BODY)
        self.assertIn("459 + 48 * static_cast<int>(displayIndex / 4)", BODY)
        self.assertNotIn("town->creatures =", BODY)
        self.assertNotIn("std::sort(town->creatures", BODY)
        self.assertNotIn("std::stable_sort(town->creatures", BODY)
        self.assertIn("level(Level)", SOURCE)
        self.assertIn("town->getGrowthInfo(level)", SOURCE)
        self.assertIn("town->creatureGrowth(level)", SOURCE)


if __name__ == "__main__":
    unittest.main()
