/*
 * ProjectedUnitMagicResistance.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedUnitMagicResistance.h"
#include "../bonuses/ProjectedBonusRoster.h"
#include "../spells/NewHorizonsMagic.h"
#include <stdexcept>

battle::ProjectedUnitMagicResistance::ProjectedUnitMagicResistance(
	const ProjectedBonusRoster & roster, const JsonNode & magicRules)
	: roster(roster), magicRules(magicRules)
{
}

void battle::ProjectedUnitMagicResistance::requireSubject(const Unit & unit) const
{
	if(!roster.contains(unit))
		throw std::invalid_argument("Projected resistance subject is outside its roster");
}

battle::Units battle::ProjectedUnitMagicResistance::adjacentUnits(const Unit & unit) const
{
	const auto adjacent = roster.adjacentUnits(unit);
	return Units(adjacent.begin(), adjacent.end());
}

bool battle::ProjectedUnitMagicResistance::newHorizonsRulesActive() const
{
	return newHorizonsMagic::rulesActive(magicRules);
}
