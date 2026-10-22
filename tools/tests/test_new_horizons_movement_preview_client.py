#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Battle movement-preview source wiring; not native/rendered acceptance."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index("{", source.index(signature))
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start + 1:end - 1]


class MovementPreviewClientTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        battle = ROOT / "client/battle"
        cls.controller = (battle / "BattleActionsController.cpp").read_text()
        cls.header = (battle / "BattleActionsController.h").read_text()
        cls.preview = (battle / "BattleMovementPreview.cpp").read_text()
        cls.cmake = (ROOT / "client/CMakeLists.txt").read_text()

    def test_component_is_registered_and_routes_selected_skirmisher(self):
        self.assertIn("battle/BattleMovementPreview.cpp", self.cmake)
        self.assertIn("battle/BattleMovementPreview.h", self.cmake)
        self.assertIn('include "BattleMovementPreview.h"', self.controller)
        body = function(self.controller, "BattleActionsController::actionGetStatusMessage(")
        self.assertIn("return skirmisherStatusMessage(targetHex);", body)
        self.assertNotIn("BattleAttackInfo estimate(attacker, selectedTarget", body)
        self.assertIn("skirmisherStatusMessage(const BattleHex & targetHex)", self.header)

    def test_skirmisher_warning_uses_legal_hover_endpoint_after_outgoing_estimate(self):
        body = function(self.preview, "BattleActionsController::skirmisherStatusMessage(")
        legal = body.index("!vstd::contains(legalFiringHexes, targetHex)")
        estimate = body.index("estimate.attackerPos = targetHex;")
        warning = body.index("overwatchReactorCount(*owner.getBattle(), attacker, targetHex)")
        self.assertLess(legal, estimate)
        self.assertLess(estimate, warning)
        self.assertIn("getSkirmisherLegalFiringHexes()", body)
        self.assertIn("skirmisherFiringDistancesCache[targetHex.toInt()]", body)
        self.assertIn("battleEstimateDamage(estimate)", body)
        self.assertIn("SKIRMISHER_DAMAGE_PERCENT", body)
        self.assertEqual(body.count("overwatchReactorCount("), 1)
        self.assertIn('"; " + attackCount + " at 75% damage."', body)
        self.assertIn('" may fire at 50%."', body)
        self.assertIn('"\\nEst/attack: "', body)
        for field, label in (("estimateResult.damage", "damage"), ("estimateResult.kills", "kills"),
                             ("{shotsAfterAction, shotsAfterAction}", "shots")):
            self.assertIn(f'previewQuantity({field}, "vcmi.battleWindow.damageEstimation.{label}")', body)
        self.assertNotIn("selectedTarget->getName()", body)
        self.assertNotIn("sendCommand", body)

    def test_ordinary_movement_and_melee_keep_shared_warning(self):
        body = function(self.controller, "BattleActionsController::actionGetStatusMessage(")
        self.assertIn("? battleMovementPreview::overwatchWarning(*owner.getBattle(), activeStack, targetHex) : std::string()", body)
        self.assertIn("+ battleMovementPreview::overwatchWarning(*owner.getBattle(), attacker, attackFromHex)", body)
        command = function(self.controller, "BattleActionsController::actionRealize(")
        self.assertIn("BattleAction::makeMeleeAttack(attacker, skirmisherTargetHex, targetHex, false)", command)
        self.assertIn("command.archerySkirmisherAttack = true;", command)
        self.assertIn("owner.sendCommand(command, attacker);", command)

    def test_warning_preserves_path_order_deduplication_and_shared_admission(self):
        body = function(self.preview, "static size_t overwatchReactorCount(")
        self.assertIn("destination == mover->getPosition()", body)
        self.assertIn("BattleHexArray{destination}", body)
        self.assertIn("battle.getPath(mover->getPosition(), destination, mover).first", body)
        self.assertIn("for(size_t index = path.size(); index > 0; --index)", body)
        self.assertIn("const auto to = path[index - 1];", body)
        self.assertIn("battle.battleGetOverwatchReactors(mover, from, to)", body)
        self.assertIn("std::set<uint32_t> reactors;", body)
        self.assertIn("reactors.insert(id);", body)
        self.assertIn("from = to;", body)
        warning = function(self.preview, "battleMovementPreview::overwatchWarning(")
        self.assertIn("overwatchReactorCount(battle, mover, destination)", warning)
        self.assertIn('" ready shooter(s) may react at 50% damage before arrival."', warning)
        self.assertNotIn("battleGetFightingHero", body)
        self.assertNotIn("sendAndApply", body)

    def test_compact_estimate_preserves_existing_localized_quantity_and_plural_rules(self):
        quantity = function(self.preview, "static std::string previewQuantity(")
        self.assertIn("range.min == range.max", quantity)
        self.assertIn('std::to_string(range.min) + "-" + std::to_string(range.max)', quantity)
        self.assertIn('MetaString::createFromTextID(textID + (range.max == 1 ? ".1" : ""))', quantity)
        self.assertIn('text.replaceTokenRawString("%d", quantity)', quantity)
        self.assertIn("text.toString(&GAME->translator())", quantity)


if __name__ == "__main__":
    unittest.main()
