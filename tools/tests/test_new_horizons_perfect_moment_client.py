#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Source guards that Perfect Moment is automatic, not a player declaration.

These guards cover UI wiring only; native acceptance and rendered UI validation
remain separate responsibilities.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
BATTLE = ROOT / "client/battle"


def function(source, signature):
    start = source.index("{", source.index(signature))
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start + 1:end - 1]


class PerfectMomentClientTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.interface = (BATTLE / "BattleInterface.cpp").read_text()
        cls.header = (BATTLE / "BattleInterface.h").read_text()
        cls.panel = (BATTLE / "BattleHeroActionWindow.cpp").read_text()
        cls.panelHeader = (BATTLE / "BattleHeroActionWindow.h").read_text()
        cls.controller = (BATTLE / "BattleActionsController.cpp").read_text()

    def test_client_has_no_manual_perfect_moment_state_or_control(self):
        for source in (self.interface, self.header, self.panel, self.panelHeader, self.controller):
            self.assertNotIn("perfectmoment", source.lower())
            self.assertNotIn("perfect moment", source.lower())

    def test_unit_actions_use_the_existing_authoritative_dispatch(self):
        body = function(self.interface, "void BattleInterface::sendCommand")
        self.assertNotIn("perfectMoment", body)
        self.assertNotIn("perfect moment", body.lower())
        self.assertIn("battleMakeUnitAction", body)
        self.assertIn("battleMakeTacticAction", body)
        self.assertIn("command.stackNumber", body)

    def test_action_panels_keep_their_existing_controls_and_order_instructions(self):
        self.assertEqual(self.panel.count("[this] { cancelSelection(); }, EShortcut::GLOBAL_CANCEL"), 2)
        self.assertIn('"Cancel", "Return to battle without spending an action."', self.panel)
        self.assertIn("Targeted Orders select stacks directly on the battlefield", self.panel)
        self.assertIn("Protect uses two clicks: Protector, then adjacent Ward.", self.panel)
        self.assertIn("cancel->block(pendingOrder)", function(self.panel, "void BattleHeroActionWindow::refresh()"))


if __name__ == "__main__":
    unittest.main()
