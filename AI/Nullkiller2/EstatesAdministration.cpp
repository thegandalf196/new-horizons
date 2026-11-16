/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#include "StdInc.h"
#include "EstatesAdministration.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/EstatesStewardship.h"
#include "../../lib/mapObjects/NewHorizonsEstatesPolicy.h"

#include <algorithm>
#include <set>

namespace NK2AI
{
void appointAvailableStewards(CCallback & callback, PlayerColor owner)
{
	const auto * state = callback.getPlayerState(owner);
	if(!state)
		return;
	auto heroes = state->getHeroes();
	auto towns = state->getTowns();
	std::ranges::sort(heroes, {}, &CGHeroInstance::id);
	std::ranges::sort(towns, {}, &CGTownInstance::id);
	std::set<ObjectInstanceID> appointed;
	for(const auto & [town, hero] : state->estates.stewards)
		appointed.insert(hero);
	for(const auto * town : towns)
	{
		if(newHorizonsEstates::townSteward(*town))
			continue;
		const auto candidate = std::find_if(heroes.begin(), heroes.end(), [&appointed](const auto * hero)
		{
			return !appointed.contains(hero->id)
				&& newHorizonsEstates::hasRevisedPerk(hero, "new-horizons:estates.stewardship");
		});
		if(candidate == heroes.end())
			break;
		if(callback.assignTownSteward(town->id, (*candidate)->id) >= 0)
			appointed.insert((*candidate)->id);
	}
}
}
