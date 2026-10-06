#!/usr/bin/env python3
"""Focused wiring guard; native presentation tests cover state transitions."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PANEL = (ROOT / "client/battle/StackInfoBasicPanel.cpp").read_text()
STATUS = (ROOT / "client/battle/NewHorizonsBattleStatus.h").read_text()

readback = PANEL.split("currentSylvanLuckStatus(", 1)[1].split(
    "newHorizonsBattleStatus::StackInfoStatusSnapshot currentStackInfoStatus(", 1)[0]
for token in (
    "isOrdinaryCreatureAttacker(stack)",
    "battleGetOwner(stack)",
    "battleGetFightingHero(ownerSide)",
    "usesNewHorizonsBattleRules(hero)",
    "getPerkSkillRank(std::string(SYLVAN_LUCK_SKILL_KEY))",
    "getSylvanLuckState(ownerSide)",
    "battleGetAttackLuck(stack, nullptr, false, false)",
):
    assert token in readback, f"Missing readback/privacy gate: {token}"

assert "result.sylvanLuck = currentSylvanLuckStatus(stack, battleCallback);" in PANEL
assert "SylvanLuckStackStatus sylvanLuck;" in STATUS
assert "bool operator==(const StackInfoStatusSnapshot &) const = default;" in STATUS
assert "if(current == displayedStatus" in PANEL
assert "if(displayedStatus.sylvanLuck.active())" in PANEL
assert 'translate("skill.new-horizons.sylvanLuck.name")' in PANEL
assert "Rect(7, 141, 67, 14), tooltip, tooltip" in PANEL

print("PASS: synchronized current-controller Sylvan Luck state reaches existing Luck help and refresh snapshot")
