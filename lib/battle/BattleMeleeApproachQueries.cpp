/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CBattleInfoCallback.h"
#include "BattleReach.h"
#include "BattleInfo.h"
#include "CObstacleInstance.h"
#include "Unit.h"
#include "../entities/building/TownFortifications.h"
#include <optional>
#include <stdexcept>

namespace
{
static std::optional<std::pair<BattleHex, BattleHex>> getLongWeaponLineHexes(const BattleHex & defenderHex, BattleHex::EDir direction)
{
	try
	{
		BattleHex middleHex = defenderHex.cloneInDirection(direction, false);
		BattleHex attackerHex = middleHex.cloneInDirection(direction, false);
		return std::make_pair(middleHex, attackerHex);
	}
	catch(const std::out_of_range &)
	{
		return std::nullopt;
	}
}

static bool isLongWeaponMiddleHexClear(const CBattleInfoCallback & callback, const BattleHex & middleHex)
{
	if(!middleHex.isValid())
		return false;

	const auto accessibility = callback.getAccessibility();
	return accessibility[middleHex.toInt()] == EAccessibility::ACCESSIBLE;
}
}

BattleHex CBattleInfoCallback::fromWhichHexAttack(const battle::Unit * attacker, const BattleHex & target,
	const BattleHex::EDir & direction, bool allowLongWeapon) const
{
	RETURN_IF_NOT_BATTLE(BattleHex::INVALID);
	const auto ordinary = ordinaryMeleeAttackSource(attacker, target, direction, allowLongWeapon);
	if(!attacker || !target.isValid() || direction == BattleHex::NONE) return ordinary;
	if(battleReach::maximumMeleeDistance(*attacker) <= 1) return ordinary;
	// Preserve a legacy Long Weapon strike from the actor's existing footprint.
	if(allowLongWeapon && attacker->hasBonusOfType(BonusType::LONG_WEAPON)
		&& ordinary == attacker->getPosition()) return ordinary;
	const auto cache = getReachability(attacker);
	const auto available = battleGetAvailableHexes(cache, attacker, false);
	const auto reach = findReachAttackSource(attacker, target, direction, available, &cache);
	if(reach.isValid()) return reach;
	if(!available.contains(ordinary)) return BattleHex::INVALID;
	if(ordinary != attacker->getPosition() && !attacker->hasBonusOfType(BonusType::FLYING))
	{
		const auto stoppers = getStoppers(cache.params.perspective);
		for(const auto & occupied : attacker->getHexes(ordinary))
			if(stoppers.contains(occupied)) return BattleHex::INVALID;
	}
	return ordinary;
}

BattleHex CBattleInfoCallback::ordinaryMeleeAttackSource(const battle::Unit * attacker, const BattleHex & target, const BattleHex::EDir & direction, bool allowLongWeapon) const
{
	RETURN_IF_NOT_BATTLE(BattleHex::INVALID);
	if (!attacker)
		throw std::runtime_error("Undefined attacker in fromWhichHexAttack!");

	if (!target.isValid() || direction == BattleHex::NONE)
		return BattleHex::INVALID;

	if(allowLongWeapon && attacker->hasBonusOfType(BonusType::LONG_WEAPON) && direction != BattleHex::TOP && direction != BattleHex::BOTTOM)
	{
		const auto longLine = getLongWeaponLineHexes(target, direction);
		if(longLine)
		{
			const auto [middleHex, longAttackFrom] = *longLine;
			if(attacker->coversPos(longAttackFrom) && isLongWeaponMiddleHexClear(*this, middleHex))
				return attacker->getPosition();
		}
	}


	bool isAttacker = attacker->unitSide() == BattleSide::ATTACKER;
	if (attacker->doubleWide())
	{
		if(allowLongWeapon && attacker->hasBonusOfType(BonusType::LONG_WEAPON) && direction != BattleHex::TOP && direction != BattleHex::BOTTOM)
		{
			const auto longLine = getLongWeaponLineHexes(target, direction);
			if(longLine)
			{
				const auto [middleHex, longAttackFrom] = *longLine;
				if(isLongWeaponMiddleHexClear(*this, middleHex))
				{
					const auto availableHexes = battleGetAvailableHexes(attacker, false);
					if(availableHexes.contains(longAttackFrom))
						return longAttackFrom;
				}
			}
		}

		// We need to find position of right hex of double-hex creature (or left for defending side)
		// | TOP_LEFT | TOP_RIGHT |  RIGHT  |BOTTOM_RIGHT|BOTTOM_LEFT|  LEFT   |  TOP   | BOTTOM
		// |  o o -   |    - o o  |  - -    |   - -      |    - -    |    - -  |  o o   |   - -
		// |   - x -  |   - x -   | - x o o |  - x -     |   - x -   | o o x - | - x -  |  - x -
		// |    - -   |    - -    |  - -    |   - o o    |  o o -    |    - -  |  - -   |   o o

		try
		{
			switch (direction)
			{
				case BattleHex::TOP_LEFT:
				case BattleHex::LEFT:
				case BattleHex::BOTTOM_LEFT:
					return target.cloneInDirection(direction, false)
						.cloneInDirection(isAttacker ? BattleHex::NONE : BattleHex::LEFT, false);

				case BattleHex::TOP_RIGHT:
				case BattleHex::RIGHT:
				case BattleHex::BOTTOM_RIGHT:
					return target.cloneInDirection(direction, false)
						.cloneInDirection(isAttacker ? BattleHex::RIGHT : BattleHex::NONE, false);

				case BattleHex::TOP:
					return target.cloneInDirection(isAttacker ? BattleHex::TOP_RIGHT : BattleHex::TOP_LEFT, false);

				case BattleHex::BOTTOM:
					return target.cloneInDirection(isAttacker ? BattleHex::BOTTOM_RIGHT : BattleHex::BOTTOM_LEFT, false);

				default:
					return BattleHex::INVALID;
			}
		}
		catch(const std::out_of_range &)
		{
			return BattleHex::INVALID;
		}
	}
	if (direction == BattleHex::TOP || direction == BattleHex::BOTTOM)
		return BattleHex::INVALID;

	BattleHex adjacentAttackFrom = BattleHex::INVALID;
	try
	{
		adjacentAttackFrom = target.cloneInDirection(direction, false);
	}
	catch(const std::out_of_range &)
	{
		return BattleHex::INVALID;
	}

	if(allowLongWeapon && attacker->hasBonusOfType(BonusType::LONG_WEAPON))
	{
		const auto longLine = getLongWeaponLineHexes(target, direction);
		if(!longLine)
			return adjacentAttackFrom;

		const auto [middleHex, longAttackFrom] = *longLine;

		if(isLongWeaponMiddleHexClear(*this, middleHex))
		{
			const auto availableHexes = battleGetAvailableHexes(attacker, false);
			const bool longReachable = availableHexes.contains(longAttackFrom);

			if(longReachable)
				return longAttackFrom;
		}
	}

	return adjacentAttackFrom;
}

bool CBattleInfoCallback::battleCanAttackHex(const battle::Unit * attacker, const BattleHex & position) const
{
	return battleCanAttackHex(battleGetAvailableHexes(attacker, false), attacker, position);
}

bool CBattleInfoCallback::battleCanAttackHex(const BattleHexArray & availableHexes, const battle::Unit * attacker, const BattleHex & position) const
{
	for (auto direction = 0; direction < 8; direction++)
	{
		if (battleCanAttackHex(availableHexes, attacker, position, BattleHex::EDir(direction)))
			return true;
	}
	return false;
}

bool CBattleInfoCallback::battleCanAttackHex(const battle::Unit * attacker, const BattleHex & position, const BattleHex::EDir & direction) const
{
	return battleCanAttackHex(battleGetAvailableHexes(attacker, false), attacker, position, direction);
}

bool CBattleInfoCallback::battleCanAttackHex(const BattleHexArray & availableHexes, const battle::Unit * attacker, const BattleHex & position, const BattleHex::EDir & direction) const
{
	RETURN_IF_NOT_BATTLE(false);

	if (!attacker)
		throw std::runtime_error("Undefined attacker in battleCanAttackHex!");

	if (!position.isValid() || direction == BattleHex::NONE)
		return false;

	const auto canAttackFrom = [&](BattleHex fromHex)
	{
		//check if the attack is performed from an available hex
		if (!fromHex.isValid() || !availableHexes.contains(fromHex))
			return false;

		//if the movement ends in an obstacle, check if the obstacle allows attacking from that position
		if (attacker->getPosition() != fromHex)
		{
			if (!attacker->hasBonusOfType(BonusType::FLYING))
			{
				for (const auto & obstacle : battleGetAllObstacles())
				{
					if (obstacle->getStoppingTile().contains(fromHex))
						return false;
					if (attacker->doubleWide() && obstacle->getStoppingTile().contains(attacker->occupiedHex(fromHex)))
						return false;
				}
			}
			const battle::Unit * defender = battleGetUnitByPos(position, false); //Do not allow to target corpses when standing on them (a WALK_AND_SPELLCAST action)
			if (defender && defender->isDead() && defender->coversPos(fromHex))
				return false;
		}

		return true;
	};

	if(findReachAttackSource(attacker, position, direction, availableHexes, nullptr).isValid())
		return true;

	BattleHex fromHex = ordinaryMeleeAttackSource(attacker, position, direction, true);
	if (canAttackFrom(fromHex))
		return true;

	if(attacker->hasBonusOfType(BonusType::LONG_WEAPON) && direction != BattleHex::TOP && direction != BattleHex::BOTTOM)
	{
		const auto longLine = getLongWeaponLineHexes(position, direction);
		if(!longLine)
			return false;

		const auto [middleHex, longAttackFrom] = *longLine;

		if (isLongWeaponMiddleHexClear(*this, middleHex) && canAttackFrom(longAttackFrom))
			return true;
	}

	return false;
}

bool CBattleInfoCallback::isLongWeaponAttack(const battle::Unit * attacker, const battle::Unit * defender) const
{
	return isLongWeaponAttack(attacker, defender, BattleHex::INVALID);
}

bool CBattleInfoCallback::isLongWeaponAttack(const battle::Unit * attacker, const battle::Unit * defender,
	const BattleHex & attackerPosition) const
{
	RETURN_IF_NOT_BATTLE(false);

	if(!attacker)
		throw std::runtime_error("Undefined attacker in isLongWeaponAttack!");
	if(!defender)
		throw std::runtime_error("Undefined defender in isLongWeaponAttack!");

	if(!attacker->hasBonusOfType(BonusType::LONG_WEAPON))
		return false;

	if(isMeleeAttackPossible(attacker, defender, attackerPosition))
		return false;

	const auto projectedHexes = attackerPosition.isValid()
		? attacker->getHexes(attackerPosition) : attacker->getHexes();
	auto accessibility = getAccessibility();
	if(attackerPosition.isValid() && attackerPosition != attacker->getPosition())
	{
		for(const auto & originalHex : attacker->getHexes())
		{
			if(!originalHex.isAvailable() || projectedHexes.contains(originalHex)
				|| accessibility[originalHex.toInt()] != EAccessibility::ALIVE_STACK
				|| accessibility.isDemonicGateReserved(originalHex))
				continue;
			// Stacks overwrite the gate's base accessibility. Moving the actor
			// must not turn a closed or blocked gate into an open attack corridor.
			if((originalHex == BattleHex::GATE_OUTER || originalHex == BattleHex::GATE_INNER)
				&& battleGetFortifications().wallsHealth > 0
				&& (battleGetGateState() == EGateState::CLOSED || battleGetGateState() == EGateState::BLOCKED))
				continue;
			accessibility[originalHex.toInt()] = EAccessibility::ACCESSIBLE;
		}
	}

	for(const BattleHex & defenderHex : defender->getHexes())
	{
		for(int direction = 0; direction < 6; ++direction)
		{
			const auto longLine = getLongWeaponLineHexes(defenderHex, static_cast<BattleHex::EDir>(direction));
			if(!longLine)
				continue;

			const auto [middleHex, attackerHex] = *longLine;
			if(projectedHexes.contains(attackerHex) && middleHex.isValid()
				&& !projectedHexes.contains(middleHex)
				&& accessibility[middleHex.toInt()] == EAccessibility::ACCESSIBLE)
				return true;
		}
	}

	return false;
}
