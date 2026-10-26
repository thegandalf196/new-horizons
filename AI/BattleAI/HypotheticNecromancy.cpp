/*
 * HypotheticNecromancy.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in the main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/NewHorizonsNecromancyCombat.h"

void HypotheticBattle::captureNecromancyPassives(const CBattleInfoCallback & source)
{
	if(const auto * parent = dynamic_cast<const HypotheticBattle *>(&source))
	{
		necromancyPassives = parent->necromancyPassives;
		return;
	}
	const auto observer = source.battleGetMySide();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const bool known = observer == BattleSide::ALL_KNOWING || observer == side;
		necromancyPassives.at(side) = newHorizonsNecromancy::captureCombatPassives(
			known ? source.battleGetFightingHero(side) : nullptr);
	}
}

newHorizonsNecromancy::CombatPassives HypotheticBattle::unitNecromancyPassives(const battle::Unit * unit) const
{
	if(!unit)
		return {};
	const auto side = playerToSide(battleGetOwner(unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return {};
	return newHorizonsNecromancy::passivesForCreature(necromancyPassives.at(side), unit->creatureId());
}
