/*
 * AttackDamageValue.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "AttackPossibility.h"
#include "../../lib/battle/CBattleInfoCallback.h"

float hpFunction(uint64_t unitHealthStart, uint64_t unitHealthEnd, uint64_t maxHealth)
{
	float ratioStart = static_cast<float>(unitHealthStart) / maxHealth;
	float ratioEnd = static_cast<float>(unitHealthEnd) / maxHealth;
	float base = 0.666666f;

	// reduce from max to 0 must be 1.
	// 10 hp from end costs bit more than 10 hp from start because our goal is to kill unit, not just hurt it
	// ********** 2 * base - ratioStart *********
	// *                                                              *
	// *        height = ratioStart - ratioEnd         *
	// *                                                                  *
	// ******************** 2 * base - ratioEnd ******
	// S = (a + b) * h / 2
	return (base * (4 - ratioStart - ratioEnd)) * (ratioStart - ratioEnd) / 2 ;
}

/// <summary>
/// How enemy damage will be reduced by this attack
/// Half bounty for kill, half for making damage equal to enemy health
/// Bounty - the killed creature average damage calculated against attacker
/// </summary>
float AttackPossibility::calculateDamageReduce(
	const battle::Unit * attacker,
	const battle::Unit * defender,
	uint64_t damageDealt,
	DamageCache & damageCache,
	std::shared_ptr<CBattleInfoCallback> state)
{
	return calculateDamageReduce(attacker, defender, damageDealt, damageCache, std::move(state), nullptr);
}

float AttackPossibility::calculateDamageReduce(
	const battle::Unit * attacker,
	const battle::Unit * defender,
	uint64_t damageDealt,
	DamageCache & damageCache,
	std::shared_ptr<CBattleInfoCallback> state,
	DamageCacheProbeTiming * timing)
{
	const float HEALTH_BOUNTY = 0.5;
	const float KILL_BOUNTY = 0.5;

	// FIXME: provide distance info for Jousting bonus
	auto attackerUnitForMeasurement = attacker;

	if(!attackerUnitForMeasurement || attackerUnitForMeasurement->isTurret())
	{
		auto ourUnits = state->battleGetUnitsIf([&](const battle::Unit * u) -> bool
			{
				return state->battleGetOwner(u) != state->battleGetOwner(defender)
					&& !u->isTurret()
					&& !u->isCatapult()
					&& !u->isBallista()
					&& !u->isFirstAidTent()
					&& u->getCount();
			});

		if(ourUnits.empty())
			attackerUnitForMeasurement = defender;
		else
			attackerUnitForMeasurement = ourUnits.front();
	}

	// Phantom Army has one stack-wide Integrity pool, not ordinary per-creature
	// health. Losing Integrity does not reduce the copied stack's offensive
	// count; only exhausting the pool removes the full copied stack.
	if(defender->getPhantomInitialIntegrity() > 0)
	{
		const auto integrity = defender->getPhantomIntegrity();
		if(integrity <= 0 || defender->getCount() <= 0
			|| damageDealt < static_cast<uint64_t>(integrity))
			return 0.0f;

		const auto copiedStackDamage = damageCache.getOriginalDamage(defender, attackerUnitForMeasurement, state, timing);
		return static_cast<float>(copiedStackDamage);
	}

	const auto * capacityTrackedDefender = dynamic_cast<const battle::CUnitState *>(defender);
	const auto capacityReferenceMax = capacityTrackedDefender
		? capacityTrackedDefender->getCapacityHealthReferenceMax() : 0;
	const auto maxHealth = defender->getMaxHealth();
	if(capacityTrackedDefender && capacityTrackedDefender->health.isCapacityHealthTracking())
	{
		// A capacity change can leave survivors with different current HP, even
		// after the effect expires. The first-creature-plus-full-rear-units formula
		// cannot represent those cohorts, so apply the projected damage to a
		// detached copy and use its actual health/count deltas.
		const auto damageProjection = defender->acquireState();
		if(!damageProjection || damageProjection->getCount() <= 0)
			return 0.0f;

		const auto healthBefore = damageProjection->getAvailableHealth();
		const auto countBefore = damageProjection->getCount();
		damageDealt = std::min<uint64_t>(damageDealt, static_cast<uint64_t>(std::max<int64_t>(0, healthBefore)));
		int64_t projectedDamage = static_cast<int64_t>(damageDealt);
		damageProjection->damage(projectedDamage);

		const auto healthAfter = damageProjection->getAvailableHealth();
		const auto actualDamage = std::max<int64_t>(0, healthBefore - healthAfter);
		const auto enemiesKilled = std::max(0, countBefore - damageProjection->getCount());
		const auto enemyDamageBeforeAttack = damageCache.getOriginalDamage(defender, attackerUnitForMeasurement, state, timing);
		const auto damagePerEnemy = enemyDamageBeforeAttack / static_cast<double>(countBefore);
		const auto woundHealth = static_cast<float>(actualDamage) / std::max(1, capacityReferenceMax);
		const auto hpValue = std::min(woundHealth, static_cast<float>(countBefore));

		return static_cast<float>(damagePerEnemy
			* (enemiesKilled * KILL_BOUNTY + hpValue * HEALTH_BOUNTY));
	}

	auto availableHealth = defender->getFirstHPleft() + ((defender->getCount() - 1) * maxHealth);

	vstd::amin(damageDealt, availableHealth);

	auto enemyDamageBeforeAttack = damageCache.getOriginalDamage(defender, attackerUnitForMeasurement, state, timing);
	auto enemiesKilled = damageDealt / maxHealth + (damageDealt % maxHealth >= defender->getFirstHPleft() ? 1 : 0);
	auto damagePerEnemy = enemyDamageBeforeAttack / (double)defender->getCount();
	auto exceedingDamage = (damageDealt % maxHealth);
	float hpValue = (damageDealt / maxHealth);

	if(defender->getFirstHPleft() >= exceedingDamage)
	{
		hpValue += hpFunction(defender->getFirstHPleft(), defender->getFirstHPleft() - exceedingDamage, maxHealth);
	}
	else
	{
		hpValue += hpFunction(defender->getFirstHPleft(), 0, maxHealth);
		hpValue += hpFunction(maxHealth, maxHealth + defender->getFirstHPleft() - exceedingDamage, maxHealth);
	}

	return damagePerEnemy * (enemiesKilled * KILL_BOUNTY + hpValue * HEALTH_BOUNTY);
}

