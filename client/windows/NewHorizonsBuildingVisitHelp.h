/*
 * NewHorizonsBuildingVisitHelp.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#pragma once

#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/TownBuildingInstance.h"

namespace newHorizonsBuildingVisitHelp
{
/// Returns authored town-rewardable state text without changing town or hero state.
inline MetaString heroVisitStatus(const TownRewardableBuildingInstance & building, const CGHeroInstance * hero)
{
	bool visited = false;
	switch(building.configuration.visitMode)
	{
	case Rewardable::VISIT_HERO:
		if(!hero)
			return {};
		visited = building.wasVisited(hero);
		break;
	case Rewardable::VISIT_ONCE:
		// VISIT_ONCE tracks the building's visitor set and ignores its hero argument.
		visited = building.wasVisited(nullptr);
		break;
	default:
		return {};
	}

	const auto & status = visited
		? building.configuration.visitedTooltip
		: building.configuration.notVisitedTooltip;

	return status.empty() ? MetaString{} : status;
}
}
