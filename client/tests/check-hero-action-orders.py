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

action = ACTION.read_text(encoding="utf-8")
controller = CONTROLLER.read_text(encoding="utf-8")
creature_window = CREATURE_WINDOW.read_text(encoding="utf-8")

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
assert '"Leadership: " + std::to_string(leadershipRequirement) + " each | Stack: "' in creature_window
assert '"Leadership: --"' in creature_window

# Frontend must submit requests through the callback, not mutate the battle
# snapshot or write the action budget/effects locally.
assert "getHeroCommandUsed" not in controller
assert "battleGetActiveOrder" not in controller
assert "setHeroCommand" not in action + controller

print("New Horizons Orders client surface: PASS")
