#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Puppet Master registration contract, not gameplay or graphical acceptance."""
import json
from pathlib import Path
import unittest

from tools.tests.test_new_horizons_content import parse_jsonc

ROOT = Path(__file__).resolve().parents[2]


class PuppetMasterDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spells = parse_jsonc((ROOT / "Mods/new-horizons/Content/config/spells/newHorizons.json").read_text())
        cls.rules = json.loads((ROOT / "config/newHorizonsMagic.json").read_text())

    def test_single_target_chaos_spell_has_fixed_cost_at_every_rank(self):
        spell = self.spells["puppetMaster"]
        self.assertEqual(spell["school"], {"new-horizons:chaos": True})
        self.assertEqual(spell["level"], 4)
        self.assertEqual(spell["targetType"], "CREATURE")
        self.assertEqual(spell["levels"]["base"]["cost"], 16)
        self.assertEqual(spell["levels"]["base"]["range"], "0")
        self.assertTrue(spell["levels"]["base"]["targetModifier"]["smart"])
        self.assertEqual(spell["levels"]["base"]["battleEffects"]["control"]["type"],
                         "newHorizonsPuppetMaster")
        for rank in ("none", "basic", "advanced", "expert"):
            self.assertEqual(spell["levels"][rank], {})
        self.assertEqual(self.rules["spells"]["new-horizons:puppetMaster"], {
            "schools": ["new-horizons:chaos"], "level": 4, "costs": [16] * 4,
        })

    def test_lucidity_is_internal_and_only_explicit_control_spells_are_tagged(self):
        self.assertTrue(self.spells["lucidity"]["flags"]["special"])
        self.assertNotIn("new-horizons:lucidity", self.rules["spells"])
        for key in ("puppetMaster", "core:berserk"):
            self.assertEqual(self.spells[key]["targetCondition"]["noneOf"]["bonus.LUCIDITY"],
                             "absolute")
        for key in ("blink", "shieldOfChaos", "handOfFate"):
            self.assertNotIn("bonus.LUCIDITY", json.dumps(self.spells[key]))

    def test_compatibility_berserk_patch_does_not_replace_its_effect_or_ownership(self):
        self.assertEqual(set(self.spells["core:berserk"]), {"targetCondition"})
        self.assertEqual(self.spells["core:berserk"]["targetCondition"], {
            "noneOf": {"bonus.LUCIDITY": "absolute"},
        })

    def test_hover_acquires_controller_caster_after_mutating_action_selection(self):
        # Lifetime/source contract only; actual popup-close acceptance is graphical.
        source = (ROOT / "client/battle/BattleStacksController.cpp").read_text()
        body = source.split("BattleStacksController::selectHoveredStacks()", 1)[1]
        body = body.split("BattleStacksController::getHoveredStacksUnitIds()", 1)[0]
        acquire = body.index("getCurrentSpellcaster()")
        construct = body.index("spells::BattleCast event(", acquire)
        for selector in ("currentActionSpellcasting(", "creatureSpellcastingModeActive()",
                         "getCurrentSpell(", "getCurrentCastMode()"):
            self.assertLess(body.index(selector), acquire)
            self.assertNotIn(selector, body[acquire:construct])
        self.assertEqual(body.count("getCurrentSpellcaster()"), 1)
        self.assertIn("if(caster)", body[acquire:construct])
        self.assertIn("battleGetStackByPos(hoveredHex, true)", body[construct:])


if __name__ == "__main__":
    unittest.main()
