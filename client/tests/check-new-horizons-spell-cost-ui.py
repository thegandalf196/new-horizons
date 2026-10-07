#!/usr/bin/env python3
"""Focused binding guard; native cost fixtures establish arithmetic correctness."""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
book = (ROOT / "client/windows/CSpellWindow.cpp").read_text(encoding="utf-8")
overcharge = (ROOT / "client/battle/BattleInterface.cpp").read_text(encoding="utf-8")
mechanics = (ROOT / "lib/spells/BattleSpellMechanics.cpp").read_text(encoding="utf-8")
types = (ROOT / "lib/spells/SpellCostBreakdown.h").read_text(encoding="utf-8")
texts = json.loads((ROOT / "config/newHorizonsCombatTexts.json").read_text())
module = json.loads((ROOT / "Mods/new-horizons/mod.json").read_text())

for token in ("getSpellCostBreakdown", "listedCost", "finalCost", "stages"):
    assert token in book, f"spellbook lacks shared breakdown consumer: {token}"
for token in ("struct DLL_LINKAGE SpellCostStage", "struct DLL_LINKAGE SpellCostBreakdown"):
    assert token in types, f"cost result must be safe across library boundary: {token}"
assert "spellCost - 2" not in book, "frontend must not duplicate Arcane Economy arithmetic"
assert "metamagicBaseCost" not in overcharge, "Overcharge must use the shared follow-up cost"
assert "battleGetSpellCostBreakdown(spell, hero, 1, metamagicFollowup)" in overcharge
assert "battleGetSpellCostBreakdown(owner" not in mechanics, "cast legality must use the trace-free cost path"

for suffix in (
    "row", "listed", "current", "stage", "overcharge",
    "modifier.listedMultiplier", "modifier.wisdom", "modifier.adventureArtifact",
    "modifier.knightlySequence", "modifier.preparedCaster", "modifier.archmage",
    "modifier.alliedArmy", "modifier.enemyArmy", "modifier.minimumCost",
    "modifier.arcaneEconomy",
):
    key = "new-horizons.spellCost." + suffix
    assert key in texts and texts[key], f"missing cost translation: {key}"
    assert module["translations"][key] == texts[key], f"module translation drift: {key}"
    assert key in book, f"cost translation has no spellbook consumer: {key}"

print("New Horizons spell-cost UI bindings: PASS (not rendered acceptance)")
