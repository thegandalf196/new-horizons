/*
 * NewHorizonsBuildingVisitHelp.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#pragma once

#include <cstdint>

#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/TownBuildingInstance.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/texts/MetaString.h"

namespace newHorizonsBuildingVisitHelp
{
/// Returns authored town-rewardable state text without changing town or hero state.
inline MetaString heroVisitStatus(const TownRewardableBuildingInstance & building, const CGHeroInstance * hero)
{
	if(building.configuration.visitMode == Rewardable::VISIT_UNLIMITED)
	{
		if(!hero || !hero->areSpellPointsInitialized()
			|| !newHorizonsMagic::spellPointRulesActive(hero->getMagicRules())
			|| building.configuration.notVisitedTooltip.empty())
			return {};

		MetaString status = building.configuration.notVisitedTooltip;
		const int32_t normal = hero->getNormalSpellPoints();
		const int32_t maximum = hero->manaLimit();
		const int32_t buffer = hero->getBufferSpellPoints();
		const int64_t restored = maximum > normal ? static_cast<int64_t>(maximum) - normal : 0;
		status.replaceTokenNumber("%NORMAL%", normal);
		status.replaceTokenNumber("%MAXIMUM%", maximum);
		status.replaceTokenNumber("%RESTORED%", restored);
		status.replaceTokenNumber("%BUFFER%", buffer);
		return status;
	}

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
