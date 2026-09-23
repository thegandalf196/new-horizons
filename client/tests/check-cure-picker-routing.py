#!/usr/bin/env python3
"""Static Cure routing/lifetime contract; not native or graphical acceptance."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def section(source, start, end):
    return source.split(start, 1)[1].split(end, 1)[0]


def check(controller, interface):
    begin_cast = section(controller, "void BattleActionsController::castThisSpell(",
                         "void BattleActionsController::")
    end_cast = section(controller, "void BattleActionsController::endCastingSpell()",
                       "void BattleActionsController::")
    assert "++castingSession;" in begin_cast
    assert "++castingSession;" in end_cast
    picker = section(interface, "void BattleInterface::installCureAfflictionUI()",
                     "void BattleInterface::installTemporalFieldUI()")
    assert "newHorizonsMagic::cureEnabled" in picker
    assert "if(choices.empty())" in picker
    assert "CObjectListWindow" in picker
    confirm = section(picker, "auto confirm =", "auto window =")
    assert confirm.index("getCastingSession() != session") < confirm.index("endCastingSpell()")
    assert confirm.index("!makingTurn()") < confirm.index("endCastingSpell()")
    assert "battleGetUnitByID(targetID)" in confirm
    assert "hero->id != heroID" in confirm
    assert "initialTarget" not in confirm
    assert confirm.index("canBeCastAt(targetCheck, problem)") < confirm.index("battleMakeSpellAction")
    assert "action.spellCureAffliction = choices[selected]" in confirm
    cancel = picker.split("window->onExit =", 1)[1]
    assert "getCastingSession() == session" in cancel
    assert "battleMakeSpellAction" not in cancel
    realize = section(controller, "void BattleActionsController::actionRealize(",
                      "void BattleActionsController::")
    assert "cureAfflictionPicker(pending, targetStack)" in realize
    hover = section(controller, "bool BattleActionsController::isCastingPossibleHere(",
                    "void BattleActionsController::activateStack()")
    assert "newHorizonsMagic::cureAfflictions(rules, cureTarget)" in hover
    assert "cast.setCureAffliction(affliction)" in hover
    assert "battleMakeSpellAction" not in hover


def main():
    controller = (ROOT / "client/battle/BattleActionsController.cpp").read_text()
    interface = (ROOT / "client/battle/BattleInterface.cpp").read_text()
    check(controller, interface)
    for before, after in (
        ("getCastingSession() != session", "false"),
        ("!makingTurn()", "false"),
        ("battleGetUnitByID(targetID)", "nullptr"),
        ("getCastingSession() == session", "true"),
    ):
        try:
            check(controller, interface.replace(before, after))
        except (AssertionError, ValueError):
            continue
        raise AssertionError(f"missed unsafe mutation: {before}")
    try:
        check(controller.replace("++castingSession;", ""), interface)
    except AssertionError:
        pass
    else:
        raise AssertionError("missed removal of casting-session invalidation")
    print("PASS: Cure client routing contract; five unsafe mutations rejected")
    print("NOT compiled, native gameplay, or graphical acceptance")


if __name__ == "__main__":
    main()
