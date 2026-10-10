#!/usr/bin/env python3
"""Localized Order-badge metadata bindings, not rendered acceptance."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
helper = (ROOT / "client/windows/NewHorizonsOrderBadgeHelp.h").read_text()
consumer = (ROOT / "client/windows/CCreatureWindow.cpp").read_text()
texts = json.loads((ROOT / "config/newHorizonsCombatTexts.json").read_text())

for token in (
    "state.command == HeroCommand::NONE", "BattleSide::ATTACKER", "BattleSide::DEFENDER",
    "state.issuedRound <= 0", "!state.hasScheduledRecipients(currentRound)",
    "currentRound == state.issuedRound",
    'replaceTokenTextID("%SOURCE%"', 'replaceTokenNumber("%ROUND%", state.issuedRound)',
):
    assert token in helper
for forbidden in ("sendAndApply", "getFightingHero", "getName", "setOrder", "setPosition"):
    assert forbidden not in helper
for key in ("sourceExpiry", "carriedExpiry", "attacker", "defender"):
    assert f"new-horizons.combat.orderBadge.{key}" in texts
template = texts["new-horizons.combat.orderBadge.sourceExpiry"]
assert "%SOURCE%" in template and "%ROUND%" in template
assert "end of round" in template and "used or broken" in template
carried = texts["new-horizons.combat.orderBadge.carriedExpiry"]
assert "%SOURCE%" in carried and "%ROUND%" in carried
assert "activation" in carried.lower() and "used or broken" in carried
assert "battleOrderBenefitAppliesTo(*state, side, stack)" in consumer
assert "newHorizonsOrderBadgeHelp::sourceAndExpiry(" in consumer
assert "*state, side, battle->battleGetRound()" in consumer
assert "sourceExpiry.toString(&GAME->translator())" in consumer
print("PASS: localized current Order source/expiry help reaches existing creature badges; no hidden hero lookup")
