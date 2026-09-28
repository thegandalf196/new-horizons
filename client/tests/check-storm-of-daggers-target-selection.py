#!/usr/bin/env python3
"""Source guard for the ordered Storm of Daggers battle target selector."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTROLLER = (ROOT / "client/battle/BattleActionsController.cpp").read_text(encoding="utf-8")
CONTROLLER_H = (ROOT / "client/battle/BattleActionsController.h").read_text(encoding="utf-8")
FIELD = (ROOT / "client/battle/BattleFieldController.cpp").read_text(encoding="utf-8")
STACKS = (ROOT / "client/battle/BattleStacksController.cpp").read_text(encoding="utf-8")
WINDOW = (ROOT / "client/battle/BattleWindow.cpp").read_text(encoding="utf-8")


def require(source: str, text: str, label: str) -> None:
    if text not in source:
        raise AssertionError(f"missing {label}: {text}")


def main() -> None:
    require(CONTROLLER, '"new-horizons:stormOfDaggers"', "canonical New Horizons spell identity")
    require(CONTROLLER, "constexpr int32_t stormOfDaggersMaximumTargets = 5", "five-stack cap")
    require(CONTROLLER, "std::set<uint32_t> distinct", "duplicate identity rejection")
    require(CONTROLLER, "unit->unitSide() != enemySide", "friendly-fire rejection")
    require(CONTROLLER, "!unit || !unit->alive()", "live target revalidation")
    require(CONTROLLER, "mechanics->canBeCastAt(target, problem)", "full-vector spell legality")
    require(CONTROLLER, "stormOfDaggersBattleID", "battle identity captured for revalidation")
    require(CONTROLLER, "stormOfDaggersRound", "round captured for revalidation")
    require(CONTROLLER, "stormOfDaggersHeroID", "casting hero captured for revalidation")
    require(CONTROLLER, "stormOfDaggersTargetHexIsLegal(hoveredHex)", "hover legality feedback")
    require(CONTROLLER, "selectStormOfDaggersTarget(clickedHex)", "battlefield click selection")
    require(CONTROLLER, "action.aimToUnit(target)", "ordered unit identities in submitted BattleAction")
    require(CONTROLLER, "confirmStormOfDaggersTargets()", "confirm revalidates before submission")
    require(CONTROLLER, "stormOfDaggersSelectedUnitIds.pop_back()", "undo removes most recent target")

    require(CONTROLLER_H, "std::vector<uint32_t> stormOfDaggersSelectedUnitIds", "presentation-only ordered IDs")
    require(CONTROLLER_H, "std::optional<int64_t> projectedDamage", "per-target projected damage result")
    require(CONTROLLER, "mechanics->setStormOfDaggersTargetCount(targetCount)", "shared forecast count context")
    require(CONTROLLER, "mechanics->getStormOfDaggersTotalDamage(targetCount)", "shared total-pool helper")
    require(CONTROLLER, "mechanics->getStormOfDaggersDamagePerTarget(targetCount)", "shared raw per-target helper")
    require(CONTROLLER, "effect.transformTarget(mechanics.get(), fullAim, canonicalTarget)",
            "shared transformed-effect forecast path")
    require(CONTROLLER, "effect.getHealthChange(mechanics.get(), oneTarget)", "target-specific mitigation forecast")
    for duplicated_formula in ("45 + 1.5", "0.15 *", "0.15*"):
        if duplicated_formula in CONTROLLER:
            raise AssertionError(f"client must not duplicate Storm of Daggers damage math: {duplicated_formula}")

    require(FIELD, "stormOfDaggersSelectedTargetIds()", "persistent selected-cell highlighting")
    require(FIELD, "stormOfDaggersTargetHexIsLegal(hovered)", "legal candidate highlighting")
    require(STACKS, "stormOfDaggersSelectionOrder(stack->unitId())", "numbered marker order")
    require(STACKS, "std::to_string(selectionOrder)", "visible numbered marker")

    require(WINDOW, '"Undo"', "mouse undo control")
    require(WINDOW, '"Cancel"', "mouse cancel control")
    require(WINDOW, '"Confirm"', "mouse confirm control")
    require(WINDOW, 'ImagePath::builtin("DiBoxBck"), Rect(0, 0, 720, 62)',
            "continuous tiled leather panel surface")
    require(WINDOW, "ColorRGBA(145, 18, 12), 2", "recessed red battle-frame accent")
    require(WINDOW, "ColorRGBA(213, 185, 117)", "fine brass outer frame")
    require(WINDOW, "ColorRGBA(0, 0, 0, 75), ColorRGBA(82, 65, 40, 255)",
            "forecast recessed into the leather panel")
    require(WINDOW, "Target limit reached; Confirm, or use Undo to revise.", "five-target completion guidance")
    require(WINDOW, "GLOBAL_ACCEPT", "Enter/accept confirmation shortcut")
    require(WINDOW, "GLOBAL_BACKSPACE", "Backspace undo shortcut")
    require(WINDOW, "endCastingSpell()", "Escape/cancel returns without submitting spell")
    require(WINDOW, "!owner.actionsController->stormOfDaggersSelectedTargetIds().empty()",
            "shortcut readiness refresh follows selection changes")

    print("PASS: ordered distinct enemy selection, live full-vector validation, shared damage forecast, controls, and markers")
    print("Source wiring only; rendered appearance and a live battle remain unverified")


if __name__ == "__main__":
    main()
