"""Source contracts for Storm-only compact targeting and truthful combat feedback."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class StormTargetUI(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text()

    def test_panel_is_extracted_and_registered(self):
        window = self.read("client/battle/BattleWindow.cpp")
        cmake = self.read("client/CMakeLists.txt")
        self.assertNotIn("class BattleTargetSelectionPanel final", window)
        self.assertIn('battle/BattleTargetSelectionPanel.cpp', cmake)
        self.assertIn('battle/BattleTargetSelectionPanel.h', cmake)
        self.assertEqual(window.count("battleTargetSelectionPanel->reposition();"), 2)

    def test_storm_uses_compact_strip_above_console(self):
        panel = self.read("client/battle/BattleTargetSelectionPanel.cpp")
        self.assertIn("setCompactMode(storm);", panel)
        self.assertIn("pos.w = compact ? 500 : 720;", panel)
        self.assertIn("pos.h = compact ? 42 : 62;", panel)
        self.assertIn("owner.console->pos.y - pos.h - 4", panel)
        self.assertIn("targetReadback->setEnabled(!compact);", panel)
        self.assertIn("confirmButton->block(!preview.canConfirm);", panel)

    def test_forecasts_are_log_entries_cached_before_redraw(self):
        panel = self.read("client/battle/BattleTargetSelectionPanel.cpp")
        refresh = panel.split("void BattleTargetSelectionPanel::refreshStorm()", 1)[1]
        refresh = refresh.split("std::string BattleTargetSelectionPanel::stormPreviewLog", 1)[0]
        self.assertIn("loggedStormPreview != previewLog", refresh)
        self.assertIn("loggedStormCanConfirm != preview.canConfirm", refresh)
        self.assertLess(refresh.index("loggedStormPreview = previewLog;"),
                        refresh.index("owner.appendBattleLog("))
        self.assertLess(refresh.index("loggedStormCanConfirm = preview.canConfirm;"),
                        refresh.index("owner.appendBattleLog"))
        self.assertIn("Storm of Daggers preview:", panel)
        self.assertIn("per target before resistance.", panel)
        self.assertIn('message += "about "', panel)

    def test_same_targets_refresh_changed_forecasts_but_not_identical_redraws(self):
        panel = self.read("client/battle/BattleTargetSelectionPanel.cpp")
        formatter = panel.split("std::string BattleTargetSelectionPanel::stormPreviewLog", 1)[1]
        formatter = formatter.split("void BattleTargetSelectionPanel::setCompactMode", 1)[0]
        for field in ("selectedTargetCount", "maximumTargetCount", "poolAvailable",
                      "totalDamagePool", "rawDamagePerTarget", "targets",
                      "status"):
            self.assertIn("preview." + field, formatter)
        for field in ("name", "projectedDamage"):
            self.assertIn("target." + field, formatter)
        self.assertNotIn("loggedStormTargets", panel)
        self.assertIn("loggedStormPreview.clear();", panel)
        self.assertIn("stormPreviewLogged = false;", panel)
        refresh = panel.split("void BattleTargetSelectionPanel::refreshStorm()", 1)[1]
        refresh = refresh.split("std::string BattleTargetSelectionPanel::stormPreviewLog", 1)[0]
        self.assertEqual(refresh.count("owner.appendBattleLog("), 1)
        self.assertIn("const auto previewLog = stormPreviewLog(preview);", refresh)

    def test_order_markers_survive_without_floating_damage_badges(self):
        field = self.read("client/battle/BattleFieldController.cpp")
        stacks = self.read("client/battle/BattleStacksController.cpp")
        self.assertIn("stormOfDaggersSelectionOrder(stack->unitId())", stacks)
        self.assertIn("stormOfDaggersSelectedTargetIds()", field)
        self.assertNotIn("getStormOfDaggersRenderPreview()", field)
        self.assertIn("showChainLightningPreviewNumbers(clippedCanvas);", field)

    def test_all_selection_commands_still_use_controller(self):
        panel = self.read("client/battle/BattleTargetSelectionPanel.cpp")
        for call in ("undoStormOfDaggersTarget", "confirmStormOfDaggersTargets",
                     "undoSoulChainTarget", "confirmSoulChainTargets",
                     "undoRepeatedPlacement", "confirmRepeatedPlacement",
                     "endCastingSpell"):
            self.assertIn("->" + call + "();", panel)
        self.assertIn("EShortcut::GLOBAL_CANCEL", panel)

    def test_actual_result_uses_resolved_damage_and_casualties_only_for_storm(self):
        damage = self.read("scripts/spells/damage.lua")
        self.assertIn('local stormOfDaggers = mechanics:getSpell():getJsonKey() == "new-horizons:stormOfDaggers"', damage)
        block = damage.split("if stormOfDaggers then", 1)[1].split("\n\t\t\t\tend", 1)[0]
        self.assertIn("replaceNumbers = { dmg }", block)
        self.assertIn("unit:getCreature():getNameTextID(unit:getCount())", block)
        self.assertIn("BattleLog.creaturesPerish(server, battle, unit, killed)", block)
        self.assertNotIn("amount", block)
        self.assertIn("if describe and not stormOfDaggers and firstUnit and totalDamage > 0 then", damage)


if __name__ == "__main__":
    unittest.main()
