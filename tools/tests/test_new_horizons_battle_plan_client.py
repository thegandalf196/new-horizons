# SPDX-License-Identifier: GPL-2.0-or-later
"""Opening Order interaction source guards, not rendered or runtime acceptance."""

from pathlib import Path
import unittest

from tools.tests.test_new_horizons_perfect_moment_client import function


BATTLE = Path(__file__).resolve().parents[2] / "client/battle"


class BattlePlanClientTest(unittest.TestCase):
    def test_opening_choice_reuses_the_authoritative_ordinary_order_panel(self):
        source = (BATTLE / "BattleInterface.cpp").read_text()
        body = function(source, "void BattleInterface::presentPendingHeroOrderChoice()")
        for token in ("battleHasPendingPreCombatOrder", "heroOrderTargetingModeActive()",
                      "findWindows<BattleHeroActionWindow>()", "curInt->isAutoFightOn",
                      "createAndPushWindow<BattleHeroActionWindow>"):
            self.assertIn(token, body)
        self.assertNotIn("hasActivePerk", body)
        self.assertNotIn("battleMake", body)

    def test_pending_opening_order_cannot_be_declined_and_has_no_extra_declaration_control(self):
        source = (BATTLE / "BattleHeroActionWindow.cpp").read_text()
        cancel = function(source, "void BattleHeroActionWindow::cancelSelection()")
        crisis = function(cancel, "if(crisis.choice())")
        ordinary = cancel[cancel.index(crisis) + len(crisis) + 1:]
        mandatory_guard = (
            "if(callback->battleHasPendingDoubleCommand(callback->battleGetMySide())\n"
            "\t\t\t|| callback->battleHasPendingPreCombatOrder(callback->battleGetMySide()))\n"
            "\t\t\treturn;"
        )
        self.assertIn(mandatory_guard, ordinary)
        self.assertEqual(ordinary.count("close();"), 1)
        self.assertLess(ordinary.index(mandatory_guard), ordinary.index("close();"))
        self.assertNotIn("sendCommand", ordinary)
        self.assertNotIn("battleMake", ordinary)
        refresh = function(source, "void BattleHeroActionWindow::refresh()")
        for token in ("pendingDoubleCommand || pendingPreCombatOrder || pendingCrisis",
                      "cancel->block(pendingOrder && !pendingCrisis)",
                      "Battle Plan: choose a free opening Order now"):
            self.assertIn(token, refresh)
        self.assertNotIn("Perfect Moment", source)
        self.assertNotIn("perfectMoment", refresh)

    def test_optional_crisis_decline_keeps_validated_anchor_dispatch(self):
        source = (BATTLE / "BattleHeroActionWindow.cpp").read_text()
        cancel = function(source, "void BattleHeroActionWindow::cancelSelection()")
        crisis = function(cancel, "if(crisis.choice())")
        for token in ("battleGetStackByID(crisis.returns.back().anchor)",
                      "if(!anchor) return;", "BattleAction::makeNoAction(anchor)",
                      "decline.side = crisis.chooser();", "owner->sendCommand(decline, anchor);"):
            self.assertIn(token, crisis)
        self.assertLess(crisis.index("if(!anchor) return;"), crisis.index("close();"))
        self.assertLess(crisis.index("decline.side = crisis.chooser();"), crisis.index("close();"))
        self.assertLess(crisis.index("close();"), crisis.index("owner->sendCommand(decline, anchor);"))
        self.assertTrue(crisis.rstrip().endswith("return;"))
        self.assertNotIn("battleMake", crisis)

    def test_right_click_target_cancel_returns_to_the_opening_chooser(self):
        source = (BATTLE / "BattleActionsController.cpp").read_text()
        body = function(source, "void BattleActionsController::onHexRightClicked")
        begin = body.index("if(heroOrderTargetingModeActive())")
        opening = body[begin:body.index("if(repeatedPlacementModeActive())", begin)]
        for token in ("battleHasPendingPreCombatOrder", "cancelHeroOrderTargeting();",
                      "presentPendingHeroOrderChoice();"):
            self.assertIn(token, opening)
        self.assertNotIn("battleMake", opening)

    def test_escape_target_cancel_returns_without_spending(self):
        source = (BATTLE / "BattleWindow.cpp").read_text()
        begin = source.index("addShortcut(EShortcut::GLOBAL_CANCEL")
        body = source[begin:source.index("setShortcutBlocked", begin)]
        for token in ("heroOrderTargetingModeActive()", "battleHasPendingPreCombatOrder",
                      "cancelHeroOrderTargeting();", "presentPendingHeroOrderChoice();"):
            self.assertIn(token, body)
        self.assertNotIn("battleMake", body)


if __name__ == "__main__":
    unittest.main()
