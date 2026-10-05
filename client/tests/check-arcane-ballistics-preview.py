#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Guard Arcane Ballistics eligibility, shared PDR stage, and hover wiring."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = (root / "client/battle/BattleActionsController.cpp").read_text(encoding="utf-8")
callback = (root / "lib/battle/CBattleInfoCallback.cpp").read_text(encoding="utf-8")
payload = (root / "lib/combatScripts/IDamageCalculatorScript.h").read_text(encoding="utf-8")
calculator = (root / "scripts/damage/damageCalculator.lua").read_text(encoding="utf-8")
combat_skills = (root / "lib/battle/NewHorizonsCombatSkills.cpp").read_text(encoding="utf-8")
status = source.split("std::string BattleActionsController::actionGetStatusMessage(", 1)[1]
shot = status.split("case PossiblePlayerBattleAction::SHOOT:", 1)[1].split(
    "case PossiblePlayerBattleAction::AIMED_SPELL_CREATURE:", 1
)[0]
assert "battle.battleGetRangedAttackPenetration(attackInfo)" in shot
assert "penetration.creatureDefenseIgnoreBasisPoints" in shot
assert "penetration.physicalDamageReductionIgnorePercent > 0" in shot
assert "of combined Physical Damage Reduction ignored (included above)." in shot
assert "arcaneBreachStatus(" not in shot, "hover must not independently validate marks"

penetration = callback.split("RangedAttackPenetration CBattleInfoCallback::battleGetRangedAttackPenetration(", 1)[1].split(
    "DamageEstimation CBattleInfoCallback::calculateDmgRange(", 1
)[0]
for token in (
    "!attack.physicalDamage",
    "!attack.shooting",
    "!newHorizonsMagic::rulesActive(currentBattle->getMagicRules())",
    "attackerSide == defenderSide",
    "bonus->source != BonusSource::SPELL_EFFECT",
    "bonus->turnsRemain <= 0",
    "sideParameter->second.Float() != static_cast<int32_t>(attackerSide)",
    "newHorizonsSorcery::ARCANE_BREACH_MAX_MARKS",
    "newHorizonsCombatSkills::isPhysicalCreatureAttack(attack.attacker, attack.physicalDamage)",
    "physicalDamageReductionCapPercent(currentBattle->getMagicRules()) < 0",
    "attackerHero->hasActivePerk(",
    "newHorizonsSorcery::ARCANE_BALLISTICS_PERK",
    "ARCANE_BALLISTICS_PDR_IGNORE_PERCENT",
):
    assert token in penetration, token
assert "focusMagicStatus" not in penetration, "the shooter need not carry Focus Magic"
assert penetration.index("validMarks < newHorizonsSorcery::ARCANE_BREACH_MAX_MARKS") < penetration.index(
    "attackerHero->hasActivePerk("
), "Arcane Ballistics is gated by three valid existing marks"

callback_damage = callback.split("DamageEstimation CBattleInfoCallback::calculateDmgRange(", 1)[1].split(
    "DamageEstimation CBattleInfoCallback::battleEstimateDamage(", 1
)[0]
assert "battleGetRangedAttackPenetration(info)" in callback_damage
assert "payload.rangedDefenseIgnoreBasisPoints = rangedPenetration.creatureDefenseIgnoreBasisPoints" in callback_damage
assert "payload.physicalDamageReductionIgnorePercent = rangedPenetration.physicalDamageReductionIgnorePercent" in callback_damage

ordinary_attack = combat_skills.split("bool isPhysicalCreatureAttack(", 1)[1].split(
    "bool isPhysicalCreatureLuckAttack(", 1
)[0]
for token in ("physicalDamage", "isOrdinaryCreatureAttacker(attacker)", "SlotID::WAR_MACHINES_SLOT"):
    assert token in ordinary_attack, token

physical_stage = calculator.split("local function getPhysicalDamageReductionFactor(info)", 1)[1].split(
    "-- The same four, as methods", 1
)[0]
for token in (
    "local cappedDamageFactor = math.max(remainingDamage, 1 - capPercent / 100)",
    "local reductionIgnorePercent = math.max(0,",
    "return 1 - (1 - cappedDamageFactor) * (1 - reductionIgnorePercent / 100)",
):
    assert token in physical_stage, token
assert physical_stage.index("local cappedDamageFactor") < physical_stage.index("local reductionIgnorePercent")
assert "physicalDamageReductionIgnorePercent" in payload

print("PASS: three-mark active-perk eligibility, separate Defense/PDR payloads, capped-PDR penetration, and shared hover")
