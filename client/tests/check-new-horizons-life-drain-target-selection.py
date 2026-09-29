#!/usr/bin/env python3
"""Source guard for ordered human Life Drain battlefield targeting."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ACTION = (ROOT / "lib/battle/PossiblePlayerBattleAction.h").read_text(encoding="utf-8")
CALLBACK = (ROOT / "lib/battle/CBattleInfoCallback.cpp").read_text(encoding="utf-8")
CONTROLLER = (ROOT / "client/battle/BattleActionsController.cpp").read_text(encoding="utf-8")
CONTROLLER_H = (ROOT / "client/battle/BattleActionsController.h").read_text(encoding="utf-8")


def between(source: str, start: str, end: str) -> str:
    left = source.index(start)
    right = source.index(end, left)
    return source[left:right]


assert "LIFE_DRAIN,           // New Horizons: ordered enemy-then-friendly" in ACTION
assert "action == WALK_AND_SPELLCAST || action == LIFE_DRAIN" in ACTION

registration = between(CALLBACK, "PossiblePlayerBattleAction CBattleInfoCallback::getCasterAction",
                       "BattleHexArray CBattleInfoCallback::battleGetAttackedHexes")
assert "spell->getJsonKey() == newHorizonsMagic::SHADOW_LIFE_DRAIN_SPELL" in registration
assert "mode == spells::Mode::HERO" in registration
assert "PossiblePlayerBattleAction::LIFE_DRAIN" in registration
assert registration.index("PossiblePlayerBattleAction::LIFE_DRAIN") < registration.index("auto targetTypes")

cast_start = between(CONTROLLER, "void BattleActionsController::castThisSpell",
                     "bool BattleActionsController::continueOrdinarySpellcast")
assert "getCasterAction(heroSpellToCast->spell.toSpell(), castingHero, spells::Mode::HERO).get()" in cast_start
assert "== PossiblePlayerBattleAction::LIFE_DRAIN" in cast_start
assert "lifeDrainSelectedUnitIds.clear()" in cast_start

validation = between(CONTROLLER, "bool BattleActionsController::lifeDrainTargetsAreLegal",
                     "bool BattleActionsController::lifeDrainTargetIsLegal")
assert "unitIds.size() > 2" in validation
assert "!enemy || !enemy->alive() || !enemy->isValidTarget(false) || enemy->isInvincible()" in validation
assert "enemy->unitSide() != battle->otherSide(lifeDrainSide)" in validation
assert "if(unitIds.size() == 1)" in validation
assert "return mechanics->isReceptive(enemy)" in validation
assert "!friendly || !friendly->alive() || !friendly->isValidTarget(false)" in validation
assert "friendly->unitSide() != lifeDrainSide" in validation
assert "pair.emplace_back(enemy, enemy->getPosition())" in validation
assert "pair.emplace_back(friendly, friendly->getPosition())" in validation
assert "mechanics->canBeCastAt(pair, problem)" in validation
assert "isLiving" not in validation and "BonusType::MECHANICAL" not in validation

hex_validation = between(CONTROLLER, "bool BattleActionsController::lifeDrainTargetHexIsLegal",
                         "void BattleActionsController::updateLifeDrainSelectionStatus")
assert "battleGetUnitByPos(hex, true)" in hex_validation
assert "getStackForHex" not in hex_validation

selection = between(CONTROLLER, "void BattleActionsController::selectLifeDrainTarget",
                    "bool BattleActionsController::fireWallPlacementModeActive")
assert "lifeDrainSelectedUnitIds.push_back(target->unitId())" in selection
assert "selected.push_back(target->unitId())" in selection
assert "action.target.clear()" in selection
assert "action.aimToUnit(chosen)" in selection
assert "battleMakeSpellAction(owner.getBattleID(), action)" in selection
assert selection.index("lifeDrainSelectedUnitIds.empty()") < selection.index("lifeDrainTargetsAreLegal(selected)")
assert "enemy->getName()" not in CONTROLLER

for method, next_method in (
    ("void BattleActionsController::onHexHovered", "void BattleActionsController::onHoverEnded"),
    ("void BattleActionsController::onHoverEnded", "void BattleActionsController::onHexLeftClicked"),
    ("void BattleActionsController::onHexLeftClicked", "void BattleActionsController::tryActivateStackSpellcasting"),
):
    assert "lifeDrainTargetSelectionModeActive()" in between(CONTROLLER, method, next_method)

cleanup = between(CONTROLLER, "void BattleActionsController::endCastingSpell", "void BattleActionsController::castThisSpell")
assert "lifeDrainSelectedUnitIds.clear()" in cleanup
assert "wasLifeDrainSelection" in cleanup
assert "wasLifeDrainSelection" in CONTROLLER_H or "lifeDrainSelectedUnitIds" in CONTROLLER_H

print("PASS: Life Drain action, ordered enemy/friendly selection, complete-pair validation, explicit unit IDs, and cleanup")
print("Source wiring only; rendered feedback and a live battle remain unverified")
