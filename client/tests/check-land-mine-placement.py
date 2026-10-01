#!/usr/bin/env python3
"""Static contract checks for New Horizons repeated battlefield placement.

This checks the human client wiring for canonical Land Mine and selected-mode
Quicksand. It is a source guard, not a substitute for authoritative rules tests
or a normal-input graphical playthrough.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTROLLER = (ROOT / "client/battle/BattleActionsController.cpp").read_text()
CONTROLLER_H = (ROOT / "client/battle/BattleActionsController.h").read_text()
FIELD = (ROOT / "client/battle/BattleFieldController.cpp").read_text()
WINDOW = (ROOT / "client/battle/BattleWindow.cpp").read_text()


def between(source: str, start: str, end: str) -> str:
    left = source.index(start)
    right = source.index(end, left)
    return source[left:right]


# Land Mine remains saved-ruleset and canonical-spell gated. Quicksand enters
# this selector only through the state-backed saved marker helper.
assert "newHorizonsMagic::rulesActive" in CONTROLLER
assert "newHorizonsMagic::isLandMine" in CONTROLLER
assert "getNewHorizonsLandMinePatchCount" in CONTROLLER
assert "newHorizonsMagic::quicksandSelectedPlacementEnabled" in CONTROLLER
assert "legacy/random obstacle action" in CONTROLLER

cast = between(CONTROLLER, "void BattleActionsController::castThisSpell", "bool BattleActionsController::continueOrdinarySpellcast")
assert "landMinePlacementModeActive() || quicksandPlacementModeActive()" in cast
assert "possibleActions.clear()" in cast
assert "owner.getBattle()->getCasterAction" in cast
assert cast.index("quicksandPlacementModeActive()") < cast.index("owner.getBattle()->getCasterAction")

# One ordered vector feeds either spell. Clicking a selected hex removes it;
# new selections are legal, unique, and capped at the exact live count.
click = between(CONTROLLER, "void BattleActionsController::selectOrUndoRepeatedPlacementHex", "bool BattleActionsController::repeatedPlacementTargetsValid")
assert "repeatedPlacementSelectedHexes.push_back(clickedHex)" in click
assert "repeatedPlacementSelectedHexes.erase(selected)" in click
assert "repeatedPlacementRequiredHexes()" in click
assert "repeatedPlacementModeActive()" in click

# Quicksand uses the saved mechanics count and authoritative placement predicate.
quicksand_mode = between(CONTROLLER, "bool BattleActionsController::quicksandPlacementModeActive", "bool BattleActionsController::repeatedPlacementModeActive")
assert "quicksandSelectedPlacementEnabled" in quicksand_mode
count = between(CONTROLLER, "int BattleActionsController::repeatedPlacementRequiredHexes", "bool BattleActionsController::repeatedPlacementReady")
assert "getNewHorizonsQuicksandPatchCount" in count
legal = between(CONTROLLER, "bool BattleActionsController::repeatedPlacementHexIsLegal", "bool BattleActionsController::repeatedPlacementHexIsSelected")
assert "quicksandPlacementHexIsLegal(*owner.getBattle(), hex)" in legal

# Confirmation sends the ordered vector only after a fresh legality/castability
# check. No partial selection reaches the callback.
valid = between(CONTROLLER, "bool BattleActionsController::repeatedPlacementTargetsValid", "void BattleActionsController::confirmRepeatedPlacement")
assert "repeatedPlacementReady()" in valid
assert "repeatedPlacementHexIsLegal(hex)" in valid
assert "mechanics->canBeCastAt(target, problem)" in valid
confirm = between(CONTROLLER, "void BattleActionsController::confirmRepeatedPlacement", "void BattleActionsController::undoRepeatedPlacement")
assert "repeatedPlacementReady()" in confirm
assert "repeatedPlacementTargetsValid()" in confirm
assert "action.target.clear()" in confirm
assert "action.aimToHex(hex)" in confirm
assert "battleMakeSpellAction" in confirm

# Escape/right-click cancel, Backspace undoes the last selection, and the
# existing continuous leather panel provides visible count/readback/buttons.
right_click = between(CONTROLLER, "void BattleActionsController::onHexRightClicked", "bool BattleActionsController::heroSpellcastingModeActive")
assert "repeatedPlacementModeActive()" in right_click
assert "endCastingSpell()" in right_click
assert "spell cancelled" in right_click
assert "void undoRepeatedPlacement()" in CONTROLLER_H
assert 'EShortcut::GLOBAL_ACCEPT' in WINDOW
assert "confirmRepeatedPlacement" in WINDOW
assert 'EShortcut::GLOBAL_BACKSPACE' in WINDOW
assert "undoRepeatedPlacement" in WINDOW
assert "BattleTargetSelectionPanel" in WINDOW
assert "remaining" in WINDOW
assert 'setTextOverlay("Undo"' in WINDOW
assert 'setTextOverlay("Cancel"' in WINDOW
assert 'setTextOverlay("Confirm"' in WINDOW
assert 'ImagePath::builtin("DiBoxBck")' in WINDOW
assert 'AnimationPath::builtin("settingsWindow/button80")' in WINDOW
assert "EShortcut::GLOBAL_CANCEL" in WINDOW
assert "updateBattleTargetSelectionControls" in WINDOW

# The battlefield keeps legal candidates, hover, and ordered selections
# distinct using existing highlight primitives.
field = between(FIELD, "void BattleFieldController::showHighlightedHexes", "Rect BattleFieldController::hexPositionLocal")
assert "repeatedPlacementModeActive" in field
assert "getRepeatedPlacementLegalHexes" in field
assert "repeatedPlacementHexIsLegal" in field
assert "cellShade" in field
assert "cellUnitMovementHighlight" in field

print("New Horizons repeated-placement client checks passed")
