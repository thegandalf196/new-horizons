#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Guard the authoritative Metamagic follow-up prompt lifecycle."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
interface_h = (ROOT / "client/battle/BattleInterface.h").read_text(encoding="utf-8")
interface = (ROOT / "client/battle/BattleInterface.cpp").read_text(encoding="utf-8")
window = (ROOT / "client/battle/BattleWindow.cpp").read_text(encoding="utf-8")
mechanics = (ROOT / "lib/spells/BattleSpellMechanics.cpp").read_text(encoding="utf-8")
spellbook = (ROOT / "client/windows/CSpellWindow.cpp").read_text(encoding="utf-8")
actions = (ROOT / "client/battle/BattleActionsController.cpp").read_text(encoding="utf-8")

assert "bool metamagicPromptPending = false;" in interface_h
end_action = interface.split("void BattleInterface::endAction", 1)[1].split(
    "void BattleInterface::presentAcceptedHeroOrder", 1
)[0]
assert "metamagicPromptPending = true;" in end_action
assert "openMetamagicSpellbook" not in end_action

activate = interface.split("void BattleInterface::activateStack", 1)[1].split(
    "bool BattleInterface::makingTurn", 1
)[0]
assert activate.index("actionsController->activateStack();") < activate.index(
    "if(metamagicPromptPending"
)
assert "metamagicPromptPending = false;" in activate
assert "battleCanUseMetamagicFollowup(side)" in activate
assert "windowObject->updateCounterspellStatus();" in activate
assert "windowObject->openMetamagicSpellbook();" not in activate

cancel = window.split("addShortcut(EShortcut::GLOBAL_CANCEL", 1)[1].split(
    "setShortcutBlocked(EShortcut::GLOBAL_ACCEPT", 1
)[0]
assert "metamagicFollowupModeActive()" in cancel
assert "declineMetamagicFollowup()" in cancel
assert "endCastingSpell()" in cancel

decline = interface.split(
    "void BattleInterface::declineMetamagicFollowup()", 1
)[1].split("void BattleInterface::toggleMetamagicGrandFollowup()", 1)[0]
assert "metamagicFollowupModeActive()" in decline
assert "if(curInt && getBattle())" in decline
assert "battleCanUseMetamagicFollowup(side)" in decline
assert "makeMetamagicDecline(side)" in decline
assert decline.count("actionsController->endCastingSpell();") == 1
assert decline.index("makeMetamagicDecline") < decline.index(
    "actionsController->endCastingSpell();"
)

exit_button = spellbook.split("void CSpellWindow::fexitb()", 1)[1].split(
    "void CSpellWindow::closeForSpellSelection()", 1
)[0]
assert "closeSpellbook(true);" in exit_button
spell_selection = spellbook.split("const SpellID selectedSpell = mySpell->id;", 1)[1].split(
    "battleInterface->castThisSpell(selectedSpell);", 1
)[0]
assert "closeForSpellSelection();" in spell_selection
assert "fexitb();" not in spell_selection

right_click = actions.split("void BattleActionsController::onHexRightClicked", 1)[1].split(
    "void BattleActionsController::onHexLeftClicked", 1
)[0]
assert right_click.index("metamagicFollowupModeActive()") < right_click.index(
    "heroOrderTargetingModeActive()"
)
assert "owner.declineMetamagicFollowup();" in right_click

assert "visibleSide == BattleSide::ALL_KNOWING || visibleSide == otherSide" in mechanics
assert "battle()->battleHasHero(otherSide)" in mechanics
print("PASS: Metamagic waits for active-stack authority, stays non-modal, cancel declines, hidden hero access is guarded")
