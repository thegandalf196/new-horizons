#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Guard the non-forcing Metamagic and authoritative action-count UI contract."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
interface_h = (ROOT / "client/battle/BattleInterface.h").read_text(encoding="utf-8")
interface = (ROOT / "client/battle/BattleInterface.cpp").read_text(encoding="utf-8")
window_h = (ROOT / "client/battle/BattleWindow.h").read_text(encoding="utf-8")
window = (ROOT / "client/battle/BattleWindow.cpp").read_text(encoding="utf-8")
hero_panel = (ROOT / "client/battle/HeroInfoWindow.cpp").read_text(encoding="utf-8")
hero_panel_h = (ROOT / "client/battle/HeroInfoWindow.h").read_text(encoding="utf-8")
actions_h = (ROOT / "client/battle/BattleActionsController.h").read_text(encoding="utf-8")
actions = (ROOT / "client/battle/BattleActionsController.cpp").read_text(encoding="utf-8")
spellbook = (ROOT / "client/windows/CSpellWindow.cpp").read_text(encoding="utf-8")
mechanics = (ROOT / "lib/spells/BattleSpellMechanics.cpp").read_text(encoding="utf-8")

# A pending authoritative follow-up is available to the player; it is not an
# interrupt, a spellbook launch, or a client-side decline command.
forced_ui_terms = (
    "metamagicPromptPending",
    "beginMetamagicFollowup",
    "metamagicFollowupMode",
    "metamagicFollowupModeActive",
    "declineMetamagicFollowup",
    "makeMetamagicDecline",
    "metamagicDeclineButton",
    "openMetamagicSpellbook",
    "Decline / End Metamagic",
)
for source in (interface_h, interface, window_h, window, actions_h, actions, spellbook):
    for term in forced_ui_terms:
        assert term not in source, f"forced Metamagic UI term remains: {term}"

activate = interface.split("void BattleInterface::activateStack", 1)[1].split(
    "bool BattleInterface::makingTurn", 1
)[0]
assert "actionsController->activateStack();" in activate
assert "openSpellbook" not in activate

end_action = interface.split("void BattleInterface::endAction", 1)[1].split(
    "void BattleInterface::presentAcceptedHeroOrder", 1
)[0]
assert "windowObject->updateCounterspellStatus();" in end_action

open_book = window.split("void BattleWindow::openSpellbook()", 1)[1].split(
    "void BattleWindow::bWaitf()", 1
)[0]
assert "battleCanUseMetamagicFollowup" not in open_book
assert "beginMetamagicFollowup" not in open_book

cancel = window.split("addShortcut(EShortcut::GLOBAL_CANCEL", 1)[1].split(
    "setShortcutBlocked(EShortcut::GLOBAL_ACCEPT", 1
)[0]
assert "actionsController->endCastingSpell();" in cancel
assert "battleMakeSpellAction" not in cancel

# Wait remains an ordinary creature action even while a spell allowance is
# pending; opening the spellbook later is the player's explicit choice.
wait = window.split("void BattleWindow::bWaitf()", 1)[1].split(
    "void BattleWindow::bDefencef()", 1
)[0]
assert "owner.giveCommand(EActionType::WAIT);" in wait
assert "Metamagic" not in wait

cast = actions.split("void BattleActionsController::castThisSpell", 1)[1].split(
    "bool BattleActionsController::continueOrdinarySpellcast", 1
)[0]
assert "battleCanUseMetamagicFollowup(heroSpellToCast->side)" in cast
assert "heroSpellToCast->metamagicFollowup" in cast
assert "metamagicGrand" not in cast

# Grand extension is selected from authoritative consumed-use state by the
# runtime, never from a client-side mode or BattleAction request bit. All
# spell previews likewise use the default (non-player-selected) legality path.
client_sources = (interface_h, interface, window_h, window, actions_h, actions, spellbook,
                  (ROOT / "client/windows/CSpellWindow.h").read_text(encoding="utf-8"))
for source in client_sources:
    for term in ("metamagicGrand", "toggleMetamagicGrandFollowup", "metamagicGrandModeActive"):
        assert term not in source, f"manual Grand request/control remains: {term}"
assert "setMetamagicFollowup(followup)" in actions
assert "setMetamagicGrand" not in actions + interface
assert "battleCanUseMetamagicFollowup(metamagicSide)" in spellbook
assert "METAMAGIC_ARCANE_ECONOMY" in spellbook
assert "owner->myHero);" in spellbook.split("const bool canCast", 1)[1].split("if(canCast)", 1)[0]

book_exit = spellbook.split("void CSpellWindow::fexitb()", 1)[1].split(
    "void CSpellWindow::closeForSpellSelection()", 1
)[0]
assert "closeSpellbook();" in book_exit
assert "decline" not in book_exit.lower()
close_book = spellbook.split("void CSpellWindow::closeSpellbook()", 1)[1].split(
    "void CSpellWindow::fadvSpellsb()", 1
)[0]
assert "battleMakeSpellAction" not in close_book
assert "closeSpellbook(bool" not in (ROOT / "client/windows/CSpellWindow.h").read_text(encoding="utf-8")

# The compact sidebar consumes the same authoritative, current-round counts as
# command validation, and only appears for sides with a real hero in NH rules.
refresh = window.split("void BattleWindow::refreshHeroBattleStatus", 1)[1].split(
    "void BattleWindow::updateCounterspellStatus", 1
)[0]
assert "battleCallback->battleUsesHeroCommands()" in refresh
assert "battle->getSideHero(side) != nullptr" in refresh
assert "battleHeroActionAllowanceCounts(side)" in refresh
assert 'addCount(0, "Hero", actionCounts.heroActions);' in hero_panel
assert 'addCount(1, "Order", actionCounts.orderActions);' in hero_panel
assert 'addCount(2, "Spell", actionCounts.spellActions);' in hero_panel
assert "Hero Actions can cast a spell OR issue an Order." in hero_panel
assert "Spell Actions can only cast spells; Order Actions can only issue Orders." in hero_panel

# If the larger outside column will not fit vertically, existing compact
# overlay placement is retained instead of cropping the active-stack panel.
outside_layout = window.split("bool BattleWindow::placeInfoWindowsOutside() const", 1)[1].split(
    "bool BattleWindow::quickActionsPanelActive() const", 1
)[0]
assert "HeroInfoPanelLayout::outsideStackPanelOffsetY" in outside_layout
assert "outsideStackInfoPanelExtent" in outside_layout
assert "stackPanelBottom <= ENGINE->screenDimensions().y" in outside_layout

# Expiry rewards arrive with the round packet, not a separate mana packet.
new_round = interface.split("void BattleInterface::newRound()", 1)[1].split(
    "void BattleInterface::giveCommand", 1
)[0]
assert "windowObject->heroManaPointsChanged(attackingHeroInstance);" in new_round
assert "windowObject->heroManaPointsChanged(defendingHeroInstance);" in new_round

# Preserve the existing hidden-hero access guard used by follow-up mechanics.
assert "visibleSide == BattleSide::ALL_KNOWING || visibleSide == otherSide" in mechanics
assert "battle()->battleHasHero(otherSide)" in mechanics

print("PASS: pending Metamagic remains optional, action counts use live NH state, and sidebar geometry falls back safely")
