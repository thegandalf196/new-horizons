/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CGTownInstance.h"
#include "NewHorizonsEstatesPolicy.h"
#include "../CPlayerState.h"
#include "../callback/IGameInfoCallback.h"
#include "../callback/IGameEventCallback.h"
#include "../networkPacks/PacksForClient.h"
#include <algorithm>

void CGTownInstance::removeCapitols(IGameEventCallback & gameEvents, const PlayerColor & owner) const
{
	if (hasCapitol()) // search if there's an older capitol
	{
		const PlayerState* state = cb->getPlayerState(owner); //get all towns owned by player
		const auto towns = state->getTowns();
		const auto others = std::count_if(towns.begin(), towns.end(),
			[this](const auto * town) { return town != this && town->hasCapitol(); });
		if(static_cast<size_t>(others) >= newHorizonsEstates::capitolLimit(*state))
		{
			RazeStructures rs;
			rs.tid = id;
			rs.bid.insert(BuildingID::CAPITOL);
			rs.destroyed = destroyed;
			gameEvents.sendAndApply(rs);
		}
	}
}
