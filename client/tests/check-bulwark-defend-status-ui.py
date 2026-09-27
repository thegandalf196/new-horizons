#!/usr/bin/env python3
"""Source guard for the visible, visibility-safe Bulwark Defend status."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
STATUS = (ROOT / "client/battle/NewHorizonsBattleStatus.h").read_text(encoding="utf-8")
PANEL = (ROOT / "client/battle/StackInfoBasicPanel.cpp").read_text(encoding="utf-8")
PANEL_H = (ROOT / "client/battle/StackInfoBasicPanel.h").read_text(encoding="utf-8")
WINDOW = (ROOT / "client/battle/BattleWindow.cpp").read_text(encoding="utf-8")
STACKS = (ROOT / "client/battle/BattleStacksController.cpp").read_text(encoding="utf-8")
STATUS_HEADER = ROOT / "client/battle/NewHorizonsBattleStatus.h"
UNIT_STATE = (ROOT / "lib/battle/CUnitState.cpp").read_text(encoding="utf-8")
HOVER_STATE = (ROOT / "client/battle/StackInfoPanelHoverState.h").read_text(encoding="utf-8")
HOVER_STATE_TEST = (ROOT / "client/tests/StackInfoPanelHoverStateTest.cpp").read_text(encoding="utf-8")
TEST_CMAKE = (ROOT / "test/CMakeLists.txt").read_text(encoding="utf-8")


class BulwarkDefendStatusUiTest(unittest.TestCase):
    def test_values_use_the_shared_rule_helpers_and_current_perks(self):
        self.assertIn("if(rank < 1 || rank > 3)", STATUS)
        self.assertIn("newHorizonsBulwark::reductionBasisPoints(rank, heroDefense, mirebornTerrain)", STATUS)
        self.assertIn("newHorizonsBulwark::preemptivePercent(rank, bogAmbush)", STATUS)
        self.assertIn("newHorizonsBulwark::reflectionBasisPoints(rank, false, thickHide)", STATUS)
        self.assertIn("newHorizonsBulwark::reflectionBasisPoints(rank, true, thickHide)", STATUS)

    def test_status_requires_defend_and_does_not_read_hidden_hero_details(self):
        status_source = PANEL[PANEL.index("currentDefendStatus("):PANEL.index("StackInfoBasicPanel::StackInfoBasicPanel")]
        self.assertIn("if(!stack || !stack->defended())", status_source)
        self.assertIn("!newHorizonsCombatSkills::isOrdinaryCreatureAttacker(stack)", status_source)
        self.assertLess(status_source.index("result.defending = true"), status_source.index("isOrdinaryCreatureAttacker(stack)"))
        self.assertIn("battleCallback->battleGetFightingHero(ownerSide)", status_source)
        self.assertIn("if(!hero)", status_source)
        self.assertIn("newHorizonsBulwark::hasMireborn(hero)", status_source)
        self.assertIn("terrain == TerrainId::SWAMP || terrain == TerrainId::ROUGH", status_source)
        self.assertNotIn("stack->getMyHero()", status_source)

    def test_help_shows_only_applicable_current_values(self):
        tooltip = STATUS[STATUS.index("inline std::string defendStatusTooltip"):STATUS.index("inline std::string beneficiarySideName")]
        self.assertIn('"BULWARK" : "DEFEND"', PANEL)
        self.assertIn("Physical creature damage reduction:", tooltip)
        self.assertIn("Pre-emptive strike:", tooltip)
        self.assertIn("bulwark.preemptiveReady", tooltip)
        self.assertIn("if(bulwark.meleeReflectionBasisPoints > 0)", tooltip)
        self.assertIn("if(bulwark.rangedReflectionBasisPoints > 0)", tooltip)
        self.assertIn("Mireborn's +5 percentage points", tooltip)
        self.assertIn("Bog Ambush is included above", tooltip)

    def test_hovered_status_refreshes_only_when_state_changes(self):
        self.assertIn("if(current == displayedDefendStatus)", PANEL)
        self.assertIn("panel->refreshDefendStatus(stack)", WINDOW)
        self.assertIn("stackInfoUnitId && stackInfoPanelRetention.retain(msPassed, cursorOverStackInfo)", STACKS)
        self.assertIn("battleGetStackByID(*stackInfoUnitId, false)", STACKS)
        self.assertIn("refreshHoveredStackStatus(stack)", STACKS)
        self.assertIn("displayedDefendStatus.defending", PANEL)
        self.assertIn("defendStatusTooltip(displayedDefendStatus)", PANEL)

    def test_defend_duration_matches_round_reset(self):
        status = STATUS_HEADER.read_text(encoding="utf-8")
        tooltip = status[status.index("inline std::string defendStatusTooltip"):status.index("inline std::string beneficiarySideName")]
        self.assertEqual(tooltip.count("through the end of the current battle round"), 2)
        self.assertNotIn("until its next activation", tooltip)
        after_new_round = UNIT_STATE[UNIT_STATE.index("void CUnitState::afterNewRound"):]
        self.assertIn("defending = false;", after_new_round[:after_new_round.index("\n}")])

    def test_panel_hover_retains_help_until_pointer_leaves(self):
        self.assertIn("bool containsPoint(const Point & point) const", PANEL_H)
        self.assertIn("background->pos.isInside(point)", PANEL)
        self.assertIn("background2->pos.isInside(point)", PANEL)
        self.assertIn("bool BattleWindow::cursorOverStackInfoWindow() const", WINDOW)
        self.assertIn("!panel->isDisabled() && panel->containsPoint(cursor)", WINDOW)
        self.assertIn("cursorOverStackInfo ? std::vector<const CStack *>{} : selectHoveredStacks()", STACKS)
        self.assertIn("else if(owner.windowObject->hasStackInfoWindow())", STACKS)
        self.assertIn("owner.windowObject->updateStackInfoWindow(nullptr)", STACKS)
        self.assertIn("stackInfoUnitId.reset()", STACKS)
        self.assertIn("STACK_INFO_PANEL_HOVER_GRACE_MS = 2000", HOVER_STATE)
        self.assertIn("stackInfoPanelRetention.stackInspected()", STACKS)
        self.assertIn("nhStackInfoPanelHoverStateTest", TEST_CMAKE)

    def test_native_hover_sequence_crosses_empty_hexes_before_panel(self):
        self.assertIn("retention.stackInspected()", HOVER_STATE_TEST)
        self.assertEqual(HOVER_STATE_TEST.count("retention.retain(450, false)"), 3)
        self.assertIn("retention.retain(16, true)", HOVER_STATE_TEST)
        self.assertIn("retention.retain(1800, false)", HOVER_STATE_TEST)
        self.assertIn("!retention.retain(2000, false)", HOVER_STATE_TEST)


if __name__ == "__main__":
    unittest.main()
