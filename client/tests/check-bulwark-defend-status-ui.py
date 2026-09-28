#!/usr/bin/env python3
"""Source guard for the visible, visibility-safe Bulwark Defend status."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
STATUS = (ROOT / "client/battle/NewHorizonsBattleStatus.h").read_text(encoding="utf-8")
PANEL = (ROOT / "client/battle/StackInfoBasicPanel.cpp").read_text(encoding="utf-8")
AUTHORITATIVE_BULWARK = (ROOT / "lib/battle/NewHorizonsBulwark.cpp").read_text(encoding="utf-8")
AUTHORITATIVE_CALLBACK = (ROOT / "lib/battle/CBattleInfoCallback.cpp").read_text(encoding="utf-8")
AUTHORITATIVE_ATTACK = (ROOT / "server/battles/BattleActionProcessor.cpp").read_text(encoding="utf-8")
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
        self.assertIn("newHorizonsBulwark::reflectionBasisPoints(rank, false, thickHide, vengefulMire)", STATUS)
        self.assertIn("newHorizonsBulwark::reflectionBasisPoints(rank, true, thickHide, vengefulMire)", STATUS)

    def test_shared_cover_status_matches_the_current_adjacent_defender_rule(self):
        status_source = PANEL[PANEL.index("currentDefendStatus("):PANEL.index("StackInfoBasicPanel::StackInfoBasicPanel")]
        self.assertIn("newHorizonsBulwark::hasSharedCover(hero)", status_source)
        self.assertIn("battleCallback->battleAdjacentUnits(stack)", status_source)
        self.assertIn("adjacent->unitSide() == stackSide && adjacent->defended()", status_source)
        self.assertIn("isOrdinaryCreatureAttacker(adjacent)", status_source)
        self.assertIn("stack->bulwarkPreemptiveUsed, sharedCoverApplies", status_source)
        self.assertIn("friendUnit->unitSide() != info.defender->unitSide() || !friendUnit->defended()", AUTHORITATIVE_CALLBACK)
        self.assertIn("newHorizonsCombatSkills::isOrdinaryCreatureAttacker(friendUnit)", AUTHORITATIVE_CALLBACK)
        self.assertIn("std::min(10000,", AUTHORITATIVE_CALLBACK)
        self.assertIn("baseReduction + newHorizonsBulwark::sharedCoverBasisPoints(baseReduction)", AUTHORITATIVE_CALLBACK)
        self.assertIn("newHorizonsBulwark::sharedCoverBasisPoints(baseReduction)", STATUS)
        self.assertIn("Shared Cover adds", STATUS)
        self.assertIn("sharedCoverApplied", STATUS)

    def test_vengeful_mire_status_matches_melee_reflection_only(self):
        status_source = PANEL[PANEL.index("currentDefendStatus("):PANEL.index("StackInfoBasicPanel::StackInfoBasicPanel")]
        self.assertIn("newHorizonsBulwark::hasVengefulMire(hero)", status_source)
        self.assertIn("return ranged || !vengefulMire", AUTHORITATIVE_BULWARK)
        self.assertIn("std::min(7500, basisPoints + 25 * BASIS_POINTS_PER_PERCENT)", AUTHORITATIVE_BULWARK)
        self.assertIn("newHorizonsBulwark::hasVengefulMire(bulwarkHero)", AUTHORITATIVE_ATTACK)
        tooltip = STATUS[STATUS.index("inline std::string defendStatusTooltip"):STATUS.index("inline std::string beneficiarySideName")]
        self.assertIn("if(bulwark.vengefulMireBonusBasisPoints > 0)", tooltip)
        self.assertIn("to melee reflection only, up to 75%", tooltip)
        self.assertIn("vengefulMireBonusBasisPoints = effectiveMeleeReflection - baseMeleeReflection", STATUS)

    def test_status_requires_defend_and_does_not_read_hidden_hero_details(self):
        status_source = PANEL[PANEL.index("currentDefendStatus("):PANEL.index("StackInfoBasicPanel::StackInfoBasicPanel")]
        self.assertIn("if(!stack || !stack->defended())", status_source)
        self.assertIn("!newHorizonsCombatSkills::isOrdinaryCreatureAttacker(stack)", status_source)
        self.assertLess(status_source.index("result.defending = true"), status_source.index("isOrdinaryCreatureAttacker(stack)"))
        self.assertIn("battleCallback->battleGetFightingHero(ownerSide)", status_source)
        self.assertIn("if(!hero)", status_source)
        self.assertIn("newHorizonsBulwark::hasMireborn(hero)", status_source)
        self.assertIn("terrain == TerrainId::SWAMP || terrain == TerrainId::ROUGH", status_source)
        self.assertLess(status_source.index("if(!hero)"), status_source.index("newHorizonsBulwark::hasSharedCover(hero)"))
        self.assertLess(status_source.index("if(!hero)"), status_source.index("newHorizonsBulwark::hasVengefulMire(hero)"))
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
        self.assertIn("Shared Cover adds", tooltip)
        self.assertIn("Shared Cover reaches its 100% Bulwark reduction cap", tooltip)
        self.assertIn("Vengeful Mire (adds", tooltip)

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
