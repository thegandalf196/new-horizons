#!/usr/bin/env python3
"""Source guard for the explicit Vengeful Vines orientation selector."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTROLLER = (ROOT / "client/battle/BattleActionsController.cpp").read_text(encoding="utf-8")
CONTROLLER_H = (ROOT / "client/battle/BattleActionsController.h").read_text(encoding="utf-8")
FIELD = (ROOT / "client/battle/BattleFieldController.cpp").read_text(encoding="utf-8")
WINDOW = (ROOT / "client/battle/BattleWindow.cpp").read_text(encoding="utf-8")


def between(source: str, start: str, end: str) -> str:
    left = source.index(start)
    right = source.index(end, left)
    return source[left:right]


def main() -> None:
    assert "newHorizonsVengefulVines::enabled" in CONTROLLER
    assert "newHorizonsVengefulVines::footprint(origin, direction)" in CONTROLLER
    assert "BattleHex::EDir vengefulVinesOrientation = BattleHex::RIGHT" in CONTROLLER_H
    assert "struct VengefulVinesSelectionPreview" in CONTROLLER_H

    # The saved-v3 spell enters a dedicated selector before LOCATION reaches
    # the ordinary caster-action path that would submit a single-hex target.
    cast = between(CONTROLLER, "void BattleActionsController::castThisSpell",
                   "bool BattleActionsController::continueOrdinarySpellcast")
    assert "if(vengefulVinesTargetSelectionModeActive())" in cast
    assert "vengefulVinesBattleID = owner.getBattleID()" in cast
    assert cast.index("vengefulVinesTargetSelectionModeActive()") < cast.index("getCasterAction(spellID.toSpell()")
    assert "updateVengefulVinesStatus(BattleHex::INVALID)" in cast

    context = between(CONTROLLER, "bool BattleActionsController::vengefulVinesSelectionContextIsCurrent",
                      "bool BattleActionsController::vengefulVinesOriginSelected")
    for token in ("vengefulVinesBattleID", "vengefulVinesPlayer", "vengefulVinesSide",
                  "vengefulVinesRound", "vengefulVinesHeroID", "owner.makingTurn()"):
        assert token in context

    # Full geometry, receptive enemy filtering, and mechanics legality are
    # rechecked on Confirm. Selection clicks only update origin/orientation.
    validation = between(CONTROLLER, "bool BattleActionsController::vengefulVinesTargetsAreLegal",
                         "VengefulVinesSelectionPreview BattleActionsController::getVengefulVinesSelectionPreview")
    assert "newHorizonsVengefulVines::footprint(origin, direction)" in validation
    assert "vengefulVinesEnemyTargetCount(footprint) == 0" in validation
    assert "mechanics->canBeCast(problem)" in validation
    assert "newHorizonsVengefulVines::footprint(target)" in validation
    assert "mechanics->canBeCastAt(target, problem)" in validation

    selection = between(CONTROLLER, "void BattleActionsController::selectVengefulVinesOriginOrOrientation",
                        "void BattleActionsController::confirmVengefulVines")
    assert "vengefulVinesOrientation = BattleHex::RIGHT" in selection
    assert "adjacentSpellDirection(vengefulVinesOrigin, clickedHex)" in selection
    assert "battleMakeSpellAction" not in selection
    assert "vengefulVinesEndpointIsLegal(clickedHex)" in selection

    confirmation = between(CONTROLLER, "void BattleActionsController::confirmVengefulVines",
                           "void BattleActionsController::updateRepeatedPlacementStatus")
    assert "vengefulVinesTargetsAreLegal" in confirmation
    assert "action.aimToHex(vengefulVinesOrigin)" in confirmation
    assert "cloneInDirection(vengefulVinesOrientation, false)" in confirmation
    assert "battleMakeSpellAction(owner.getBattleID(), action)" in confirmation
    assert confirmation.index("vengefulVinesTargetsAreLegal") < confirmation.index("BattleAction action")

    cleanup = between(CONTROLLER, "void BattleActionsController::endCastingSpell",
                      "bool BattleActionsController::isActiveStackSpellcaster")
    assert "vengefulVinesOrigin = BattleHex::INVALID" in cleanup
    assert "vengefulVinesOrientation = BattleHex::RIGHT" in cleanup
    assert "updateBattleTargetSelectionControls()" in cleanup
    right_click = between(CONTROLLER, "void BattleActionsController::onHexRightClicked",
                          "bool BattleActionsController::heroSpellcastingModeActive")
    assert "vengefulVinesTargetSelectionModeActive()" in right_click
    assert "endCastingSpell()" in right_click

    field = between(FIELD, "void BattleFieldController::showHighlightedHexes",
                    "Rect BattleFieldController::hexPositionLocal")
    assert "vengefulVinesTargetSelectionModeActive()" in field
    assert "getVengefulVinesLegalStartHexes()" in field
    assert "getVengefulVinesPreviewFootprint()" in field
    preview = between(CONTROLLER, "BattleHexArray BattleActionsController::getVengefulVinesPreviewFootprint",
                      "int32_t BattleActionsController::vengefulVinesEnemyTargetCount")
    assert "footprint(vengefulVinesOrigin, vengefulVinesOrientation)" in preview
    assert "hoveredEndpoint" not in preview
    assert "getVengefulVinesRotationHexes()" in field
    assert "cellShade" in field and "cellUnitMovementHighlight" in field

    panel = between(WINDOW, "class BattleTargetSelectionPanel", "BattleWindow::BattleWindow")
    for token in ("vengefulVinesTargetSelectionModeActive()", "getVengefulVinesSelectionPreview()",
                  'setTextOverlay(undoButtonText, FONT_SMALL, Colors::WHITE)', '"Rotate"',
                  'setTextOverlay("Cancel"', 'setTextOverlay("Confirm"',
                  'ImagePath::builtin("DiBoxBck")', 'AnimationPath::builtin("settingsWindow/button80")',
                  "rotateVengefulVinesOrientation()", "confirmVengefulVines()"):
        assert token in panel, token

    controls = between(WINDOW, "void BattleWindow::updateBattleTargetSelectionControls",
                       "void BattleWindow::bOpenActiveUnit")
    assert "vengefulVinesCanConfirm" in controls
    assert "vengefulVinesCanRotate" in controls
    assert "GLOBAL_ACCEPT" in controls and "GLOBAL_BACKSPACE" in controls
    assert "!vengefulVinesActive" in controls
    assert "GLOBAL_CANCEL" in WINDOW

    print("PASS: saved-v3 routing, six-way preview, full-fit enemy validation, explicit confirm, controls, and cleanup")
    print("Source wiring only; rendered appearance and a live battle remain unverified")


if __name__ == "__main__":
    main()
