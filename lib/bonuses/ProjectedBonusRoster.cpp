/*
 * ProjectedBonusRoster.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBonusRoster.h"
#include "../battle/UnitAdjacency.h"
#include <set>
#include <stdexcept>

ProjectedBonusRoster::ProjectedBonusRoster(std::vector<const battle::Unit *> values)
	: units(std::move(values))
{
	std::set<uint32_t> identities;
	for(const auto * unit : units)
		if(!unit || !identities.insert(unit->unitId()).second)
			throw std::invalid_argument("Projected bonus roster has null or duplicate units");
}

bool ProjectedBonusRoster::contains(const battle::Unit & unit) const
{
	return std::find(units.begin(), units.end(), &unit) != units.end();
}

std::vector<const battle::Unit *> ProjectedBonusRoster::adjacentUnits(const battle::Unit & unit) const
{
	if(!contains(unit))
		throw std::invalid_argument("Projected bonus subject is outside its roster");
	const auto & hexes = unit.getSurroundingHexes();
	std::vector<const battle::Unit *> result;
	for(const auto * candidate : units)
		if(battle::isAdjacentLivingUnit(hexes, *candidate))
			result.push_back(candidate);
	return result;
}
