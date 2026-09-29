#!/usr/bin/env python3
"""Static UI contract checks for saved-marker Quicksand placement."""

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


# Selection is enabled only through the saved-v3 row marker. The patch count
# comes from the active cast mechanics, which share School/Spellcraft/Warcasting
# and Empower inputs with the authoritative effect.
mode = between(CONTROLLER, "bool BattleActionsController::quicksandPlacementModeActive", "bool BattleActionsController::repeatedPlacementModeActive")
assert "quicksandSelectedPlacementEnabled" in mode
required = between(CONTROLLER, "int BattleActionsController::repeatedPlacementRequiredHexes", "bool BattleActionsController::repeatedPlacementReady")
assert "getNewHorizonsQuicksandPatchCount" in required
assert "setMetamagicFollowup(heroSpellToCast->metamagicFollowup)" in required
assert "bool quicksandPlacementModeActive() const" in CONTROLLER_H

# Saved-marker casts stop in a dedicated selector before ordinary NO_TARGET
# dispatch. V1/v2 and markerless v3 snapshots remain on that existing path.
cast = between(CONTROLLER, "void BattleActionsController::castThisSpell", "bool BattleActionsController::continueOrdinarySpellcast")
assert "landMinePlacementModeActive() || quicksandPlacementModeActive()" in cast
assert "possibleActions.clear()" in cast
assert "updateRepeatedPlacementStatus(BattleHex::INVALID)" in cast
assert cast.index("quicksandPlacementModeActive()") < cast.index("owner.getBattle()->getCasterAction")

# Every click is checked against the shared authoritative placement predicate,
# capped at the exact required count, and retained in click order.
legal = between(CONTROLLER, "bool BattleActionsController::repeatedPlacementHexIsLegal", "bool BattleActionsController::repeatedPlacementHexIsSelected")
assert "quicksandPlacementHexIsLegal(*owner.getBattle(), hex)" in legal
click = between(CONTROLLER, "void BattleActionsController::selectOrUndoRepeatedPlacementHex", "bool BattleActionsController::repeatedPlacementTargetsValid")
assert "repeatedPlacementHexIsLegal(clickedHex)" in click
assert "repeatedPlacementSelectedHexes.push_back(clickedHex)" in click
assert "repeatedPlacementSelectedHexes.erase(selected)" in click
assert "repeatedPlacementRequiredHexes()" in click

# Confirmation requires the exact count, rejects duplicates or newly illegal
# hexes against the current snapshot, checks castability, then serializes the
# ordered coordinates in one request.
validation = between(CONTROLLER, "bool BattleActionsController::repeatedPlacementTargetsValid", "void BattleActionsController::confirmRepeatedPlacement")
assert "repeatedPlacementReady()" in validation
assert "selected.insert(hex.toInt()).second" in validation
assert "repeatedPlacementHexIsLegal(hex)" in validation
assert "mechanics->canBeCastAt(target, problem)" in validation
confirm = between(CONTROLLER, "void BattleActionsController::confirmRepeatedPlacement", "void BattleActionsController::undoRepeatedPlacement")
assert "repeatedPlacementModeActive()" in confirm
assert "repeatedPlacementTargetsValid()" in confirm
assert "action.target.clear()" in confirm
assert "action.aimToHex(hex)" in confirm
assert "battleMakeSpellAction" in confirm
assert confirm.index("repeatedPlacementTargetsValid()") < confirm.index("BattleAction action")

# The field marks legal candidates and the ordered selection with existing
# battle highlights. The shared leather strip exposes an exact remaining count,
# Undo-last, Cancel, and explicit Confirm; keyboard and right-click paths match.
field = between(FIELD, "void BattleFieldController::showHighlightedHexes", "Rect BattleFieldController::hexPositionLocal")
assert "repeatedPlacementModeActive" in field
assert "getRepeatedPlacementLegalHexes" in field
assert "repeatedPlacementHexIsSelected" in field
assert "cellShade" in field
assert "cellUnitMovementHighlight" in field

panel = between(WINDOW, "class BattleTargetSelectionPanel", "BattleWindow::BattleWindow")
for required_text in (
    "repeatedPlacementModeActive()",
    "repeatedPlacementRequiredHexes()",
    "remaining",
    "#",
    'setTextOverlay("Undo"',
    'setTextOverlay("Cancel"',
    'setTextOverlay("Confirm"',
    'ImagePath::builtin("DiBoxBck")',
    'AnimationPath::builtin("settingsWindow/button80")',
    "undoRepeatedPlacement()",
    "confirmRepeatedPlacement()",
    "endCastingSpell()",
):
    assert required_text in panel, required_text

assert "quicksandPlacementModeActive()" in panel
assert "EShortcut::GLOBAL_ACCEPT" in WINDOW
assert "EShortcut::GLOBAL_BACKSPACE" in WINDOW
assert "EShortcut::GLOBAL_CANCEL" in WINDOW
right_click = between(CONTROLLER, "void BattleActionsController::onHexRightClicked", "bool BattleActionsController::heroSpellcastingModeActive")
assert "repeatedPlacementModeActive()" in right_click
assert "endCastingSpell()" in right_click

print("New Horizons saved-marker Quicksand placement UI checks passed")
