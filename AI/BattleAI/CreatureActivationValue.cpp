/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CreatureActivationValue.h"
#include "AttackPossibility.h"
#include "PotentialTargets.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/ReachabilityInfo.h"

namespace creatureActivationValue
{
float bestLegalActionValue(const battle::Unit * unit, DamageCache & cache, const std::shared_ptr<HypotheticBattle> & state)
{
	PotentialTargets actions(unit, cache, state);
	return std::max(0.0f, static_cast<float>(actions.bestActionValue()));
}

float expectedTargetActivationValue(const battle::Unit * target,
	DamageCache & damageCache, const std::shared_ptr<CBattleInfoCallback> & projectedBattle, bool effectiveAllegiance,
	const ReachabilityInfo * distances, int radius)
{
	if(!target || !target->alive() || target->getCount() <= 0
		|| target->isGhost() || target->isTurret() || !projectedBattle)
		return 0.0f;

	float bestActionValue = 0.0f;
	for(const auto * friendly : projectedBattle->battleGetAllUnits(false))
	{
		if(!friendly || !friendly->alive() || !friendly->isValidTarget(true)
			|| friendly->isGhost() || friendly->isTurret()
			|| (effectiveAllegiance
				? projectedBattle->battleGetOwner(friendly) == projectedBattle->battleGetOwner(target)
				: friendly->unitSide() == target->unitSide()))
			continue;

		if(!projectedBattle->battleCanAttackUnitAction(target, friendly)
			&& !projectedBattle->battleCanShootAction(target, friendly->getPosition()))
			continue;
		const bool shooting = projectedBattle->battleCanShoot(target, friendly->getPosition());
		if(distances && !shooting
			&& !projectedBattle->isMeleeAttackPossibleWithLongReach(target, friendly, target->getPosition()))
		{
			bool reachable = false;
			for(const auto & cell : friendly->getHexes())
				for(const auto & neighbor : cell.getNeighbouringTiles())
					if(distances->isReachable(neighbor) && radius >= 0
						&& distances->distances.at(neighbor.toInt()) <= static_cast<uint32_t>(radius)) reachable = true;
			if(!reachable) continue;
		}
		const int attackCount = AttackPossibility::getAttackCount(*target, shooting, *projectedBattle);
		if(attackCount <= 0)
			continue;

		const auto perAttackDamage = damageCache.getDamage(target, friendly, projectedBattle);
		const auto availableHealth = std::max<int64_t>(0, friendly->getAvailableHealth());
		const auto cappedPerAttackDamage = std::min(perAttackDamage, availableHealth);
		const auto attackDamage = std::min(availableHealth,
			cappedPerAttackDamage * static_cast<int64_t>(attackCount));
		if(attackDamage <= 0)
			continue;

		bestActionValue = std::max(bestActionValue, static_cast<float>(AttackPossibility::calculateDamageReduce(
			nullptr, friendly, static_cast<uint64_t>(attackDamage), damageCache, projectedBattle)));
	}

	// Forecast the expected value of one activation after the same source-aware
	// Fearless query used by the server's turn-start fear roll. This is a nominal
	// chance estimate; seeded combat-ability bias and Twist of Fate history are
	// not projected here.
	const int fearChance = target->hasBonusOfType(BonusType::FEARFUL)
		? std::clamp(projectedBattle->battleGetFearChance(target), 0, 100)
		: 0;
	return bestActionValue * static_cast<float>(100 - fearChance) / 100.0f;
}
}
