/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#include "StdInc.h"
#include "EstatesStewardship.h"
#include "CGTownInstance.h"
#include "CGHeroInstance.h"
#include "NewHorizonsEstatesPolicy.h"
#include "../CPlayerState.h"
#include "../callback/IGameInfoCallback.h"

namespace newHorizonsEstates
{
const CGHeroInstance * townSteward(const CGTownInstance & town)
{
	if(!town.getOwner().isValidPlayer())
		return nullptr;
	const auto * state = town.cb->getPlayerState(town.getOwner());
	if(!state)
		return nullptr;
	const auto it = state->estates.stewards.find(town.id);
	if(it == state->estates.stewards.end())
		return nullptr;
	for(const auto * hero : state->getHeroes())
		if(hero->id == it->second && hero->getOwner() == town.getOwner()
			&& hasRevisedPerk(hero, "new-horizons:estates.stewardship"))
			return hero;
	return nullptr;
}
}
