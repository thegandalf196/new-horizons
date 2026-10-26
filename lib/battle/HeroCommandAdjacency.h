/*
 * HeroCommandAdjacency.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "Unit.h"
namespace heroCommands
{
inline bool orderUnitsAdjacent(const battle::Unit * first, const battle::Unit * second,
	const BattleHex & firstPosition = BattleHex::INVALID, const BattleHex & secondPosition = BattleHex::INVALID)
{
	if(!first || !second)
		return false;
	const auto & firstHexes = firstPosition.isValid() ? first->getHexes(firstPosition) : first->getHexes();
	const auto & secondHexes = secondPosition.isValid() ? second->getHexes(secondPosition) : second->getHexes();
	for(const auto & firstHex : firstHexes)
	{
		if(!firstHex.isValid())
			continue;
		for(const auto & secondHex : secondHexes)
			if(secondHex.isValid() && BattleHex::getDistance(firstHex, secondHex) == 1)
				return true;
	}
	return false;
}

}
