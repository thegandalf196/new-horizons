#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Focused source/lifecycle guards for the presentation-only declaration.

These guards do not replace native server acceptance or graphical validation.
They deliberately run while the separately owned backend protocol is landing.
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
        cls.controller = (BATTLE / "BattleActionsController.cpp").read_text()

    def test_eligibility_is_authoritative_and_not_a_hero_action_budget(self):
        body = function(self.interface, "bool BattleInterface::canArmPerfectMoment()")
        for required in ("getActiveStack()", "!curInt->isAutoFightOn", "!isInTacticsMode()",
                         "heroSpellcastingModeActive()", "creatureSpellcastingModeActive()",
                         "battleCanUsePerfectMoment(active)"):
            self.assertIn(required, body)
        self.assertNotIn("getHeroCommandUsed", body)
        self.assertNotIn("battleCastSpells", body)
        self.assertNotIn("canArmPerfectMoment() const", self.header)

    def test_local_arming_is_bound_to_one_stack(self):
        self.assertIn("std::optional<uint32_t> perfectMomentStack", self.header)
        arm = function(self.interface, "void BattleInterface::setPerfectMomentArmed")
        self.assertLess(arm.index("clearPerfectMoment()"), arm.index("if(armed && canArmPerfectMoment())"))
        self.assertIn("getActiveStack()->unitId()", arm)
        self.assertNotIn("battleMake", arm)
        query = function(self.interface, "bool BattleInterface::isPerfectMomentArmed()")
        self.assertIn("perfectMomentStack == active->unitId()", query)
        self.assertIn("canArmPerfectMoment()", query)

    def test_only_next_player_melee_or_shot_can_carry_declaration(self):
        body = function(self.interface, "void BattleInterface::sendCommand")
        declaration = body[body.index("command.perfectMoment ="):body.index("clearPerfectMoment();")]
        for required in ("actor &&", "perfectMomentStack == actor->unitId()", "isPerfectMomentArmed()",
                         "EActionType::WALK_AND_ATTACK", "EActionType::SHOOT"):
            self.assertIn(required, declaration)
        for excluded in ("HERO_SPELL", "HERO_COMMAND", "MONSTER_SPELL", "CATAPULT"):
            self.assertNotIn(excluded, declaration)
        for dispatch in ("battleMakeUnitAction", "battleMakeTacticAction"):
            self.assertLess(body.index("clearPerfectMoment();"), body.index(dispatch))

    def test_cancellation_stack_changes_and_teardown_clear(self):
        for signature in ("BattleInterface::~BattleInterface()", "void BattleInterface::stackActivated",
                          "void BattleInterface::stackRemoved", "void BattleInterface::activateStack"):
            self.assertIn("clearPerfectMoment()", function(self.interface, signature))
        for signature in ("void BattleActionsController::endCastingSpell",
                          "void BattleActionsController::onHexRightClicked"):
            self.assertIn("owner.clearPerfectMoment()", function(self.controller, signature))
        for signature in ("void BattleHeroActionWindow::cancelSelection",
                          "void BattleHeroActionWindow::chooseCommand",
                          "void BattleHeroActionWindow::chooseTargetedCommand",
                          "void BattleHeroActionWindow::chooseSpell"):
            self.assertIn("clearPerfectMoment()", function(self.panel, signature))
        self.assertEqual(self.panel.count("[this] { cancelSelection(); }, EShortcut::GLOBAL_CANCEL"), 2)

    def test_toggle_only_arms_locally_and_shows_no_action_cost(self):
        body = function(self.panel, "void BattleHeroActionWindow::createPerfectMomentControl")
        for required in ("Perfect Moment — next attack", "No Hero Action is spent",
                         "canArmPerfectMoment()", "setPerfectMomentArmed(selected)", "close();"):
            self.assertIn(required, body)
        self.assertNotIn("battleMake", body)
        self.assertNotIn("sendCommand", body)
        self.assertIn("Perfect Moment armed: next attack. Esc/right-click cancels.", self.controller)

    def test_unavailable_toggle_is_hidden_not_just_blocked(self):
        body = function(self.panel, "void BattleHeroActionWindow::refresh()")
        self.assertIn("owner && owner->canArmPerfectMoment()", body)
        self.assertIn("perfectMomentToggle->CIntObject::setEnabled(perfectMomentAvailable)", body)
        self.assertIn("perfectMomentLabel->setEnabled(perfectMomentAvailable)", body)
        self.assertIn("perfectMomentToggle->block(!perfectMomentAvailable)", body)
        self.assertIn("orderInstructions->setEnabled(!perfectMomentAvailable)", body)

    def test_footer_reuses_existing_slot_without_card_or_cancel_overlap(self):
        self.assertIn("Point(16, 414)", self.panel)
        self.assertIn("Rect(16, 378, 516, 30)", self.panel)
        self.assertIn("Point(548, 443)", self.panel)
        self.assertIn("Rect(48, 414, 480, 32)", self.panel)
        self.assertGreaterEqual(414, 378 + 30)
        self.assertLess(414 + 32, 463)  # checkbox stays above the footer caption
        self.assertLess(48 + 480, 548)  # label lane stays left of Cancel


if __name__ == "__main__":
    unittest.main()
