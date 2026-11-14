/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleReach.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "../bonuses/IBonusBearer.h"
#include <algorithm>
#include <limits>

namespace battleReach
{
int64_t maximumMeleeDistance(int64_t netGap)
{
	if(netGap <= 0) return 1;
	return netGap == std::numeric_limits<int64_t>::max() ? netGap : netGap + 1;
}

int64_t maximumMeleeDistance(const IBonusBearer & bearer)
{
	return maximumMeleeDistance(static_cast<int64_t>(bearer.valOfBonuses(BonusType::LONG_REACH)));
}
}

namespace
{
bool clearReachApproach(const CBattleInfoCallback & callback, const battle::Unit & attacker,
	const battle::Unit & defender, BattleHex attackerPosition, BattleHex defenderPosition,
	BattleHex::EDir approach = BattleHex::NONE)
{
	const auto maximumDistance = battleReach::maximumMeleeDistance(attacker);
	if(maximumDistance <= 1 || (approach != BattleHex::NONE
		&& (approach < BattleHex::TOP_LEFT || approach > BattleHex::LEFT))) return false;
	const auto attackerHexes = attacker.getHexes(attackerPosition);
	const auto defenderHexes = defender.getHexes(defenderPosition);
	for(const auto & occupied : attackerHexes)
		if(defenderHexes.contains(occupied)) return false;
	const auto accessibility = callback.getAccessibility();
	BattleHexArray endpointFootprints = attackerHexes;
	endpointFootprints.insert(defenderHexes);
	// Projected movement vacates live footprints. Preserve the existing ray
	// solver's endpoint exceptions rather than treating those as new blockers.
	endpointFootprints.insert(attacker.getHexes());
	endpointFootprints.insert(defender.getHexes());
	for(const auto & from : attackerHexes)
	{
		if(!from.isAvailable()) continue;
		for(const auto & to : defenderHexes)
		{
			if(!to.isAvailable()) continue;
			const auto distance = BattleHex::getDistance(from, to);
			if(distance > 1 && distance <= maximumDistance
				&& accessibility.hasClearStraightHexRay(from, to, endpointFootprints, approach, defenderHexes)) return true;
		}
	}
	return false;
}
}

bool CBattleInfoCallback::isMeleeAttackPossibleWithLongReach(const battle::Unit * attacker, const battle::Unit * defender,
	const BattleHex & attackerPosition, const BattleHex & defenderPosition) const
{
	if(!attacker || !defender || defender->isInvincible()) return false;
	if(isMeleeAttackPossible(attacker, defender, attackerPosition, defenderPosition)) return true;
	return clearReachApproach(*this, *attacker, *defender,
		attackerPosition.isValid() ? attackerPosition : attacker->getPosition(),
		defenderPosition.isValid() ? defenderPosition : defender->getPosition());
}

BattleHex CBattleInfoCallback::findReachAttackSource(const battle::Unit * attacker, const BattleHex & target,
	BattleHex::EDir direction, const BattleHexArray & availableHexes, const ReachabilityInfo * cache) const
{
	RETURN_IF_NOT_BATTLE(BattleHex::INVALID);
	// TOP/BOTTOM are presentation aliases for ordinary doublewide adjacency,
	// not physical ray edges. Never invent a remote Reach source for them.
	if(!attacker || direction < BattleHex::TOP_LEFT || direction > BattleHex::LEFT
		|| battleReach::maximumMeleeDistance(*attacker) <= 1) return BattleHex::INVALID;
	const auto * defender = battleGetUnitByPos(target, false);
	if(!defender || !defender->alive() || defender->isDead() || defender->isInvincible()
		|| isMeleeAttackPossible(attacker, defender)) return BattleHex::INVALID;
	const auto perspective = cache ? cache->params.perspective
		: (battleDoWeKnowAbout(attacker->unitSide()) ? attacker->unitSide() : battleGetMySide());
	const auto stoppingHexes = getStoppers(perspective);
	const bool flying = attacker->hasBonusOfType(BonusType::FLYING);
	BattleHex best = BattleHex::INVALID;
	uint32_t bestCost = ReachabilityInfo::INFINITE_DIST;
	for(const auto & candidate : availableHexes)
	{
		if(!candidate.isAvailable()) continue;
		if(candidate != attacker->getPosition() && !flying)
		{
			bool stopped = false;
			for(const auto & occupied : attacker->getHexes(candidate))
				stopped |= stoppingHexes.contains(occupied);
			if(stopped) continue;
		}
		if(isMeleeAttackPossible(attacker, defender, candidate, defender->getPosition())
			|| !clearReachApproach(*this, *attacker, *defender, candidate, defender->getPosition(), direction)) continue;
		if(!cache) return candidate; // Availability only, no extra solver pass.
		const auto cost = cache->distances[candidate.toInt()];
		if(cost < bestCost)
		{
			best = candidate;
			bestCost = cost;
		}
	}
	return best;
}
