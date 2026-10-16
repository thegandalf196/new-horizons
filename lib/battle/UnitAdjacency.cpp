/*
 * UnitAdjacency.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "UnitAdjacency.h"
#include "Unit.h"

bool battle::isAdjacentLivingUnit(const BattleHexArray & hexes, const Unit & candidate)
{
	if(!candidate.alive())
		return false;
	const auto & unitHexes = candidate.getHexes();
	for(const auto & hex : unitHexes)
		if(hexes.contains(hex))
			return true;
	return false;
}
