#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Guard Metamagic, generic combat-status providers, and action-count UI behavior."""

import json
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
mechanics_h = (ROOT / "lib/spells/ISpellMechanics.h").read_text(encoding="utf-8")
lua_mechanics = (ROOT / "luascript/api/spells/Mechanics.cpp").read_text(encoding="utf-8")
skill_schema = json.loads((ROOT / "config/schemas/skill.json").read_text(encoding="utf-8"))
new_horizons_skills = json.loads((ROOT / "config/newHorizonsSkills.json").read_text(encoding="utf-8"))

combat_status_providers = skill_schema["properties"]["combatStatus"]["properties"]["provider"]["enum"]
assert combat_status_providers == ["metamagicUses", "bloodrageDamage"]
assert new_horizons_skills["metamagic"]["combatStatus"]["provider"] == "metamagicUses"
assert new_horizons_skills["bloodrage"]["combatStatus"]["provider"] == "bloodrageDamage"

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
assert "battleCallback->battleGetFightingHero(side)" in refresh
assert "skill->getCombatStatusProvider() == CSkill::CombatStatusProvider::NONE" in refresh
assert "!skill ||" in refresh
assert "hero->getPerkSkillRank(skill->getJsonKey())" in refresh
assert "if(skillRank <= 0)" in refresh
assert "switch(skill->getCombatStatusProvider())" in refresh
assert "case CSkill::CombatStatusProvider::METAMAGIC_USES:" in refresh
assert "case CSkill::CombatStatusProvider::BLOODRAGE_DAMAGE:" in refresh
assert "newHorizonsMagic::metamagicRank(hero)" in refresh
assert "if(total <= 0)" in refresh
assert "battleCallback->battleMetamagicUsesConsumed(side)" in refresh
assert "std::clamp(battleCallback->battleMetamagicUsesConsumed(side), 0, total)" in refresh
assert 'std::to_string(total - consumed) + " / " + std::to_string(total)' in refresh
assert "newHorizonsBloodrage::capForRank(battle->getBloodrageRank(side))" in refresh
assert "battle->getBloodrageDamagePercent(side)" in refresh
assert '"/" + std::to_string(cap) + "%"' in refresh
assert refresh.index("switch(skill->getCombatStatusProvider())") < refresh.index(
    "const auto description = skill->getCombatStatusDescriptionTranslated()"
)
assert refresh.count("entries.push_back({skill->at(") == 1
assert "skill->at(std::clamp(skillRank, 1, 3)).iconSmall" in refresh
assert "skill->getNameTranslated()" in refresh
assert "skill->getCombatStatusDescriptionTranslated()" in refresh
assert "std::vector<CombatStatusEntry> entries;" in refresh
assert "panel->setBattleStatus(entries, actionCounts, showActionCounts);" in refresh
assert "statusArea->setStatus(entries, actionCounts, showActionCounts);" in refresh
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
assert "heroBattleStatusHeight(BattleSide::ATTACKER)" in outside_layout
assert "heroBattleStatusHeight(BattleSide::DEFENDER)" in outside_layout
assert "outsideStackInfoPanelExtent" in outside_layout
assert "stackPanelBottom <= ENGINE->screenDimensions().y" in outside_layout

# The status renderer is generic and resource/action rows remain distinct.
assert "struct CombatStatusEntry" in hero_panel_h
assert "std::vector<CombatStatusEntry> statusEntries;" in hero_panel_h
assert "const int statusRows = static_cast<int>(statusEntries.size());" in hero_panel
assert "const int countsTop = statusRows * HeroInfoPanelLayout::effectAreaRowHeight;" in hero_panel
assert "effectAreaMaxStatusRows" not in hero_panel_h
assert "outsideStackPanelOffsetY" not in hero_panel_h

# State-driven refreshes also recompute placement when rows appear or disappear.
update_status = window.split("void BattleWindow::updateCounterspellStatus()", 1)[1].split(
    "void BattleWindow::updateStackInfoWindow", 1
)[0]
assert "refreshHeroBattleStatus(BattleSide::ATTACKER);" in update_status
assert "refreshHeroBattleStatus(BattleSide::DEFENDER);" in update_status
assert "setPositionInfoWindow();" in update_status
update_hero = window.split("void BattleWindow::updateHeroInfoWindow", 1)[1].split(
    "void BattleWindow::refreshHeroBattleStatus", 1
)[0]
assert "refreshHeroBattleStatus" in update_hero
assert "setPositionInfoWindow();" in update_hero

# Expiry rewards arrive with the round packet, not a separate mana packet.
new_round = interface.split("void BattleInterface::newRound()", 1)[1].split(
    "void BattleInterface::giveCommand", 1
)[0]
assert "windowObject->heroManaPointsChanged(attackingHeroInstance);" in new_round
assert "windowObject->heroManaPointsChanged(defendingHeroInstance);" in new_round

# Preserve the existing hidden-hero access guard used by follow-up mechanics.
assert "visibleSide == BattleSide::ALL_KNOWING || visibleSide == otherSide" in mechanics
assert "battle()->battleHasHero(otherSide)" in mechanics

# Focus Magic's Lua effect consumes authoritative Metamagic provenance through
# the common Mechanics facade. Keep the interface and binding in lockstep.
mechanics_facade = mechanics_h.split("class DLL_LINKAGE Mechanics", 1)[1].split(
    "class DLL_LINKAGE BaseMechanics", 1
)[0]
base_mechanics = mechanics_h.split("class DLL_LINKAGE BaseMechanics", 1)[1]
assert "virtual bool isMetamagicFollowup() const" in mechanics_facade
assert "bool isMetamagicFollowup() const override;" in base_mechanics
assert 'R.method<&Mechanics::isMetamagicFollowup>("isMetamagicFollowup"' in lua_mechanics

print("PASS: pending Metamagic remains optional, action counts use live NH state, and sidebar geometry falls back safely")
