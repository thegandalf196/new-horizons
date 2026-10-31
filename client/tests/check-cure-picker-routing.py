#!/usr/bin/env python3
"""Static Cure routing/lifetime contract; not native or graphical acceptance."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def section(source, start, end):
    return source.split(start, 1)[1].split(end, 1)[0]


def braced_region(source, marker):
    """Bound this known function/lambda, skipping braces in literals/comments."""
    start = source.index(marker) + len(marker)
    opening = source.index("{", start)
    depth = 0
    tokens = r'''"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|//[^\n]*|/\*.*?\*/|[{}]'''
    for token in re.finditer(tokens, source[opening:], re.DOTALL):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return source[start:opening + token.end()]
    raise AssertionError(f"unclosed Cure source region: {marker}")


def check(controller, interface, adapter, session):
    begin_cast = section(controller, "void BattleActionsController::castThisSpell(",
                         "void BattleActionsController::")
    end_cast = section(controller, "void BattleActionsController::endCastingSpell()",
                       "void BattleActionsController::")
    assert "++castingSession;" in begin_cast
    assert "++castingSession;" in end_cast
    assert "installCureAfflictionUI();" in interface
    assert "void BattleInterface::installCureAfflictionUI()" not in interface
    assert "void BattleInterface::installCureAfflictionUI()" in adapter
    assert "newHorizonsMagic::cureEnabled" in adapter
    assert "if(choices.empty() && physicalChoices.empty())" in adapter
    assert "CObjectListWindow" in adapter
    assert "owner.get() != this" in adapter
    assert "std::weak_ptr<Battle> battle;" in session
    assert "result != current" in session
    assert "getCastingSession() != castingSession" in session
    guarded = section(session, "void confirm(", "void cancel(")
    assert guarded.index("heroSpellcastingModeActive()") < guarded.index("endCastingSpell()")
    assert guarded.index("!owner->makingTurn()") < guarded.index("endCastingSpell()")
    assert guarded.index("endCastingSpell()") < guarded.index("std::forward<Continue>(afterEnding)")
    installer = braced_region(adapter, "void BattleInterface::installCureAfflictionUI()")
    confirm = braced_region(installer, "auto confirm =")
    assert "[session, pending, localBattleID, targetID, heroID, choices, physicalChoices]" in confirm
    assert "session.confirm(CPlayerInterface::battleInt" in confirm
    assert "initialTarget" not in confirm and "[this" not in confirm
    assert "battleGetUnitByID(targetID)" in confirm
    assert "hero->id != heroID" in confirm
    assert confirm.index("hero->id != heroID") < confirm.index("battleMakeSpellAction")
    assert confirm.index("canBeCastAt(targetCheck, problem)") < confirm.index("battleMakeSpellAction")
    assert 'action.spellCureAffliction = removePhysical ? SpellID(SpellID::NONE) : choices[selected]' in confirm
    assert 'action.spellCurePhysicalAffliction = physicalKind' in confirm
    assert "physicalChoices[selected - choices.size()]" in confirm
    assert "choices.size() + physicalChoices.size()" in confirm
    assert "curePhysicalAfflictionChoices(rules, cureTarget)" in controller
    assert "curePhysicalAfflictionChoices(callback->getBattle()->getMagicRules(), initialTarget)" in adapter
    assert 'spells::curePhysical::present(rules, *target, "graveDust")' in adapter
    assert "spells::curePhysical::livingTarget(target)" in adapter
    cancel = braced_region(installer, "auto cancel =" if "auto cancel =" in installer else "window->onExit =")
    assert "[session]" in cancel
    assert "session.cancel(CPlayerInterface::battleInt)" in cancel
    assert "battleMakeSpellAction" not in cancel
    realize = section(controller, "void BattleActionsController::actionRealize(",
                      "void BattleActionsController::")
    assert "cureAfflictionPicker(pending, targetStack)" in realize
    hover = section(controller, "bool BattleActionsController::isCastingPossibleHere(",
                    "void BattleActionsController::activateStack()")
    assert "newHorizonsMagic::cureAfflictions(rules, cureTarget)" in hover
    assert "cast.setCureAffliction(affliction)" in hover
    assert "battleMakeSpellAction" not in hover


def check_layouts(sources):
    # Helper code may legally appear before/after the installer. Its window
    # assignment is not the installed cancellation callback under inspection.
    helper = '''
void relocatedPresenterHelper()
{
    auto window = makeWindow();
    window->onExit = [] { const char * braces = "} {"; };
    battleMakeSpellAction();
}
'''
    for before in (True, False):
        layout = list(sources)
        layout[2] = helper + sources[2] if before else sources[2] + helper
        check(*layout)
        for replacement in (
            "session.cancel(CPlayerInterface::battleInt); battleMakeSpellAction()",
            "/* cancelled call missing */",
        ):
            unsafe = list(layout)
            unsafe[2] = unsafe[2].replace("session.cancel(CPlayerInterface::battleInt)", replacement)
            try:
                check(*unsafe)
            except (AssertionError, ValueError):
                continue
            raise AssertionError("relocated helper hid unsafe actual cancel")
    literal = list(sources)
    literal[2] = literal[2].replace("session.cancel(CPlayerInterface::battleInt)",
        '/* } { */ const char * braces = "} {"; session.cancel(CPlayerInterface::battleInt)')
    check(*literal)


def main():
    sources = [
        (ROOT / path).read_text() for path in (
            "client/battle/BattleActionsController.cpp", "client/battle/BattleInterface.cpp",
            "client/battle/BattleCureAfflictionUI.cpp", "client/battle/CureAfflictionSession.h")]
    check(*sources)
    for index, before, after in (
        (0, "++castingSession;", ""),
        (3, "getCastingSession() != castingSession", "false"),
        (3, "result != current", "false"),
        (3, "!owner->makingTurn()", "false"),
        (2, "battleGetUnitByID(targetID)", "nullptr"),
        (2, "hero->id != heroID", "false"),
        (2, "physicalChoices[selected - choices.size()]", '"frozen"'),
        (2, 'spells::curePhysical::present(rules, *target, "graveDust")', "true"),
        (2, "session.cancel(CPlayerInterface::battleInt)", ""),
    ):
        mutated = list(sources)
        mutated[index] = mutated[index].replace(before, after)
        try:
            check(*mutated)
        except (AssertionError, ValueError):
            continue
        raise AssertionError(f"missed unsafe mutation: {before}")
    check_layouts(sources)
    print("PASS: Cure extracted source wiring; nine unsafe mutations rejected")
    print("PASS: relocated-helper/literal positives and four unsafe-cancel negatives")
    print("NOT compiled, native gameplay, or graphical acceptance")


if __name__ == "__main__":
    main()
