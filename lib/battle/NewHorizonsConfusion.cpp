/*
 * NewHorizonsConfusion.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsConfusion.h"

#include "ReachabilityInfo.h"
#include "CObstacleInstance.h"
#include "Unit.h"

#include <algorithm>

namespace newHorizonsConfusion
{
Choices enumerateChoices(const CBattleInfoCallback & battle, const battle::Unit * unit)
{
	Choices result;
	if(!battle.getBattle() || !unit || !unit->alive() || unit->isGhost()
		|| !unit->getPosition().isAvailable() || !unit->canMove() || battle.battleTacticDist())
		return result;

	const auto reachability = battle.getReachability(unit);
	const auto available = battle.battleGetAvailableHexes(reachability, unit, false);
	for(const auto hex : available)
	{
		if(hex.isAvailable() && hex != unit->getPosition())
			result.wanderDestinations.push_back(hex);
	}
	result.wanderFallsBackToDefend = result.wanderDestinations.empty();
	BattleHexArray stoppingHexes;
	for(const auto & obstacle : battle.battleGetAllObstacles(reachability.params.perspective))
	{
		if(!battle.battleIsObstacleVisibleForSide(*obstacle, reachability.params.perspective))
			continue;
		for(const auto hex : obstacle->getStoppingTile())
		{
			if(hex == BattleHex::GATE_BRIDGE && obstacle->obstacleType == CObstacleInstance::MOAT
				&& (battle.battleGetGateState() == EGateState::OPENED || battle.battleGetGateState() == EGateState::DESTROYED))
				continue;
			stoppingHexes.insert(hex);
		}
	}
	const auto endsOnStoppingHazard = [&](BattleHex position)
	{
		if(position == unit->getPosition() || unit->hasBonusOfType(BonusType::FLYING))
			return false;
		for(const auto hex : unit->getHexes(position))
		{
			if(stoppingHexes.contains(hex))
				return true;
		}
		return false;
	};

	for(const auto * target : battle.battleGetAllUnits(false))
	{
		if(!target || target == unit || !target->isValidTarget(false)
			|| !battle.battleMatchActionController(unit, target, false))
			continue;
		const bool meleeAllowed = battle.battleCanAttackUnitAction(unit, target);
		const bool shootAllowed = battle.battleCanShootAction(unit, target->getPosition());
		const auto skirmisherPositions = battle.battleGetSkirmisherAttackFromHexes(unit, target->getPosition());
		if(!meleeAllowed && !shootAllowed && skirmisherPositions.empty())
			continue;

		AttackTargetChoices group;
		group.targetId = target->unitId();
		if(shootAllowed)
			group.attacks.push_back({{EActionType::SHOOT, unit->getPosition(), target}, false});
		for(const auto hex : skirmisherPositions)
		{
			if(!endsOnStoppingHazard(hex))
				group.attacks.push_back({{EActionType::SHOOT, hex, target}, true});
		}
		if(meleeAllowed)
		{
			for(const auto hex : available)
			{
				if(endsOnStoppingHazard(hex))
					continue;
				if(battle.isMeleeAttackPossibleWithLongReach(unit, target, hex)
					|| battle.isLongWeaponAttack(unit, target, hex))
					group.attacks.push_back({{EActionType::WALK_AND_ATTACK, hex, target}, false});
			}
		}

		if(group.attacks.empty())
		{
			// Approach a normally reachable attack anchor using the same shared
			// geometry as existing forced movement. If every anchor is blocked,
			// the helper's ordinary blocked-target approach remains available.
			BattleHex approach = target->getPosition();
			uint32_t closestDistance = ReachabilityInfo::INFINITE_DIST;
			for(const auto hex : target->getAttackableHexes(unit))
			{
				if(reachability.isReachable(hex) && reachability.distances[hex.toInt()] < closestDistance)
				{
					closestDistance = reachability.distances[hex.toInt()];
					approach = hex;
				}
			}
			auto advance = battle.getClosestHexToTargetInRange(reachability, *unit, approach);
			if(advance.isAvailable() && !available.contains(advance))
			{
				// The existing approach helper indexes its path by Speed. Where
				// ordinary movement costs more than one per hex, retain that exact
				// route but stop at its furthest legal, budgeted endpoint.
				advance = BattleHex::INVALID;
				const auto path = battle.getPath(unit->getPosition(), approach, unit).first;
				for(const auto hex : path)
				{
					if(available.contains(hex) && hex != unit->getPosition())
					{
						advance = hex;
						break;
					}
				}
			}
			if(advance.isAvailable() && advance != unit->getPosition() && available.contains(advance))
				group.furthestAdvances.push_back(advance);
			group.zeroAdvanceUnresolved = group.furthestAdvances.empty();
		}
		result.attacks.push_back(std::move(group));
	}
	// Stable enumeration supports deterministic fixtures and detached consumers;
	// it does not change the later authoritative uniform target draw.
	std::ranges::sort(result.attacks, {}, &AttackTargetChoices::targetId);
	return result;
}
}
