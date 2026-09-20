#!/usr/bin/env python3
"""Static guard for the canonical New Horizons Orders client surface.

This is intentionally source-level: native gameplay tests own authoritative
legality. The check prevents a future UI refactor from silently dropping an
Order, bypassing the shared target selector, or mutating battle state in the
frontend.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ACTION = ROOT / "client/battle/BattleHeroActionWindow.cpp"
TARGET = ROOT / "client/battle/FocusFireTargetWindow.cpp"

action = ACTION.read_text(encoding="utf-8")
target = TARGET.read_text(encoding="utf-8")

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

for identifier in ("FOCUS_FIRE", "PROTECT", "FLANK", "SECOND_WIND"):
    assert f"HeroCommand::{identifier}" in action.split("bool isTargeted", 1)[1].split("}", 1)[0]

assert "battleCanBeginHeroCommand(side, entry.first)" in action
assert "battleGetHeroCommandTargets(side, entry.first)" in action
assert "createAndPushWindow<FocusFireTargetWindow>(owner, command)" in action
assert "BattleAction::makePairedHeroCommand(context->side, context->command" in target
assert "BattleAction::makeTargetedHeroCommand(context->side, context->command" in target
assert "playerCallback->battleMakeSpellAction(battleID, action)" in target
assert "battlePrepareHeroOrderState" in target
assert "battlePrepareFocusFireState" in target
assert "isCanonicalRules" in target
assert "prepared(*context, *owner, {*context->first, id})" in target
assert "BattleHex::getDistance" not in target
assert "adjacent(*" not in target
assert "visibleCommandDisplays" in action
assert "CRClickPopup::createAndPush" in action
assert 'AnimationPath::builtin("NH_orders_gauntlet")' in (ROOT / "client/battle/BattleWindow.cpp").read_text(encoding="utf-8")
assert "Protect unavailable. No legal Protector/Ward pair is available" in action
assert "protectPairUnavailable" in action
assert "entry.second->block(!available && !protectPairUnavailable)" in action

# Frontend must submit requests through the callback, not mutate the battle
# snapshot or write the action budget/effects locally.
assert "getHeroCommandUsed" not in target
assert "battleGetActiveOrder" not in target
assert "setHeroCommand" not in action + target

print("New Horizons Orders client surface: PASS")
