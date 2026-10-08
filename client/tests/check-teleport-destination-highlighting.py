#!/usr/bin/env python3
"""Source contract only: Teleport overlay wiring, legality delegation and cache.

Does not establish compiled/native input, rendering or authoritative acceptance.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
ACTIONS = (ROOT / "client/battle/BattleActionsController.cpp").read_text()
FIELD = (ROOT / "client/battle/BattleFieldController.cpp").read_text()
TELEPORT = (ROOT / "scripts/spells/teleport.lua").read_text()


def body(source, marker):
    start = source.index("{", source.index(marker))
    depth = 1
    cursor = start + 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[start:cursor]


class TeleportDestinationHighlighting(unittest.TestCase):
    def test_selection_is_second_stage_live_teleport_only(self):
        pending = body(ACTIONS, "BattleActionsController::getTeleportSelectedSpell(")
        self.assertIn("if(!selectedStack)", pending)
        self.assertIn("heroSpellToCast->spell == SpellID::TELEPORT", pending)
        self.assertIn("action.get() == PossiblePlayerBattleAction::TELEPORT", pending)
        self.assertNotIn("getHoveredHex", pending)
        text = body(ACTIONS, "BattleActionsController::getTeleportSelectedStack(")
        for token in ("spell->getId() != SpellID::TELEPORT", "!selectedStack",
                      "!heroSpellToCast && !monsterCaster", "battleGetAllStacks()",
                      "std::ranges::find(stacks, selectedStack)", "!selectedStack->alive()"):
            self.assertIn(token, text)
        self.assertLess(text.index("std::ranges::find"), text.index("selectedStack->alive()"))

    def test_all_playable_heads_delegate_full_paired_target(self):
        text = body(ACTIONS, "BattleActionsController::getTeleportDestinationHexes(")
        self.assertIn("index < GameConstants::BFIELD_SIZE", text)
        self.assertIn("hex.isAvailable() && isCastingPossibleHere(spell, selected, hex)", text)
        self.assertNotIn("battleGetAvailableHexes", text)
        self.assertNotIn("occupiedHex", text)
        shared = body(ACTIONS, "BattleActionsController::isCastingPossibleHere(")
        self.assertIn("target.emplace_back(targetStack)", shared)
        self.assertIn("target.emplace_back(targetHex)", shared)
        self.assertIn("m->canBeCastAt(target, problem)", shared)

    def test_blocked_and_wide_destinations_remain_shared_authority_rules(self):
        # isAccessibleForUnit owns full creature-footprint accessibility;
        # the client must not substitute single-cell or movement-range rules.
        self.assertIn("isAccessibleForUnit(unit, toHex)", TELEPORT)
        self.assertIn("hasPenaltyOnLine(fromHex, toHex", TELEPORT)
        click = body(ACTIONS, "BattleActionsController::actionIsLegal(")
        self.assertIn("isCastingPossibleHere(action.spell().toSpell(), selectedStack, targetHex)", click)

    def test_native_overlay_precedes_hover_only_paths(self):
        text = body(FIELD, "BattleFieldController::showHighlightedHexes(")
        self.assertLess(text.index("showTeleportDestinationHexes(canvas)"),
                        text.index("getHighlightedHexesForSpellRange()"))
        preview = body(FIELD, "BattleFieldController::showTeleportDestinationHexes(")
        self.assertIn("getTeleportSelectedSpell()", preview)
        self.assertIn("showHighlightedHex(canvas, cellShade, hex, true)", preview)
        self.assertNotIn("getHoveredHex().isValid()", preview)

    def test_cache_refresh_and_cancellation_do_not_poll_full_prediction(self):
        refresh = body(FIELD, "BattleFieldController::redrawBackgroundWithHexes()")
        self.assertIn("teleportPreviewNeedsRefresh = true", refresh)
        preview = body(FIELD, "BattleFieldController::showTeleportDestinationHexes(")
        for token in ("teleportPreviewStack != selected", "teleportPreviewSpell != spell",
                      "teleportPreviewCaster != casterIdentity->caster", "teleportPreviewBattle != battle->getBattle()",
                      "teleportPreviewSession != session", "teleportPreviewTreeVersion != treeVersion",
                      "teleportPreviewRound != round", "teleportDestinationHexes.clear()"):
            self.assertIn(token, preview)
        conditional = body(preview, "if(teleportPreviewNeedsRefresh")
        self.assertIn("getTeleportDestinationHexes(spell)", conditional)
        self.assertEqual(preview.count("getTeleportDestinationHexes(spell)"), 1)

    def test_caster_cache_uses_stable_unit_and_action_controller(self):
        identity = body(ACTIONS, "BattleActionsController::getTeleportPreviewCasterIdentity(")
        self.assertIn("TeleportPreviewCasterIdentity{hero, hero->getOwner(), hero->getTreeVersion()}", identity)
        self.assertIn("TeleportPreviewCasterIdentity{creature, battle->battleGetActionController(creature)", identity)
        self.assertNotIn("getCurrentSpellcaster()", identity)
        self.assertNotIn("make_unique", identity)
        preview = body(FIELD, "BattleFieldController::showTeleportDestinationHexes(")
        self.assertNotIn("getCurrentSpellcaster()", preview)
        self.assertIn("teleportPreviewController != casterIdentity->actionController.getNum()", preview)
        self.assertIn("teleportPreviewCasterTreeVersion != casterIdentity->treeVersion", preview)
        self.assertLess(preview.index("getTeleportDestinationHexes(spell)"),
                        preview.index("teleportPreviewTreeVersion = selected->getTreeVersion()"))


if __name__ == "__main__":
    unittest.main()
