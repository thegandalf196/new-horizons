/*
 * BracePreemptiveForecast.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BracePreemptiveForecast.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/NewHorizonsArmorer.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/bonuses/BonusList.h"

namespace
{
bool endedActivation(const battle::Unit & unit)
{
	const auto state = unit.acquireState();
	return state && state->armorerLastStandEndedActivation;
}

bool hasCombatReactions(const battle::Unit & unit)
{
	return unit.hasBonusOfType(BonusType::COMBAT_EVENT_TRIGGER)
		|| unit.hasBonusOfType(BonusType::ON_COMBAT_EVENT);
}

bool needsPreHitProjection(const CBattleInfoCallback & battle,
	const battle::Unit & attacker, const battle::Unit & victim)
{
	if(battle.battleResolveHeroOrderTarget(&attacker, &victim, false) != &victim)
		return true;
	if(hasCombatReactions(attacker) || hasCombatReactions(victim))
		return true;
	// Expected-Luck damage does not model the independent Death Blow/bonus-
	// damage rolls in rollAttackFlags. Do not silently omit such a multiplier.
	const auto * hero = battle.battleGetFightingHero(attacker.unitSide());
	if(attacker.hasBonusOfType(BonusType::DOUBLE_DAMAGE_CHANCE)
		|| (hero && battle.battleGetHeroBonuses(attacker.unitSide(), Selector::typeSubtype(
			BonusType::BONUS_DAMAGE_CHANCE, BonusSubtypeID(attacker.creatureId())))->totalValue() != 0))
		return true;
	// A secondary target can react before the primary hit too. Use the existing
	// footprint query, never a duplicated breath/area-attack rules whitelist.
	const auto targets = battle.getAttackedBattleUnits(&attacker, &victim,
		victim.getPosition(), false, attacker.getPosition(), victim.getPosition());
	return std::ranges::any_of(targets, [](const auto * target)
	{
		return hasCombatReactions(*target);
	});
}

heroOrderValue::BracePrimaryHit forecastPrimary(const CBattleInfoCallback & battle,
	const battle::Unit & attacker, const battle::Unit & victim, battle::DamageProvenance provenance)
{
	BattleAttackInfo strike(&attacker, &victim, 0, false);
	strike.attackerPos = attacker.getPosition();
	strike.defenderPos = victim.getPosition();
	strike.bracePreemptive = true;
	// NOT strike.reverse(): that would incorrectly enable Riposte retaliation.
	const auto range = battle.calculateDmgRange(strike);
	const auto requested = battle.battleExpectedLuckDamage(strike);
	const auto side = battle.playerToSide(battle.battleGetOwner(&victim));
	const bool validSide = side == BattleSide::ATTACKER || side == BattleSide::DEFENDER;
	auto victimState = victim.acquireState();
	const auto * hero = validSide ? battle.battleGetFightingHero(side) : nullptr;
	const bool eligible = validSide && newHorizonsArmorer::isEligiblePhysicalAttack(&attacker, true, false)
		&& newHorizonsArmorer::canTriggerLastStand(hero, &victim);
	const auto lastStand = newHorizonsArmorer::resolveLastStandDamage(requested,
		victimState->guardianSpiritHitPoints, victimState->guardianSpiritRoundsRemaining,
		victim.getAvailableHealth(), eligible, validSide && battle.getBattle()->armorerLastStandUsed(side));
	auto applied = lastStand.damageToApply;
	victimState->damage(applied, false, provenance);
	return {range, requested, lastStand.damageToApply, applied, victimState->getAvailableHealth(),
		victimState->guardianSpiritHitPoints, lastStand.triggered,
		lastStand.triggered && battle.getBattle()->getActiveStackID() == static_cast<int32_t>(victim.unitId())};
}
}

namespace heroOrderValue
{
BracePreemptiveForecast forecastBraceAtStage(const CBattleInfoCallback & battle,
	const BraceStageEntry & entry)
{
	BracePreemptiveForecast result;
	if(!battle.getBattle())
		return result;
	const auto * incoming = battle.battleGetUnitByID(entry.incomingAttackerId);
	const auto * braced = battle.battleGetUnitByID(entry.bracedDefenderId);
	if(!battle.battleCanTriggerHeroOrderBrace(incoming, braced,
		entry.voluntaryMovementDistance, entry.ranged, entry.counterDispatch))
		return result;
	result.event = BraceStageEvent{braced->unitId(), incoming->unitId(), braced->getPosition(),
		incoming->getPosition(), battle.battleGetRound(), entry.voluntaryMovementDistance,
		braced->getAvailableHealth(), incoming->getAvailableHealth(),
		newHorizonsCombatSkills::isPhysicalCreatureAttack(braced, true)
			? battle::DamageProvenance::PHYSICAL_CREATURE : battle::DamageProvenance::OTHER};
	if(newHorizonsFrozen::isFrozen(*incoming) || endedActivation(*incoming)
		|| newHorizonsFrozen::isFrozen(*braced) || endedActivation(*braced))
	{
		result.coverage = BraceStageCoverage::BLOCKED_AT_ENTRY;
		return result;
	}
	if(needsPreHitProjection(battle, *braced, *incoming))
	{
		result.coverage = BraceStageCoverage::NEEDS_PRE_HIT_PROJECTION;
		return result;
	}
	result.primary = forecastPrimary(battle, *braced, *incoming, result.event->damageProvenance);
	result.coverage = BraceStageCoverage::CONDITIONAL_PRIMARY_HIT;
	return result;
}
}
