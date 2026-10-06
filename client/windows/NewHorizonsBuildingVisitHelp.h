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
/// Returns authored per-hero rewardable state text without changing town or hero state.
inline MetaString heroVisitStatus(const TownRewardableBuildingInstance & building, const CGHeroInstance * hero)
{
	if(!hero || building.configuration.visitMode != Rewardable::VISIT_HERO)
		return {};

	const auto & status = building.wasVisited(hero)
		? building.configuration.visitedTooltip
		: building.configuration.notVisitedTooltip;

	return status.empty() ? MetaString{} : status;
}
}
