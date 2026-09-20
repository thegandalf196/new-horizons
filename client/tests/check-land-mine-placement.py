#!/usr/bin/env python3
"""Static contract checks for the canonical New Horizons Land Mine UI path.

This intentionally checks source wiring rather than pretending to be a GUI or
authoritative battle test.  The server-side Land Mine tests cover packet and
rules validation; this guard keeps the human client from regressing to the
legacy random/no-target path or submitting an unconfirmed partial selection.
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


# Activation is saved-ruleset and canonical-spell gated; installed NH content
# must not change legacy Land Mine behavior.
assert "newHorizonsMagic::rulesActive" in CONTROLLER
assert "newHorizonsMagic::isLandMine" in CONTROLLER
assert "newHorizonsMagic::landMineHexCount" in CONTROLLER
assert "legacy/random obstacle action" in CONTROLLER

cast = between(CONTROLLER, "void BattleActionsController::castThisSpell", "bool BattleActionsController::continueOrdinarySpellcast")
assert "landMinePlacementModeActive()" in cast
assert "possibleActions.clear()" in cast
assert "owner.getBattle()->getCasterAction" in cast
assert cast.index("landMinePlacementModeActive()") < cast.index("owner.getBattle()->getCasterAction")

# Click selection preserves order, toggles an existing cell as undo, and never
# grows beyond the exact requirement.
click = between(CONTROLLER, "void BattleActionsController::selectOrUndoLandMineHex", "bool BattleActionsController::landMinePlacementTargetsValid")
assert "landMineSelectedHexes.push_back(clickedHex)" in click
assert "landMineSelectedHexes.erase(selected)" in click
assert "landMinePlacementRequiredHexes()" in click

# Confirmation is explicit and revalidates the live mechanics/target before a
# request is constructed.  The request is ordered by the selection vector.
confirm = between(CONTROLLER, "void BattleActionsController::confirmLandMinePlacement", "void BattleActionsController::undoLandMinePlacement")
assert "landMinePlacementReady()" in confirm
assert "landMinePlacementTargetsValid()" in confirm
assert "action.target.clear()" in confirm
assert "action.aimToHex(hex)" in confirm
assert "battleMakeSpellAction" in confirm

# Escape/right-click cancel and Backspace undo are separate from confirm.
right_click = between(CONTROLLER, "void BattleActionsController::onHexRightClicked", "bool BattleActionsController::heroSpellcastingModeActive")
assert "landMinePlacementModeActive()" in right_click
assert "endCastingSpell()" in right_click
assert "spell cancelled" in right_click
assert "void undoLandMinePlacement()" in CONTROLLER_H

# The field uses existing highlight primitives for candidates, hover, and the
# selected set; no new art or frontend-side state mutation is introduced.
field = between(FIELD, "void BattleFieldController::showHighlightedHexes", "Rect BattleFieldController::hexPositionLocal")
assert "getLandMinePlacementLegalHexes" in field
assert "landMinePlacementHexIsLegal" in field
assert "cellShade" in field
assert "cellUnitMovementHighlight" in field

# Return/Backspace are explicit controller shortcuts, while legacy battles
# leave both blocked because placement mode is inactive.
assert "EShortcut::GLOBAL_ACCEPT" in WINDOW
assert "confirmLandMinePlacement" in WINDOW
assert "EShortcut::GLOBAL_BACKSPACE" in WINDOW
assert "nhLandMineConfirm" in WINDOW
assert "Place mines" in WINDOW
assert "undoLandMinePlacement" in WINDOW
assert "updateLandMinePlacementControls" in WINDOW

print("canonical New Horizons Land Mine client placement checks passed")
