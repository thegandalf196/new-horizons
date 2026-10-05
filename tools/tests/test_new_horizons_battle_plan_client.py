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
        self.assertLess(cancel.index("battleHasPendingPreCombatOrder"), cancel.index("close();"))
        refresh = function(source, "void BattleHeroActionWindow::refresh()")
        for token in ("pendingDoubleCommand || pendingPreCombatOrder",
                      "cancel->block(pendingOrder)",
                      "Battle Plan: choose a free opening Order now"):
            self.assertIn(token, refresh)
        self.assertNotIn("Perfect Moment", source)
        self.assertNotIn("perfectMoment", refresh)

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
