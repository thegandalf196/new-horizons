#!/usr/bin/env python3
"""Source guard for canonical three-location Vengeful Vines targeting."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTROLLER = (ROOT / "client/battle/BattleActionsController.cpp").read_text(encoding="utf-8")
CONTROLLER_H = (ROOT / "client/battle/BattleActionsController.h").read_text(encoding="utf-8")
FIELD = (ROOT / "client/battle/BattleFieldController.cpp").read_text(encoding="utf-8")
WINDOW = (ROOT / "client/battle/BattleWindow.cpp").read_text(encoding="utf-8")
RUNTIME_H = (ROOT / "lib/spells/NewHorizonsVengefulVines.h").read_text(encoding="utf-8")
RUNTIME = (ROOT / "lib/spells/NewHorizonsVengefulVines.cpp").read_text(encoding="utf-8")


def between(source: str, start: str, end: str) -> str:
    left = source.index(start)
    right = source.index(end, left)
    return source[left:right]


def main() -> None:
    assert "newHorizonsVengefulVines::enabled" in CONTROLLER
    assert "footprint(const battle::Target & target)" in RUNTIME_H
    assert "connectedTriples()" in RUNTIME_H
    assert "std::vector<BattleHex> vengefulVinesSelectedHexes" in CONTROLLER_H
    assert "BattleHex::EDir vengefulVinesOrientation" not in CONTROLLER_H
    assert "originSelected" not in CONTROLLER_H

    # The saved-v3 spell enters a dedicated selector before LOCATION reaches
    # the ordinary caster-action path that would submit a single-hex target.
    cast = between(CONTROLLER, "void BattleActionsController::castThisSpell",
                   "bool BattleActionsController::continueOrdinarySpellcast")
    assert "if(vengefulVinesTargetSelectionModeActive())" in cast
    assert "vengefulVinesBattleID = owner.getBattleID()" in cast
    assert cast.index("vengefulVinesTargetSelectionModeActive()") < cast.index("getCasterAction(spellID.toSpell()")
    assert "updateVengefulVinesStatus(BattleHex::INVALID)" in cast

    context = between(CONTROLLER, "bool BattleActionsController::vengefulVinesSelectionContextIsCurrent",
                      "const std::vector<BattleHex> & BattleActionsController::getVengefulVinesSelectedHexes")
    for token in ("vengefulVinesBattleID", "vengefulVinesPlayer", "vengefulVinesSide",
                  "vengefulVinesRound", "vengefulVinesHeroID", "owner.makingTurn()"):
        assert token in context

    # The shared rules helper owns exact-three, distinct, connected playable
    # location geometry and the authoritative validation reuses that helper.
    geometry = between(RUNTIME, "BattleHexArray footprint(const battle::Target & target)",
                       "connectedTriples()")
    for token in ("target.size() != 3", "destination.unitValue", "hex.isAvailable()",
                  "selected.contains(hex)", "mutualPosition(previous, hex)"):
        assert token in geometry, token

    validation = between(CONTROLLER, "bool BattleActionsController::vengefulVinesTargetsAreLegal",
                         "void BattleActionsController::updateVengefulVinesStatus")
    assert "selectedHexes.size() != vengefulVinesFootprintHexCount" in validation
    assert "target.emplace_back(hex)" in validation
    assert "newHorizonsVengefulVines::footprint(target)" in validation
    assert "vengefulVinesEnemyTargetCount(footprint) == 0" in validation
    assert "mechanics->canBeCast(problem)" in validation
    assert "mechanics->canBeCastAt(target, problem)" in validation

    candidate = between(CONTROLLER, "bool BattleActionsController::vengefulVinesHexIsLegalCandidate",
                        "int32_t BattleActionsController::vengefulVinesEnemyTargetCount")
    for token in ("hex.isAvailable()", "vengefulVinesSelectedHexes.size() >= vengefulVinesFootprintHexCount",
                  "std::ranges::find(vengefulVinesSelectedHexes, hex)", "mutualPosition(selected, hex)",
                  "vengefulVinesHexCompletesLegalCast(hex)"):
        assert token in candidate, token

    selection = between(CONTROLLER, "void BattleActionsController::selectVengefulVinesHex",
                        "void BattleActionsController::updateRepeatedPlacementStatus")
    assert selection.index("if(!vengefulVinesHexIsLegalCandidate(clickedHex))") < selection.index(
        "vengefulVinesSelectedHexes.push_back(clickedHex)")
    assert "action.target.clear()" in selection
    assert "for(const auto & hex : vengefulVinesSelectedHexes)" in selection
    assert "action.aimToHex(hex)" in selection
    assert selection.index("vengefulVinesSelectedHexes.push_back(clickedHex)") < selection.index("submitHeroSpellAction(action)")
    assert "confirmVengefulVines" not in CONTROLLER
    assert "rotateVengefulVinesOrientation" not in CONTROLLER

    undo = between(CONTROLLER, "void BattleActionsController::undoVengefulVinesSelection",
                   "void BattleActionsController::updateVengefulVinesStatus")
    assert "vengefulVinesSelectedHexes.pop_back()" in undo

    cleanup = between(CONTROLLER, "void BattleActionsController::endCastingSpell",
                      "bool BattleActionsController::isActiveStackSpellcaster")
    assert "vengefulVinesSelectedHexes.clear()" in cleanup
    assert "updateBattleTargetSelectionControls()" in cleanup
    right_click = between(CONTROLLER, "void BattleActionsController::onHexRightClicked",
                          "bool BattleActionsController::heroSpellcastingModeActive")
    assert "vengefulVinesTargetSelectionModeActive()" in right_click
    assert "endCastingSpell()" in right_click

    field = between(FIELD, "void BattleFieldController::showHighlightedHexes",
                    "Rect BattleFieldController::hexPositionLocal")
    assert "vengefulVinesTargetSelectionModeActive()" in field
    assert "getVengefulVinesSelectedHexes()" in field
    assert "vengefulVinesHexIsLegalCandidate(hovered)" in field
    assert "cellShade" in field and "cellUnitMovementHighlight" in field

    panel = between(WINDOW, "class BattleTargetSelectionPanel", "BattleWindow::BattleWindow")
    assert "vengefulVines" not in panel
    assert 'ImagePath::builtin("DiBoxBck")' in panel
    assert 'AnimationPath::builtin("settingsWindow/button80")' in panel
    assert "center();" in panel

    controls = between(WINDOW, "void BattleWindow::updateBattleTargetSelectionControls",
                       "void BattleWindow::bOpenActiveUnit")
    assert "vengefulVinesCanUndo" in controls
    assert "GLOBAL_ACCEPT" in controls and "GLOBAL_BACKSPACE" in controls
    assert "!vengefulVinesActive" in controls
    assert "undoVengefulVinesSelection()" in WINDOW
    assert "rotateVengefulVinesOrientation" not in WINDOW
    assert "confirmVengefulVines" not in WINDOW
    assert "GLOBAL_CANCEL" in WINDOW

    print("PASS: saved-v3 routing, three connected location targets, third-click validation/submission, undo, overlay, and cleanup")
    print("Source wiring only; rendered appearance and a live battle remain unverified")


if __name__ == "__main__":
    main()
