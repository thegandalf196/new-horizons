/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#include "StdInc.h"
#include "EstatesAdministration.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/hero/NewHorizonsRevisedSpecialties.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/EstatesStewardship.h"
#include "../../lib/mapObjects/NewHorizonsEstatesPolicy.h"

#include <algorithm>
#include <set>

namespace NK2AI
{
namespace
{
/// Cheap greedy preference: a specialty that uses this stewardship, then the
/// Stewardship perk, then any other eligible hero.
int stewardPreference(const CGHeroInstance & hero, const CGTownInstance & town)
{
	if(newHorizonsHeroes::benefitsFromStewardship(hero, town))
		return 2;
	return hero.hasActivePerk("new-horizons:estates", "new-horizons:estates.stewardship") ? 1 : 0;
}
}

void appointAvailableStewards(CCallback & callback, PlayerColor owner)
{
	const auto * state = callback.getPlayerState(owner);
	if(!state)
		return;
	auto heroes = state->getHeroes();
	auto towns = state->getTowns();
	std::ranges::sort(heroes, {}, &CGHeroInstance::id);
	std::set<ObjectInstanceID> appointed;
	for(const auto & [town, hero] : state->estates.stewards)
		appointed.insert(hero);
	std::erase_if(towns, [](const auto * town) { return newHorizonsEstates::townSteward(*town) != nullptr; });
	// Richest towns first; one income evaluation per town keeps the pass flat.
	std::vector<std::pair<int, const CGTownInstance *>> ranked;
	for(const auto * town : towns)
		ranked.emplace_back(town->dailyIncome()[EGameResID::GOLD], town);
	std::ranges::sort(ranked, [](const auto & left, const auto & right)
	{
		return left.first != right.first ? left.first > right.first : left.second->id < right.second->id;
	});
	for(const auto & [income, town] : ranked)
	{
		const CGHeroInstance * best = nullptr;
		int bestPreference = -1;
		for(const auto * hero : heroes)
		{
			if(appointed.contains(hero->id) || !newHorizonsEstates::eligibleSteward(hero))
				continue;
			const int preference = stewardPreference(*hero, *town);
			if(preference > bestPreference)
			{
				best = hero;
				bestPreference = preference;
			}
		}
		if(!best)
			break;
		if(callback.assignTownSteward(town->id, best->id) >= 0)
			appointed.insert(best->id);
	}
}
}
