#!/usr/bin/env python3
"""Source contract for Demonic Gate hover, selection, and cancellation safety.

The authoritative battle tests own Gate legality. This check protects the
client lifecycle where an unset reserve selection can otherwise be dereferenced
while hovering a hex, before the Gate button opens the reserve picker.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTROLLER = (ROOT / "client/battle/BattleActionsController.cpp").read_text(encoding="utf-8")
PANEL = (ROOT / "client/battle/UnitActionPanel.cpp").read_text(encoding="utf-8")


def section(source: str, start: str, end: str) -> str:
    return source.split(start, 1)[1].split(end, 1)[0]


def main() -> None:
    legality = section(
        CONTROLLER,
        "bool BattleActionsController::actionIsLegal(",
        "void BattleActionsController::actionRealize(",
    )
    gate_legality = section(
        legality,
        "case PossiblePlayerBattleAction::DEMONIC_GATE:",
        "case PossiblePlayerBattleAction::ATTACK:",
    )
    unset_guard = gate_legality.index("if(!source || !demonicGatingCreature.hasValue())")
    assert unset_guard < gate_legality.index("demonicGatingCreature.toCreature()")
    assert unset_guard < gate_legality.index("if(mobileGate && !demonicGatingMovement.isValid())")
    assert "if(!creature)\n\t\t\t\treturn false;" in gate_legality
    assert "return !path.empty()" in gate_legality  # valid Mobile Gate movement is retained

    realization = section(
        CONTROLLER,
        "void BattleActionsController::actionRealize(",
        "void BattleActionsController::",
    )
    assert realization.index("!demonicGatingCreature.hasValue()") < realization.index(
        "command.gatingCreature = demonicGatingCreature"
    )

    selector = section(
        CONTROLLER,
        "void BattleActionsController::selectDemonicGatingCreature(",
        "void BattleActionsController::resetCurrentStackPossibleActions(",
    )
    assert selector.index("if(!creature.hasValue() || !creature.toCreature())") < selector.index(
        "demonicGatingCreature = creature"
    )
    assert "demonicGatingMovement = BattleHex::INVALID;" in selector
    assert "possibleActions = {PossiblePlayerBattleAction::DEMONIC_GATE};" in selector

    reset = section(
        CONTROLLER,
        "void BattleActionsController::resetCurrentStackPossibleActions(",
        "void BattleActionsController::",
    )
    assert "demonicGatingCreature = CreatureID();" in reset
    assert "demonicGatingMovement = BattleHex::INVALID;" in reset

    gate_button = section(
        PANEL,
        "if(filteredActions.front().get() == PossiblePlayerBattleAction::DEMONIC_GATE)",
        "owner.actionsController->setPriorityActions(filteredActions);",
    )
    assert "CObjectListWindow" in gate_button
    assert "if(index >= 0 && static_cast<size_t>(index) < creatures.size())" in gate_button
    assert "selectDemonicGatingCreature(creatures[index])" in gate_button
    assert '"skill.new-horizons.demonicGating.name"' in PANEL
    assert '"new-horizons.skill.demonicGating.name"' not in PANEL

    print("Demonic Gate client hover-safety source checks passed")
    print("NOT compiled, native input/rendering, or runtime battle validation")


if __name__ == "__main__":
    main()
