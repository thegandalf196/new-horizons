#!/usr/bin/env python3
"""Static Purify picker contract; not compiled or graphical acceptance."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTROLLER = (ROOT / "client/battle/BattleActionsController.cpp").read_text()
CONTROLLER_HEADER = (ROOT / "client/battle/BattleActionsController.h").read_text()
INTERFACE = (ROOT / "client/battle/BattleInterface.cpp").read_text()
WINDOW = (ROOT / "client/battle/PurifyWindow.cpp").read_text()
WINDOW_HEADER = (ROOT / "client/battle/PurifyWindow.h").read_text()


def section(source: str, start: str, end: str) -> str:
    return source.split(start, 1)[1].split(end, 1)[0]


def check() -> None:
    realize = section(CONTROLLER, "void BattleActionsController::actionRealize(",
                      "PossiblePlayerBattleAction BattleActionsController::selectAction(")
    assert "PossiblePlayerBattleAction::ANY_LOCATION" in realize
    assert "newHorizonsPurify::spellID()" in realize
    assert "purifyPicker(pending, targetHex)" in realize
    assert "battleMakeSpellAction" not in realize.split("purifyPicker(pending, targetHex)", 1)[0]
    assert "using PurifyPicker = std::function<bool(const BattleAction &, const BattleHex &)>;" in CONTROLLER_HEADER

    picker = section(INTERFACE, "void BattleInterface::installPurifyUI()",
                     "void BattleInterface::installTemporalFieldUI()")
    assert "eligibleStacks(" in picker
    assert "eligible.physicalPoison && !eligible.physicalPoisonAutomaticallyCleared" in picker
    assert "physicalPoisonChoiceID()" in picker
    assert "physicalPoisonAutomaticallyCleared" in picker
    assert "display.graveDustAutomaticallyCleared = eligible.graveDustAutomaticallyCleared" in picker
    assert "context.purifierWillClearGraveDust |= display.graveDustAutomaticallyCleared" in picker
    assert "eligible.physicalPoisonAutomaticallyCleared || eligible.graveDustAutomaticallyCleared" in picker
    assert "if(stack.graveDustAutomaticallyCleared)" in WINDOW
    assert "option.automaticPhysicalEffect" in WINDOW
    assert '"Grave Dust (Purifier removes this extra effect)"' in WINDOW
    assert "bool graveDustAutomaticallyCleared = false;" in WINDOW_HEADER
    assert "bool purifierWillClearGraveDust = false;" in WINDOW_HEADER
    assert "getCastingSession() != session" in picker
    assert "hero->id != heroID" in picker
    assert "stack->maximumSpellEffectChoices" in WINDOW
    assert "stack->spellEffectGroups" in WINDOW
    assert "canBeCastAt(targetCheck, problem)" in picker
    assert "action.aimToHex(center)" in picker
    assert "action.spellPurifyChoices = std::move(*accepted)" in picker
    assert "validatedPurifyChoices(currentEligible, choices)" in picker
    assert "if(!accepted)" in picker
    assert picker.index("canBeCastAt(targetCheck, problem)") < picker.index("battleMakeSpellAction")

    cancel = picker.split("context.cancel =", 1)[1].split("createAndPushWindow", 1)[0]
    assert "endCastingSpell()" in cancel
    assert "battleMakeSpellAction" not in cancel
    assert "!hasSelectableEffects && !context.purifierWillClearPhysicalPoison && !context.purifierWillClearGraveDust" in picker

    assert 'ImagePath::builtin("DiBoxBck")' in WINDOW
    assert "CListBox" in WINDOW
    assert "option.stackName" in WINDOW
    assert "option.spellName" in WINDOW
    assert '"Physical Poison (Purifier removes this extra effect)"' in WINDOW
    assert "option.maximumSpellEffectChoices" in WINDOW
    assert "Confirm" in WINDOW and "Cancel" in WINDOW
    assert "without spending Mana or the Hero Action" in WINDOW
    assert "selection->selected.empty() && !context.purifierWillClearPhysicalPoison && !context.purifierWillClearGraveDust" in WINDOW
    assert "void show(Canvas & to) override" not in WINDOW_HEADER


if __name__ == "__main__":
    check()
    print("PASS: Purify target click, per-stack choices, Purifier Poison/Grave Dust and cancel routing")
    print("NOT compiled, native gameplay, or graphical acceptance")
