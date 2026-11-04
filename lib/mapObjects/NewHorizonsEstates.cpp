/*
 * Part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsEstates.h"

#include "CGHeroInstance.h"
#include "CGTownInstance.h"
#include "../CPlayerState.h"
#include <limits>

namespace newHorizonsEstates
{
namespace
{
TResource charged(int64_t base, int remainingPercent)
{
	if(base < 0)
		throw std::runtime_error("Negative recruitment or construction price");
	const int64_t result = base / 100 * remainingPercent
		+ (base % 100 * remainingPercent + 99) / 100;
	if(result > std::numeric_limits<TResource>::max())
		throw std::overflow_error("Recruitment or construction price overflow");
	return static_cast<TResource>(result);
}
}

bool residentHasPerk(const CGTownInstance & town, const CGHeroInstance * hero, const char * perk)
{
	return town.getOwner().isValidPlayer() && hero && hero->getVisitedTown() == &town
		&& hero->getOwner() == town.getOwner() && hero->hasActivePerk("new-horizons:estates", perk);
}

bool townHasPerk(const CGTownInstance & town, const char * perk)
{
	return residentHasPerk(town, town.getVisitingHero(), perk)
		|| residentHasPerk(town, town.getGarrisonHero(), perk);
}

const CGHeroInstance * recruitmentBuyer(const CArmedInstance * recipient)
{
	return dynamic_cast<const CGHeroInstance *>(recipient);
}

ResourceSet recruitmentCost(const ResourceSet & unitCost, int32_t amount, bool conscription)
{
	if(amount < 0)
		throw std::runtime_error("Negative recruitment quantity");
	ResourceSet result;
	for(size_t resource = 0; resource < unitCost.size(); ++resource)
		result[resource] = charged(static_cast<int64_t>(unitCost[resource]) * amount, conscription ? 90 : 100);
	return result;
}

int32_t affordableRecruitment(const ResourceSet & unitCost, const ResourceSet & resources,
	int32_t available, bool conscription)
{
	if(available <= 0)
		return 0;
	int64_t result = available;
	for(size_t resource = 0; resource < unitCost.size(); ++resource)
	{
		if(unitCost[resource] < 0)
			throw std::runtime_error("Negative recruitment price");
		if(unitCost[resource] == 0)
			continue;
		// ceil(base * n * p / 100) <= treasury iff base * n * p <= treasury * 100.
		const int64_t budget = std::max<int64_t>(0, resources[resource]);
		const int64_t maximumQuoted = std::min<int64_t>(budget, std::numeric_limits<TResource>::max());
		result = std::min(result, maximumQuoted * 100 / (static_cast<int64_t>(unitCost[resource]) * (conscription ? 90 : 100)));
	}
	return static_cast<int32_t>(result);
}

ResourceSet buildingCost(const ResourceSet & baseCost, bool architect)
{
	ResourceSet result = baseCost;
	if(architect)
		for(const auto resource : {EGameResID::GOLD, EGameResID::WOOD, EGameResID::ORE})
			result[resource] = charged(baseCost[resource], 80);
	return result;
}

int32_t bankingWeeklyGold(const PlayerState & state)
{
	const int64_t perHolder = std::min<int64_t>(1000, std::max<int64_t>(0, state.resources[EGameResID::GOLD]) / 100);
	int64_t result = 0;
	for(const auto * hero : state.getHeroes())
		if(hero->hasActivePerk("new-horizons:estates", "new-horizons:estates.banking"))
			result += perHolder;
	if(result > std::numeric_limits<int32_t>::max())
		throw std::overflow_error("Banking income overflow");
	return static_cast<int32_t>(result);
}

int32_t mercenaryWeeklyCount(const PlayerState & state)
{
	int64_t result = 0;
	for(const auto * hero : state.getHeroes())
		if(hero->hasActivePerk("new-horizons:estates", "new-horizons:estates.mercenaryCompany"))
			result += hero->level;
	if(result > std::numeric_limits<int32_t>::max())
		throw std::overflow_error("Mercenary Company grant overflow");
	return static_cast<int32_t>(result);
}
}
