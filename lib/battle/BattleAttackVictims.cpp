/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CBattleInfoCallback.h"
#include "NewHorizonsSylvanTacticsV2.h"
#include "../CStack.h"

battle::Units CBattleInfoCallback::getAttackedBattleUnits(
	const battle::Unit * attacker,
	const  battle::Unit * defender,
	BattleHex destinationTile,
	bool rangedAttack,
	BattleHex attackerPos,
	BattleHex defenderPos) const
{
	battle::Units units;
	RETURN_IF_NOT_BATTLE(units);

	if(attackerPos == BattleHex::INVALID)
		attackerPos = attacker->getPosition();

	if(defenderPos == BattleHex::INVALID)
		defenderPos = defender->getPosition();

	AttackableTiles at;

	if (rangedAttack)
		at = getPotentiallyShootableHexes(attacker, destinationTile, attackerPos);
	else
		at = getPotentiallyAttackableHexes(attacker, defender, destinationTile, attackerPos, defenderPos);

	units = battleGetUnitsIf([this, at, attacker, rangedAttack](const battle::Unit * unit)
	{
		if (unit->isGhost() || !unit->alive() || unit->isInvincible())
			return false;

		for (const BattleHex & hex : unit->getHexes())
		{
			// Match authoritative getAttackedCreatures: hostile-only splash must
			// use current control, while friendly-fire tiles can hit either side.
			if (at.hostileCreaturePositions.contains(hex) && battleGetOwner(attacker) != battleGetOwner(unit))
				return true;
			if (at.friendlyCreaturePositions.contains(hex)
				&& (rangedAttack || !newHorizonsSylvanTacticsV2::spareFriendlyBreath(*this, attacker, unit)))
				return true;
		}
		return false;
	});

	return units;
}

std::pair<battle::Units, bool> CBattleInfoCallback::getAttackedCreatures(const CStack* attacker, const BattleHex & destinationTile, bool rangedAttack, BattleHex attackerPos) const
{
	std::pair<battle::Units, bool> attackedCres;
	RETURN_IF_NOT_BATTLE(attackedCres);

	AttackableTiles at;

	if(rangedAttack)
		at = getPotentiallyShootableHexes(attacker, destinationTile, attackerPos);
	else
	{
		try
		{
			at = getPotentiallyAttackableHexes(attacker, destinationTile, attackerPos);
		}
		catch(const std::runtime_error &)
		{
			return attackedCres;
		}
	}

	// a double-wide unit is found through both of its hexes, so the same unit shows up twice
	const auto & addOnce = [&attackedCres](const battle::Unit * unit)
	{
		if(!vstd::contains(attackedCres.first, unit))
			attackedCres.first.push_back(unit);
	};

	for (const BattleHex & tile : at.hostileCreaturePositions) //all around & three-headed attack
	{
		const CStack * st = battleGetStackByPos(tile, true);
		if(st && battleGetOwner(st) != battleGetOwner(attacker) && !st->isInvincible()) //only hostile stacks - does it work well with Berserk?
			addOnce(st);
	}
	for (const BattleHex & tile : at.friendlyCreaturePositions)
	{
		const CStack * st = battleGetStackByPos(tile, true);
		if(st && !st->isInvincible() && (rangedAttack
			|| !newHorizonsSylvanTacticsV2::spareFriendlyBreath(*this, attacker, st)))
			addOnce(st);
	}

	if (at.friendlyCreaturePositions.empty())
	{
		attackedCres.second = !attackedCres.first.empty();
	}
	else
	{
		for (const BattleHex & tile : at.friendlyCreaturePositions)
			for (const auto & st : attackedCres.first)
				if (st->coversPos(tile))
					attackedCres.second = true;
	}

	return attackedCres;
}
