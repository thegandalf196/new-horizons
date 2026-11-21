/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#include "StdInc.h"
#include "CGameHandler.h"
#include "ServerNetPackVisitors.h"
#include "../lib/CPlayerState.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/mapObjects/CGTownInstance.h"
#include "../lib/mapObjects/NewHorizonsEstatesPolicy.h"
#include "../lib/mapObjects/EstatesStewardship.h"
#include "../lib/networkPacks/EstatesPacks.h"

bool CGameHandler::assignTownSteward(ObjectInstanceID townID, ObjectInstanceID heroID, PlayerColor player)
{
	const auto * town = gameInfo().getTown(townID);
	if(!town || town->getOwner() != player || !player.isValidPlayer())
	{
		complain("Only an owned town may receive a steward");
		return false;
	}
	const auto * hero = heroID == ObjectInstanceID::NONE ? nullptr : gameInfo().getHero(heroID);
	const auto * state = gameInfo().getPlayerState(player);
	if(!state)
		return false;
	if(heroID != ObjectInstanceID::NONE && (!hero || hero->getOwner() != player
		|| !newHorizonsEstates::eligibleSteward(hero)
		|| !vstd::contains(state->getHeroes(), hero)))
	{
		complain("The selected hero is not an eligible active steward");
		return false;
	}
	EstatesKingdomChanged update;
	update.owner = player;
	update.before = update.after = state->estates;
	update.after.stewards.erase(townID);
	if(hero)
	{
		std::erase_if(update.after.stewards, [heroID](const auto & entry) { return entry.second == heroID; });
		update.after.stewards.emplace(townID, heroID);
	}
	update.after.validate();
	if(update.after != update.before)
		sendAndApply(update);
	return true;
}

void ApplyGhNetPackVisitor::visitAssignTownSteward(AssignTownSteward & pack)
{
	gh.throwIfWrongPlayer(connection, &pack);
	gh.throwIfPlayerNotActive(connection, &pack);
	result = gh.assignTownSteward(pack.town, pack.hero, pack.player);
}
