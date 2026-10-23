#!/usr/bin/env python3
"""Source contracts only; native widget/audio/rendered acceptance is separate."""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CONTROLLER = (ROOT / "client/battle/BattleStacksController.cpp").read_text()
INTERFACE = (ROOT / "client/battle/BattleInterface.cpp").read_text()
PANEL = (ROOT / "client/battle/StackInfoBasicPanel.cpp").read_text()
WINDOW = (ROOT / "client/windows/CCreatureWindow.cpp").read_text()
STATUS = (ROOT / "client/battle/FrozenStatusPresentation.cpp").read_text()


class FrozenPresentationSourceTest(unittest.TestCase):
    def test_both_status_surfaces_share_real_physical_icon_and_help(self):
        section = WINDOW.split("CStackWindow::ActiveSpellsSection::ActiveSpellsSection(", 1)[1]
        section = section.split("struct OrderIndicator", 1)[0]
        self.assertIn("newHorizonsFrozenPresentation::currentTooltip(*battleStack)", section)
        self.assertIn("newHorizonsFrozenPresentation::FrozenStatusIcon", section)
        self.assertLess(section.index("currentTooltip"), section.index("battleStack->activeSpells()"))
        self.assertIn("++printed;", section.split("battleStack->activeSpells()", 1)[0])
        self.assertIn("printed >= 8", section)
        self.assertIn("newHorizonsFrozenPresentation::FrozenStatusIcon", PANEL)
        self.assertIn("newHorizonsFrozenPresentation::tooltip(displayedStatus.frozenShatterBonusPercent)", PANEL)
        self.assertNotIn("class FrozenStatusIcon", PANEL)

    def test_status_uses_current_marker_and_captured_battle_rules(self):
        current = STATUS.split("std::optional<std::string> currentTooltip(", 1)[1]
        current = current.split("std::size_t newApplicationCount", 1)[0]
        self.assertIn("!stack.alive() || !newHorizonsFrozen::isFrozen(stack)", current)
        self.assertIn("stack.getBattle()", current)
        self.assertIn("battle->getMagicRules()", current)
        self.assertNotIn("SpellID", current)
        self.assertIn('"new-horizons.combat.frozen.tooltip"', STATUS)
        self.assertIn('"new-horizons.combat.frozen.label"', STATUS)

    def test_audio_candidates_are_only_new_accepted_marker_additions(self):
        additions = STATUS.split("std::size_t newApplicationCount(", 1)[1]
        self.assertIn("packet.toAdd", additions)
        self.assertNotIn("packet.toUpdate", additions)
        self.assertNotIn("packet.toRemove", additions)
        self.assertIn("newHorizonsFrozen::markerApplicationRound(bonus)", additions)
        self.assertIn("unit->frozenLastAppliedRound() == *round", additions)
        self.assertIn("feedback.record(unitId, *round)", additions)
        self.assertIn("frozenApplicationFeedback.seed(s->unitId(), s->frozenLastAppliedRound())", CONTROLLER)
        self.assertIn("stacksController->stackEffectsSet(sse)", INTERFACE)

    def test_audio_is_staged_not_triggered_from_paint_or_resume(self):
        event = CONTROLLER.split("void BattleStacksController::stackEffectsSet(", 1)[1]
        event = event.split("void BattleStacksController::applyStackCreatureFormRefresh", 1)[0]
        self.assertIn("EAnimationEvents::AFTER_HIT", event)
        self.assertIn('playSound(AudioPath::builtin("ICERAY"))', event)
        painting = CONTROLLER.split("void BattleStacksController::showStack(", 1)[1]
        painting = painting.split("void BattleStacksController::initializeBattleAnimations", 1)[0]
        self.assertNotIn("playSound", painting)
        self.assertNotIn("newApplicationCount", painting)

    def test_only_misaligned_prism_removed_blue_hue_and_shatter_retained(self):
        painting = CONTROLLER.split("void BattleStacksController::showStack(", 1)[1]
        painting = painting.split("void BattleStacksController::tick(", 1)[0]
        self.assertIn("ColorRGBA(114, 186, 218, 140)", painting)
        self.assertNotIn("const int radius", painting)
        self.assertNotIn("vertices", painting)
        self.assertIn("frozenShatterFeedback", painting)
        self.assertIn("shattered.elapsedMilliseconds", painting)


if __name__ == "__main__":
    unittest.main()
