# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded UI source guards; not a substitute for native or rendered acceptance."""

from pathlib import Path
import unittest

from tools.tests.test_new_horizons_perfect_moment_client import function


BATTLE = Path(__file__).resolve().parents[2] / "client/battle"


class DoubleCommandClientTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.interface = (BATTLE / "BattleInterface.cpp").read_text()
        cls.panel = (BATTLE / "BattleHeroActionWindow.cpp").read_text()
        cls.controller = (BATTLE / "BattleActionsController.cpp").read_text()
        cls.window = (BATTLE / "BattleWindow.cpp").read_text()

    def test_automatic_chooser_uses_authoritative_state_and_existing_panel(self):
        body = function(self.interface, "void BattleInterface::presentPendingHeroOrderChoice()")
        for token in ("battleHasPendingDoubleCommand", "heroOrderTargetingModeActive()",
                      "findWindows<BattleHeroActionWindow>()", "curInt->isAutoFightOn",
                      "createAndPushWindow<BattleHeroActionWindow>"):
            self.assertIn(token, body)
        self.assertNotIn("hasActivePerk", body)
        self.assertNotIn("battleMake", body)
        self.assertIn("presentPendingHeroOrderChoice();",
                      function(self.interface, "void BattleInterface::activateStack()"))

    def test_cancel_cannot_decline_the_pending_opportunity(self):
        body = function(self.panel, "void BattleHeroActionWindow::cancelSelection()")
        self.assertLess(body.index("battleHasPendingDoubleCommand"), body.index("close();"))
        self.assertIn("return;", body[body.index("battleHasPendingDoubleCommand"):body.index("close();")])
        refresh = function(self.panel, "void BattleHeroActionWindow::refresh()")
        self.assertIn("cancel->block(pendingOrder)", refresh)
        self.assertIn("Double Command: choose a different Order now", refresh)

    def test_right_click_target_cancel_returns_to_ordinary_chooser(self):
        body = function(self.controller, "void BattleActionsController::onHexRightClicked")
        begin = body.index("if(heroOrderTargetingModeActive())")
        continuation = body[begin:body.index("if(repeatedPlacementModeActive())", begin)]
        self.assertLess(continuation.index("cancelHeroOrderTargeting();"),
                        continuation.index("presentPendingHeroOrderChoice();"))
        self.assertIn("battleHasPendingDoubleCommand", continuation)
        self.assertNotIn("battleMake", continuation)

    def test_escape_target_cancel_returns_without_spending(self):
        begin = self.window.index("addShortcut(EShortcut::GLOBAL_CANCEL")
        body = self.window[begin:self.window.index("setShortcutBlocked", begin)]
        for token in ("heroOrderTargetingModeActive()", "battleHasPendingDoubleCommand",
                      "cancelHeroOrderTargeting();", "presentPendingHeroOrderChoice();"):
            self.assertIn(token, body)
        self.assertNotIn("battleMake", body)


if __name__ == "__main__":
    unittest.main()
