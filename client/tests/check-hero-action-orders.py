#!/usr/bin/env python3
"""Static guard for the canonical New Horizons Orders client surface.

This is intentionally source-level: native gameplay tests own authoritative
legality. The check prevents a future UI refactor from silently dropping an
Order, bypassing the shared battlefield target selector, or mutating battle
state in the frontend.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ACTION = ROOT / "client/battle/BattleHeroActionWindow.cpp"
CONTROLLER = ROOT / "client/battle/BattleActionsController.cpp"
CREATURE_WINDOW = ROOT / "client/windows/CCreatureWindow.cpp"
HERO_WINDOW = ROOT / "client/windows/CHeroWindow.cpp"
DEVELOPMENT_WINDOW = ROOT / "client/windows/HeroGrowthWindow.cpp"

action = ACTION.read_text(encoding="utf-8")
controller = CONTROLLER.read_text(encoding="utf-8")
creature_window = CREATURE_WINDOW.read_text(encoding="utf-8")
hero_window = HERO_WINDOW.read_text(encoding="utf-8")
development_window = DEVELOPMENT_WINDOW.read_text(encoding="utf-8")

orders = {
    "CHARGE": "Charge",
    "FOCUS_FIRE": "Focus Fire",
    "RIPOSTE": "Riposte",
    "HOLD_THE_LINE": "Hold the Line",
    "BRACE": "Brace",
    "PROTECT": "Protect",
    "FLANK": "Flank",
    "SECOND_WIND": "Second Wind",
}

for identifier, label in orders.items():
    assert f"HeroCommand::{identifier}" in action, identifier
    assert f'"{label}"' in action, label

for image in (
    "NH_charge_button", "NH_focusFire_button", "NH_riposte_button",
    "NH_holdTheLine_button", "NH_brace_button", "NH_protect_button",
    "NH_flank_button", "NH_secondWind_button",
):
    assert f'"{image}"' in action, image
assert "NH_hero_actions_entry" not in action

for identifier in ("FOCUS_FIRE", "PROTECT", "FLANK", "SECOND_WIND"):
    assert f"HeroCommand::{identifier}" in action.split("bool isTargeted", 1)[1].split("}", 1)[0]

assert "battleCanBeginHeroCommand(side, entry.first)" in action
assert "battleGetHeroCommandTargets(side, entry.first)" in action
assert "beginHeroOrderTargeting(command)" in action
assert "BattleAction::makePairedHeroCommand(side, command" in controller
assert "BattleAction::makeTargetedHeroCommand(side, command" in controller
assert "playerCallback->battleMakeSpellAction(battleID, action)" in controller
assert "battlePrepareHeroOrderState" in controller
assert "battlePrepareFocusFireState" in controller
assert "isCanonicalRules" in controller
assert "heroOrderTargetingModeActive" in controller
assert "getHeroOrderTargetingLegalHexes" in (ROOT / "client/battle/BattleFieldController.cpp").read_text(encoding="utf-8")
assert "BattleHex::getDistance" not in controller
assert "adjacent(*" not in controller
assert "visibleCommandDisplays" in action
assert "CRClickPopup::createAndPush" in action
assert 'AnimationPath::builtin("NH_orders_gauntlet_framed")' in (ROOT / "client/battle/BattleWindow.cpp").read_text(encoding="utf-8")
assert "Protect unavailable. No legal Protector/Ward pair is available" in action
assert "protectPairUnavailable" in action
assert "entry.second->block(!available && !protectPairUnavailable)" in action
assert "OrderIndicatorsSection" in creature_window
assert "battleGetHeroOrderState" in creature_window
assert "cannot be dispelled" in creature_window
assert '"Leadership Cost"' in creature_window
assert 'const auto usageText = std::to_string(leadershipCount) + "/"' in creature_window
assert 'leadershipCapacity->maximum' in creature_window
assert 'battle->battleGetOwnerHero(stack)' in creature_window
assert 'std::to_string(siege->siegeRating)' in hero_window
assert 'capabilityLeadershipPerLevel(curHero->getCapabilityRules())' in hero_window
assert 'std::to_string(leadership->capacity) + " (+"' in hero_window
assert '" total, including artifacts and other modifiers.' in hero_window
assert '"Siege rating available to this hero"' in hero_window
assert '"Siege rating used by Ballista, Catapult, First Aid Tent and defensive tower formulas. It is not spent.' in hero_window
assert '"Siege " + std::to_string(siege->siegeRating) + " (War Machines "' in hero_window
assert 'hero->getSiegeCapabilities().has_value()' in hero_window
assert 'showsDevelopment = showsGrowth || showsCapabilities || showsMasteries || showsPerks' in hero_window
assert '"War Machines: " + rankName(siege->warMachinesRank)' in development_window
for canonical_output in (
    "siege->ballistaDamage", "siege->catapultStructuralDamage",
    "siege->firstAidHealing", "siege->defensiveTowerDamage",
):
    assert canonical_output in development_window
assert '"Siege is a rating, not a spendable resource.' in development_window
assert '"Legacy siege capabilities' in development_window

# Frontend must submit requests through the callback, not mutate the battle
# snapshot or write the action budget/effects locally.
assert "getHeroCommandUsed" not in controller
assert "battleGetActiveOrder" not in controller
assert "setHeroCommand" not in action + controller

print("New Horizons Orders client surface: PASS")
