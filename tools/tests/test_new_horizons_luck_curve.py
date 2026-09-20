#!/usr/bin/env python3
"""Canonical New Horizons Luck curve and module-isolation regression."""

import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class NewHorizonsLuckCurveTest(unittest.TestCase):
    def test_symmetric_four_percent_curve_through_ten(self):
        settings = json.loads((ROOT / "config/newHorizonsCombat.json").read_text())
        combat = settings["combat"]
        expected = list(range(4, 41, 4))
        self.assertEqual(combat["goodLuckChance"], expected)
        self.assertEqual(combat["badLuckChance"], expected)
        self.assertEqual(combat["luckDiceSize"], 100)
        self.assertEqual(combat["luckBias"], 0)

    def test_generated_module_carries_curve_without_changing_core(self):
        module = json.loads((ROOT / "Mods/new-horizons/mod.json").read_text())
        combat = module["settings"]["combat"]
        self.assertEqual(combat["goodLuckChance"], list(range(4, 41, 4)))
        self.assertEqual(combat["badLuckChance"], list(range(4, 41, 4)))
        self.assertEqual(combat["luckDiceSize"], 100)
        self.assertEqual(combat["luckBias"], 0)

        core_text = (ROOT / "config/gameConfig.json").read_text()
        self.assertRegex(core_text, re.compile(r'"goodLuckChance"\s*:\s*\[\s*1,\s*2,\s*3\s*\]'))
        self.assertRegex(core_text, re.compile(r'"badLuckChance"\s*:\s*\[\s*\]'))


if __name__ == "__main__":
    unittest.main()
