#!/usr/bin/env python3
"""Source contract for Demonic Gate hover, selection, and cancellation safety.

The authoritative battle tests own Gate legality. This check protects the
client lifecycle where an unset reserve selection can otherwise be dereferenced
while hovering a hex, before the Gate button opens the reserve picker.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTROLLER = (ROOT / "client/battle/BattleActionsController.cpp").read_text(encoding="utf-8")
GATE = (ROOT / "client/battle/BattleDemonicGateUI.cpp").read_text(encoding="utf-8")
RESERVE = (ROOT / "client/windows/CHeroDemonicReserve.cpp").read_text(encoding="utf-8")
PANEL = (ROOT / "client/battle/UnitActionPanel.cpp").read_text(encoding="utf-8")
WINDOW = (ROOT / "client/battle/BattleWindow.cpp").read_text(encoding="utf-8")


def section(source: str, start: str, end: str) -> str:
    return source.split(start, 1)[1].split(end, 1)[0]


def main() -> None:
    # All four user-facing Mobile paths must use the same saved-policy rule.
    helper = "newHorizonsDemonicGating::usesMobileGate(hero)"
    assert GATE.count(helper) == 4
    assert "new-horizons:demonicGating.mobileGate" not in GATE
    methods = (
        "std::string BattleActionsController::demonicGateStatus(",
        "std::string BattleActionsController::demonicGateBlockedStatus(",
        "bool BattleActionsController::demonicGateIsLegal(",
        "void BattleActionsController::demonicGateClicked(",
    )
    for method in methods:
        body = GATE.split(method, 1)[1].split("\n}\n", 1)[0]
        assert body.count(helper) == 1, "Mobile path bypasses shared policy: " + method
    legality = section(
        CONTROLLER,
        "bool BattleActionsController::actionIsLegal(",
        "void BattleActionsController::actionRealize(",
    )
    assert "return demonicGateIsLegal(targetHex);" in legality
    gate_legality = section(GATE, "bool BattleActionsController::demonicGateIsLegal(",
                            "void BattleActionsController::demonicGateClicked(")
    unset_guard = gate_legality.index("if(!source || !demonicGatingCreature.hasValue())")
    assert unset_guard < gate_legality.index("demonicGatingCreature.toCreature()")
    assert unset_guard < gate_legality.index("if(mobileGate && !demonicGatingMovement.isValid())")
    assert "if(!creature)\n\t\treturn false;" in gate_legality
    assert "return !path.empty()" in gate_legality  # valid Mobile Gate movement is retained

    realization = section(
        CONTROLLER,
        "void BattleActionsController::actionRealize(",
        "void BattleActionsController::",
    )
    assert "demonicGateClicked(targetHex);" in realization
    clicked = section(GATE, "void BattleActionsController::demonicGateClicked(",
                      "void BattleActionsController::selectDemonicGatingCreature(")
    assert clicked.index("!demonicGatingCreature.hasValue()") < clicked.index("prepareDemonicGateAction(*active, targetHex)")
    assert "owner.sendCommand(command, active);" in clicked
    selector = section(GATE, "void BattleActionsController::selectDemonicGatingCreature(",
                       "BattleAction BattleActionsController::prepareDemonicGateAction(")
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
    # Cancel must clear the highlighted toggle as well as placement mode. Otherwise
    # the next Gate click deselects it and restores movement instead of placement.
    assert "owner.windowObject->clearUnitActionSelection();" in reset
    assert reset.index("possibleActions = getPossibleActionsForStack(") < reset.index("clearUnitActionSelection();")
    clear_action_selection = section(WINDOW, "void BattleWindow::clearUnitActionSelection()",
                                     "void BattleWindow::setPossibleActions(")
    assert "unitActionWindow->clearSelection();" in clear_action_selection
    clear_buttons = section(PANEL, "void UnitActionPanel::clearSelection()",
                            "void UnitActionPanel::restoreAllActions()")
    assert "for(const auto & button : buttons)" in clear_buttons
    assert "button->setSelectedSilent(false);" in clear_buttons
    assert "setSelected(" not in clear_buttons  # Never reenter the action callback.
    assert "buttons.clear()" not in clear_buttons  # Keep the emitting button alive.
    assert "setPossibleActions(" not in reset + clear_action_selection + clear_buttons
    right_click = section(CONTROLLER, "void BattleActionsController::onHexRightClicked(",
                          "void BattleActionsController::")
    gate_cancel = right_click.split("if(demonicGatingCreature.hasValue())", 1)[1].split("return;", 1)[0]
    assert "resetCurrentStackPossibleActions();" in gate_cancel

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

    temporary_button = gate_button.split("if(newHorizonsDemonicGating::usesTemporaryGating(hero))", 1)[1].split("const int rank", 1)[0]
    assert "eligibleOriginalStack(*owner.getBattle(), *active, active->unitSide())" in temporary_button
    assert "selectDemonicGatingCreature(active->creatureId())" in temporary_button
    assert "return;" in temporary_button
    assert "newHorizonsDemonicGating::plan(*owner.getBattle()," in gate_legality
    availability = section(RESERVE, "void CHeroWindow::refreshDemonicReserveVisibility()", "void CHeroWindow::demonicReserveClicked()")
    assert 'curHero && (temporary ? hasReserve' in availability
    assert 'curHero->getPerkSkillRank("new-horizons:demonicGating") > 0' in availability
    assert "entry.second > 0" in availability
    recovery = RESERVE.split("void CHeroWindow::demonicReserveClicked()", 1)[1]
    assert 'if(!temporary && curHero->getPerkSkillRank("new-horizons:demonicGating") <= 0)' in recovery
    assert "const auto * selected = !temporary && garr ? garr->getSelection() : nullptr;" in recovery
    assert recovery.index("curHero->getOwner() != GAME->interface()->playerID") < recovery.index("const bool temporary")
    assert "arrangeDemonicReserve(curHero, SlotID(), creature, activeAmount, false)" in recovery
    print("Demonic Gate client hover-safety and withdraw-only recovery source checks passed")
    print("NOT compiled, native input/rendering, or runtime battle validation")


if __name__ == "__main__":
    main()
