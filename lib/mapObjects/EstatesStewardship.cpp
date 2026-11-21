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
namespace
{
constexpr auto STEWARDSHIP_SKILL = "new-horizons:estates";
constexpr auto STEWARDSHIP_PERK = "new-horizons:estates.stewardship";
}

bool eligibleSteward(const CGHeroInstance * hero)
{
	if(!hero || !hero->getOwner().isValidPlayer())
		return false;
	if(usesCoreStewardship(hero->getPerkState().getRules()))
		return true;
	return hasRevisedPerk(hero, STEWARDSHIP_PERK);
}

int32_t stewardGold(const CGHeroInstance & hero)
{
	if(usesCoreStewardship(hero.getPerkState().getRules())
		&& !hero.hasActivePerk(STEWARDSHIP_SKILL, STEWARDSHIP_PERK))
		return CORE_STEWARD_GOLD;
	return STEWARDSHIP_PERK_GOLD;
}

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
		if(hero->id == it->second && hero->getOwner() == town.getOwner() && eligibleSteward(hero))
			return hero;
	return nullptr;
}

const CGTownInstance * stewardedTown(const CGHeroInstance & hero)
{
	if(!hero.getOwner().isValidPlayer())
		return nullptr;
	const auto * state = hero.cb->getPlayerState(hero.getOwner());
	if(!state)
		return nullptr;
	for(const auto & [townID, heroID] : state->estates.stewards)
	{
		if(heroID != hero.id)
			continue;
		const auto * town = hero.cb->getTown(townID);
		return town && townSteward(*town) == &hero ? town : nullptr;
	}
	return nullptr;
}
}
